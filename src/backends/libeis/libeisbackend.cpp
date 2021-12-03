/*
    SPDX-FileCopyrightText: 2021 David Redondo <kde@david-redondo>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "libeisbackend.h"

#include "abstract_output.h"
#include "backends/libeis/libeis_logging.h"
#include "device.h"
#include "input.h"
#include "main.h"
#include "platform.h"
#include "workspace.h"

#include <QSocketNotifier>

#include <libeis.h>


namespace KWin
{
namespace Libeis
{
static void
eis_log_handler(eis *eis, eis_log_priority priority, const char *file, int lineNumber, const char *function, const char *message, bool is_continuation)
{
    switch (priority) {
    case EIS_LOG_PRIORITY_DEBUG:
        qCDebug(KWIN_EIS) << "Libeis:" << message;
        break;
    case EIS_LOG_PRIORITY_INFO:
        qCInfo(KWIN_EIS) << "Libeis:" << message;
        break;
    case EIS_LOG_PRIORITY_WARNING:
        qCWarning(KWIN_EIS) << "Libeis:" << message;
        break;
    case EIS_LOG_PRIORITY_ERROR:
        qCritical(KWIN_EIS) << "Libeis:" << message;
        break;
    }
}
}

LibeisBackend::LibeisBackend(QObject *parent)
    : InputBackend(parent)
{
    qRegisterMetaType<KWin::InputRedirection::PointerButtonState>();
    qRegisterMetaType<InputRedirection::PointerAxis>();
    qRegisterMetaType<InputRedirection::PointerAxisSource>();
    qRegisterMetaType<InputRedirection::KeyboardKeyState>();
}

LibeisBackend::~LibeisBackend()
{
    if (m_eis) {
        eis_unref(m_eis);
    }
}

void LibeisBackend::initialize()
{
    constexpr int maxSocketNumber = 32;
    QByteArray socketName;
    int socketNum = 0;
    m_eis = eis_new(nullptr);
    do {
        if (socketNum == maxSocketNumber) {
            return;
        }
        socketName = QByteArrayLiteral("eis-") + QByteArray::number(socketNum++);
    } while (eis_setup_backend_socket(m_eis, socketName));

    qputenv("LIBEI_SOCKET", socketName);
    auto env = kwinApp()->processStartupEnvironment();
    env.insert("LIBEI_SOCKET", socketName);
    static_cast<ApplicationWaylandAbstract *>(kwinApp())->setProcessStartupEnvironment(env);

    const auto fd = eis_get_fd(m_eis);
    auto m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &LibeisBackend::handleEvents);

    eis_log_set_priority(m_eis, EIS_LOG_PRIORITY_DEBUG);
    eis_log_set_handler(m_eis, Libeis::eis_log_handler);

    connect(kwinApp()->platform(), &Platform::outputEnabled, this, [this] (AbstractOutput *output) {
        for (const auto seat : m_seatToDevices.keys()) {
            addDevice(seat, output);
        }
    });
}

void LibeisBackend::addDevice(eis_seat *seat, AbstractOutput *output)
{
    auto client = eis_seat_get_client(seat);
    const char *clientName = eis_client_get_name(client);
    auto device = eis_seat_new_device(seat);
    auto inputDevice = new Libeis::Device(device);
    eis_device_set_user_data(device, inputDevice);
    // TODO do we need  keymaps?

    if (output) {
        // Ab absolute device
        const QByteArray name = clientName + QByteArrayLiteral(" absolute device on ") + output->name().toUtf8();
        eis_device_configure_name(device, name);
        eis_device_configure_capability(device, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
        eis_device_configure_capability(device, EIS_DEVICE_CAP_TOUCH);
        auto region = eis_device_new_region(device);
        const QRect outputGeometry = output->geometry();
        eis_region_set_offset(region, outputGeometry.x(), outputGeometry.y());
        eis_region_set_size(region, outputGeometry.width(), outputGeometry.height());
        // TODO Do we need this if our region is in logical coordinates?
        eis_region_set_physical_scale(region, output->scale());
        eis_region_add(region);
        eis_region_unref(region);
        connect(output, &AbstractOutput::enabledChanged, inputDevice, [this, inputDevice, seat] {
            eis_device_remove(inputDevice->eisDevice());
            m_seatToDevices[seat].removeOne(inputDevice);
            Q_EMIT deviceRemoved(inputDevice);
            inputDevice->deleteLater();
        });
        connect(output, &QObject::destroyed, inputDevice, [this, inputDevice, seat] {
            eis_device_remove(inputDevice->eisDevice());
            m_seatToDevices[seat].removeOne(inputDevice);
            Q_EMIT deviceRemoved(inputDevice);
            inputDevice->deleteLater();
        });
        connect(output, &AbstractOutput::geometryChanged, inputDevice, [this, inputDevice, seat, output] {
            // regions on devices have to be static => recreate the device
            eis_device_remove(inputDevice->eisDevice());
            m_seatToDevices[seat].removeOne(inputDevice);
            Q_EMIT deviceRemoved(inputDevice);
            inputDevice->deleteLater();
            addDevice(eis_device_get_seat(inputDevice->eisDevice()), output);
        });
    } else {
        // a relative device
        const QByteArray name = clientName + QByteArrayLiteral(" relative pointer & keyboard");
        eis_device_configure_name(device, name);
        eis_device_configure_capability(device, EIS_DEVICE_CAP_POINTER);
        eis_device_configure_capability(device, EIS_DEVICE_CAP_KEYBOARD);
    }

    m_seatToDevices[seat].push_back(inputDevice);
    Q_EMIT deviceAdded(inputDevice);

    eis_device_add(device);
    eis_device_resume(device);
    eis_device_unref(device);
}

void LibeisBackend::handleEvents()
{
    eis_dispatch(m_eis);
    auto eventDevice = [](eis_event *event) {
        return static_cast<Libeis::Device *>(eis_device_get_user_data(eis_event_get_device(event)));
    };
    while (eis_event *const event = eis_get_event(m_eis)) {
        switch (eis_event_get_type(event)) {
        case EIS_EVENT_CLIENT_CONNECT: {
            auto client = eis_event_get_client(event);
            const char *clientName = eis_client_get_name(client);
            const char *pid = eis_client_property_get(client, "ei.application.pid");
            const char *cmdline = eis_client_property_get(client, "ei.application.cmdline");
            const char *connectionType = eis_client_property_get(client, "ei.connection.type");
            // TODO make secure
            eis_client_connect(client);
            auto seat = eis_client_new_seat(client, QByteArrayLiteral(" seat").prepend(clientName));
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_POINTER);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_KEYBOARD);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_TOUCH);
            eis_seat_add(seat);
            m_seatToDevices.insert(seat, {});
            qCDebug(KWIN_EIS) << "New client" << clientName << "pid:" << pid;
            break;
        }
        case EIS_EVENT_CLIENT_DISCONNECT: {
            auto client = eis_event_get_client(event);
            qCDebug(KWIN_EIS) << "Client disconnected" << eis_client_get_name(client);
            eis_client_disconnect(client);
            break;
        }
        case EIS_EVENT_CLIENT_PROPERTY: {
            const char *clientName = eis_client_get_name(eis_event_get_client(event));
            qCDebug(KWIN_EIS) << "Client " << clientName << "changed property" << eis_event_property_get_name(event) << "to"
                              << eis_event_property_get_value(event);
            break;
        }
        case EIS_EVENT_SEAT_BIND: {
            auto seat = eis_event_get_seat(event);
            addDevice(seat, nullptr);
            for (const auto output : kwinApp()->platform()->enabledOutputs()) {
                addDevice(seat, output);
            }
            qCDebug(KWIN_EIS) << "Client" << eis_client_get_name(eis_event_get_client(event)) << "bound to seat" << eis_seat_get_name(seat);
            break;
        }
        case EIS_EVENT_SEAT_UNBIND: {
            auto seat = eis_event_get_seat(event);
            qCDebug(KWIN_EIS) << "Client" << eis_client_get_name(eis_event_get_client(event)) << "unbound from seat" << eis_seat_get_name(seat);
            auto devices = m_seatToDevices.take(seat);
            for (const auto device : devices) {
                eis_device_remove(device->eisDevice());
                Q_EMIT deviceRemoved(device);
                device->deleteLater();
            }
            eis_seat_remove(seat);
            eis_seat_unref(seat);
            break;
        }
        case EIS_EVENT_DEVICE_CLOSED: {
            auto device = eventDevice(event);
            qCDebug(KWIN_EIS) << "Device" << device->name() << "closed by client";
            eis_device_remove(device->eisDevice());
            m_seatToDevices[eis_device_get_seat(device->eisDevice())];
            Q_EMIT deviceRemoved(device);
            device->deleteLater();
            break;
        }
        case EIS_EVENT_FRAME: {
            auto device = eventDevice(event);
            if (device->isTouch()) {
                qCDebug(KWIN_EIS) << "Frame for touch device" << device->name();
                Q_EMIT device->touchFrame(device);
            }
            break;
        }
        case EIS_EVENT_DEVICE_START_EMULATING: {
            auto device = eventDevice(event);
            qDebug() << "Device" << device->name() << "starts emulating";
            break;
        }
        case EIS_EVENT_DEVICE_STOP_EMULATING: {
            auto device = eventDevice(event);
            qDebug() << "Device" << device->name() << "stops emulating";
            break;
        }
        case EIS_EVENT_POINTER_MOTION: {
            const auto x = eis_event_pointer_get_dx(event);
            const auto y = eis_event_pointer_get_dy(event);
            auto device = eventDevice(event);
            qCDebug(KWIN_EIS) << device->name() << "pointer motion" << x << y;
            const QSizeF delta(x, y);
            // TODO fix  time
            Q_EMIT device->pointerMotion(delta, delta, 0, 0, device);
            break;
        }
        case EIS_EVENT_POINTER_MOTION_ABSOLUTE: {
            const auto x = eis_event_pointer_get_absolute_x(event);
            const auto y = eis_event_pointer_get_absolute_y(event);
            auto device = eventDevice(event);
            qCDebug(KWIN_EIS) << device->name() << "pointer motion absolute" << x << y;
            // TODO fix time
            Q_EMIT device->pointerMotionAbsolute({x, y}, 0, device);
            break;
        }
        case EIS_EVENT_POINTER_BUTTON: {
            const auto button = eis_event_pointer_get_button(event);
            const auto press = eis_event_pointer_get_button_is_press(event);
            auto device = eventDevice(event);
            qCDebug(KWIN_EIS) << device->name() << "pointer button" << button << press;
            // TODO fix time
            Q_EMIT device->pointerButtonChanged(button, press ? InputRedirection::PointerButtonPressed : InputRedirection::PointerButtonReleased, 0, device);
            break;
        }
        case EIS_EVENT_POINTER_SCROLL: {
            const auto x = eis_event_pointer_get_scroll_x(event);
            const auto y = eis_event_pointer_get_scroll_y(event);
            auto device = eventDevice(event);
            qCDebug(KWIN_EIS) << device->name() << "pointer scroll" << x << y;
            // TODO fix time
            if (x != 0) {
                Q_EMIT device->pointerAxisChanged(InputRedirection::PointerAxisHorizontal, x, 0, InputRedirection::PointerAxisSourceUnknown, 0, device);
            }
            if (y != 0) {
                Q_EMIT device->pointerAxisChanged(InputRedirection::PointerAxisVertical, y, 0, InputRedirection::PointerAxisSourceUnknown, 0, device);
            }
            break;
        }
        case EIS_EVENT_POINTER_SCROLL_STOP:
        case EIS_EVENT_POINTER_SCROLL_CANCEL: {
            // TODO how to cancel scroll?
            auto device = eventDevice(event);
            // TODO fix time
            if (eis_event_pointer_get_scroll_stop_x(event)) {
                qCDebug(KWIN_EIS) << device->name() << "pointer x scroll" << (eis_event_get_type(event) == EIS_EVENT_POINTER_SCROLL_STOP ? "stop" : "cancel");
                Q_EMIT device->pointerAxisChanged(InputRedirection::PointerAxisHorizontal, 0, 0, InputRedirection::PointerAxisSourceUnknown, 0, device);
            }
            if (eis_event_pointer_get_scroll_stop_y(event)) {
                qCDebug(KWIN_EIS) << device->name() << "pointer y scroll" << (eis_event_get_type(event) == EIS_EVENT_POINTER_SCROLL_STOP ? "stop" : "cancel");
                Q_EMIT device->pointerAxisChanged(InputRedirection::PointerAxisVertical, 0, 0, InputRedirection::PointerAxisSourceUnknown, 0, device);
            }
            break;
        }
        case EIS_EVENT_POINTER_SCROLL_DISCRETE: {
            const auto x = eis_event_pointer_get_scroll_discrete_x(event);
            const auto y = eis_event_pointer_get_scroll_discrete_y(event);
            auto device = eventDevice(event);
            qCDebug(KWIN_EIS) << device->name() << "pointer scroll discrete" << x << y;
            constexpr int clickAmount = 120;
            constexpr int anglePerClick = 15;
            if (x != 0) {
                const int steps = x / clickAmount;
                // TODO fix time
                Q_EMIT device->pointerAxisChanged(InputRedirection::PointerAxisHorizontal,
                                                  steps * anglePerClick,
                                                  steps,
                                                  InputRedirection::PointerAxisSourceUnknown,
                                                  0,
                                                  device);
            }
            if (y != 0) {
                const int steps = y / clickAmount;
                // TODO fix time
                Q_EMIT device->pointerAxisChanged(InputRedirection::PointerAxisVertical,
                                                  steps * anglePerClick,
                                                  steps,
                                                  InputRedirection::PointerAxisSourceUnknown,
                                                  0,
                                                  device);
            }
            break;
        }
        case EIS_EVENT_KEYBOARD_KEY: {
            const auto key = eis_event_keyboard_get_key(event);
            const auto press = eis_event_keyboard_get_key_is_press(event);
            auto device = eventDevice(event);
            qCDebug(KWIN_EIS) << device->name() << "key" << key << press;
            // TODO fix time
            Q_EMIT device->keyChanged(key, press ? InputRedirection::KeyboardKeyPressed : InputRedirection::KeyboardKeyReleased, 0, device);
            break;
        }
        case EIS_EVENT_TOUCH_DOWN: {
            const auto x = eis_event_touch_get_x(event);
            const auto y = eis_event_touch_get_y(event);
            const auto id = eis_event_touch_get_id(event);
            auto device = eventDevice(event);
            qCDebug(KWIN_EIS) << device->name() << "touch down" << id << x << y;
            // TODO fix time
            Q_EMIT device->touchDown(id, {x, y}, 0, device);
            break;
        }
        case EIS_EVENT_TOUCH_UP: {
            const auto id = eis_event_touch_get_id(event);
            auto device = eventDevice(event);
            qCDebug(KWIN_EIS) << device->name() << "touch up" << id;
            // TODO fix time
            Q_EMIT device->touchUp(id, 0, device);
            break;
        }
        case EIS_EVENT_TOUCH_MOTION: {
            const auto x = eis_event_touch_get_x(event);
            const auto y = eis_event_touch_get_y(event);
            const auto id = eis_event_touch_get_id(event);
            auto device = eventDevice(event);
            qCDebug(KWIN_EIS) << device->name() << "touch move" << id << x << y;
            // TODO fix time
            Q_EMIT device->touchMotion(id, {x, y}, 0, device);
            break;
        }
        }
        eis_event_unref(event);
    }
}
}
