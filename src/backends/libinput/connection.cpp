/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "connection.h"
#include "context.h"
#include "device.h"
#include "events.h"

// TODO: Make it compile also in testing environment
#ifndef KWIN_BUILD_TESTING
#include "main.h"
#include "output.h"
#include "platform.h"
#include "window.h"
#include "workspace.h"
#endif

#include "input_event.h"
#include "libinput_logging.h"
#include "session.h"
#include "udev.h"
#include "utils/realtime.h"

#include <QDBusConnection>
#include <QMutexLocker>
#include <QSocketNotifier>

#include <cmath>
#include <libinput.h>

namespace KWin
{
namespace LibInput
{

class ConnectionAdaptor : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.InputDeviceManager")
    Q_PROPERTY(QStringList devicesSysNames READ devicesSysNames CONSTANT)

private:
    Connection *m_con;

public:
    ConnectionAdaptor(Connection *con)
        : QObject(con)
        , m_con(con)
    {
        connect(con, &Connection::deviceAdded, this, [this](LibInput::Device *inputDevice) {
            Q_EMIT deviceAdded(inputDevice->sysName());
        });
        connect(con, &Connection::deviceRemoved, this, [this](LibInput::Device *inputDevice) {
            Q_EMIT deviceRemoved(inputDevice->sysName());
        });

        QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin/InputDevice"),
                                                     QStringLiteral("org.kde.KWin.InputDeviceManager"),
                                                     this,
                                                     QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals);
    }

    ~ConnectionAdaptor() override
    {
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/org/kde/KWin/InputDeviceManager"));
    }

    QStringList devicesSysNames()
    {
        return m_con->devicesSysNames();
    }

Q_SIGNALS:
    void deviceAdded(QString sysName);
    void deviceRemoved(QString sysName);
};

Connection *Connection::s_self = nullptr;

static ConnectionAdaptor *s_adaptor = nullptr;
static Context *s_context = nullptr;

Connection::Connection(QObject *parent)
    : Connection(nullptr, parent)
{
    // only here to fix build, using will crash, BUG 343529
}

Connection *Connection::create(QObject *parent)
{
    Q_ASSERT(!s_self);
    static Udev s_udev;
    if (!s_udev.isValid()) {
        qCWarning(KWIN_LIBINPUT) << "Failed to initialize udev";
        return nullptr;
    }
    if (!s_context) {
        s_context = new Context(s_udev);
        if (!s_context->isValid()) {
            qCWarning(KWIN_LIBINPUT) << "Failed to create context from udev";
            delete s_context;
            s_context = nullptr;
            return nullptr;
        }
        const QString seat = kwinApp()->platform()->session()->seat();
        if (!s_context->assignSeat(seat.toUtf8().constData())) {
            qCWarning(KWIN_LIBINPUT) << "Failed to assign seat" << seat;
            delete s_context;
            s_context = nullptr;
            return nullptr;
        }
    }
    s_self = new Connection(s_context);
    if (!s_adaptor) {
        s_adaptor = new ConnectionAdaptor(s_self);
    }

    return s_self;
}

Connection::Connection(Context *input, QObject *parent)
    : QObject(parent)
    , m_input(input)
    , m_notifier(nullptr)
{
    Q_ASSERT(m_input);
    // need to connect to KGlobalSettings as the mouse KCM does not emit a dedicated signal
    QDBusConnection::sessionBus().connect(QString(), QStringLiteral("/KGlobalSettings"), QStringLiteral("org.kde.KGlobalSettings"),
                                          QStringLiteral("notifyChange"), this, SLOT(slotKGlobalSettingsNotifyChange(int, int)));
}

Connection::~Connection()
{
    delete s_adaptor;
    s_adaptor = nullptr;
    s_self = nullptr;
    delete s_context;
    s_context = nullptr;
}

void Connection::setup()
{
    QMetaObject::invokeMethod(this, "doSetup", Qt::QueuedConnection);
}

void Connection::doSetup()
{
    Q_ASSERT(!m_notifier);

    gainRealTime();

    m_notifier = new QSocketNotifier(m_input->fileDescriptor(), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Connection::handleEvent);

    connect(kwinApp()->platform()->session(), &Session::activeChanged, this, [this](bool active) {
        if (active) {
            if (!m_input->isSuspended()) {
                return;
            }
            m_input->resume();
        } else {
            deactivate();
        }
    });
    connect(kwinApp()->platform(), &Platform::screensQueried, this, &Connection::updateScreens);
    handleEvent();
}

void Connection::deactivate()
{
    if (m_input->isSuspended()) {
        return;
    }
    m_input->suspend();
    handleEvent();
}

void Connection::handleEvent()
{
    QMutexLocker locker(&m_mutex);
    const bool wasEmpty = m_eventQueue.empty();
    do {
        m_input->dispatch();
        std::unique_ptr<Event> event = m_input->event();
        if (!event) {
            break;
        }
        m_eventQueue.push_back(std::move(event));
    } while (true);
    if (wasEmpty && !m_eventQueue.empty()) {
        Q_EMIT eventsRead();
    }
}

#ifndef KWIN_BUILD_TESTING
QPointF devicePointToGlobalPosition(const QPointF &devicePos, const Output *output)
{
    using Transform = Output::Transform;

    QPointF pos = devicePos;
    // TODO: Do we need to handle the flipped cases differently?
    switch (output->transform()) {
    case Transform::Normal:
    case Transform::Flipped:
        break;
    case Transform::Rotated90:
    case Transform::Flipped90:
        pos = QPointF(output->modeSize().height() - devicePos.y(), devicePos.x());
        break;
    case Transform::Rotated180:
    case Transform::Flipped180:
        pos = QPointF(output->modeSize().width() - devicePos.x(),
                      output->modeSize().height() - devicePos.y());
        break;
    case Transform::Rotated270:
    case Transform::Flipped270:
        pos = QPointF(devicePos.y(), output->modeSize().width() - devicePos.x());
        break;
    default:
        Q_UNREACHABLE();
    }
    return output->geometry().topLeft() + pos / output->scale();
}
#endif

KWin::TabletToolId createTabletId(libinput_tablet_tool *tool, void *userData)
{
    auto serial = libinput_tablet_tool_get_serial(tool);
    auto toolId = libinput_tablet_tool_get_tool_id(tool);
    auto type = libinput_tablet_tool_get_type(tool);
    InputRedirection::TabletToolType toolType;
    switch (type) {
    case LIBINPUT_TABLET_TOOL_TYPE_PEN:
        toolType = InputRedirection::Pen;
        break;
    case LIBINPUT_TABLET_TOOL_TYPE_ERASER:
        toolType = InputRedirection::Eraser;
        break;
    case LIBINPUT_TABLET_TOOL_TYPE_BRUSH:
        toolType = InputRedirection::Brush;
        break;
    case LIBINPUT_TABLET_TOOL_TYPE_PENCIL:
        toolType = InputRedirection::Pencil;
        break;
    case LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH:
        toolType = InputRedirection::Airbrush;
        break;
    case LIBINPUT_TABLET_TOOL_TYPE_MOUSE:
        toolType = InputRedirection::Mouse;
        break;
    case LIBINPUT_TABLET_TOOL_TYPE_LENS:
        toolType = InputRedirection::Lens;
        break;
    case LIBINPUT_TABLET_TOOL_TYPE_TOTEM:
        toolType = InputRedirection::Totem;
        break;
    }
    QVector<InputRedirection::Capability> capabilities;
    if (libinput_tablet_tool_has_pressure(tool)) {
        capabilities << InputRedirection::Pressure;
    }
    if (libinput_tablet_tool_has_distance(tool)) {
        capabilities << InputRedirection::Distance;
    }
    if (libinput_tablet_tool_has_rotation(tool)) {
        capabilities << InputRedirection::Rotation;
    }
    if (libinput_tablet_tool_has_tilt(tool)) {
        capabilities << InputRedirection::Tilt;
    }
    if (libinput_tablet_tool_has_slider(tool)) {
        capabilities << InputRedirection::Slider;
    }
    if (libinput_tablet_tool_has_wheel(tool)) {
        capabilities << InputRedirection::Wheel;
    }
    return {toolType, capabilities, serial, toolId, userData};
}

void Connection::handleDiscreteAxis(PointerEvent *pointerEvent)
{
    const auto axes = pointerEvent->axis();
    for (const InputRedirection::PointerAxis &axis : axes) {
        Q_EMIT pointerEvent->device()->pointerAxisChanged(axis,
                                                          pointerEvent->scrollValue(axis),
                                                          pointerEvent->scrollValueV120(axis),
                                                          pointerEvent->axisSource(),
                                                          pointerEvent->time(),
                                                          pointerEvent->device());
    }
}

void Connection::handleContinuousAxis(PointerEvent *pointerEvent)
{
    const auto axes = pointerEvent->axis();

    for (const InputRedirection::PointerAxis &axis : axes) {
        Q_EMIT pointerEvent->device()->pointerAxisChanged(axis,
                                                          pointerEvent->scrollValue(axis),
                                                          0,
                                                          pointerEvent->axisSource(),
                                                          pointerEvent->time(),
                                                          pointerEvent->device());
    }
}

void Connection::processEvents()
{
    QMutexLocker locker(&m_mutex);
    while (m_eventQueue.size() != 0) {
        std::unique_ptr<Event> event = std::move(m_eventQueue.front());
        m_eventQueue.pop_front();
        switch (event->type()) {
        case LIBINPUT_EVENT_DEVICE_ADDED: {
            auto device = new Device(event->nativeDevice());
            device->moveToThread(thread());
            m_devices << device;

            applyDeviceConfig(device);
            applyScreenToDevice(device);

            Q_EMIT deviceAdded(device);
            break;
        }
        case LIBINPUT_EVENT_DEVICE_REMOVED: {
            auto it = std::find_if(m_devices.begin(), m_devices.end(), [&event](Device *d) {
                return event->device() == d;
            });
            if (it == m_devices.end()) {
                // we don't know this device
                break;
            }
            auto device = *it;
            m_devices.erase(it);
            Q_EMIT deviceRemoved(device);
            device->deleteLater();
            break;
        }
        case LIBINPUT_EVENT_KEYBOARD_KEY: {
            KeyEvent *ke = static_cast<KeyEvent *>(event.get());
            Q_EMIT ke->device()->keyChanged(ke->key(), ke->state(), ke->time(), ke->device());
            break;
        }
        case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL: {
            handleDiscreteAxis(static_cast<PointerEvent *>(event.get()));
            break;
        }
        case LIBINPUT_EVENT_POINTER_SCROLL_FINGER: {
            handleContinuousAxis(static_cast<PointerEvent *>(event.get()));
            break;
        }
        case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS: {
            handleContinuousAxis(static_cast<PointerEvent *>(event.get()));
            break;
        }
        case LIBINPUT_EVENT_POINTER_BUTTON: {
            PointerEvent *pe = static_cast<PointerEvent *>(event.get());
            Q_EMIT pe->device()->pointerButtonChanged(pe->button(), pe->buttonState(), pe->time(), pe->device());
            break;
        }
        case LIBINPUT_EVENT_POINTER_MOTION: {
            PointerEvent *pe = static_cast<PointerEvent *>(event.get());
            auto delta = pe->delta();
            auto deltaNonAccel = pe->deltaUnaccelerated();
            quint32 latestTime = pe->time();
            quint64 latestTimeUsec = pe->timeMicroseconds();
            auto it = m_eventQueue.begin();
            while (it != m_eventQueue.end()) {
                if ((*it)->type() == LIBINPUT_EVENT_POINTER_MOTION) {
                    std::unique_ptr<PointerEvent> p{static_cast<PointerEvent *>(it->release())};
                    delta += p->delta();
                    deltaNonAccel += p->deltaUnaccelerated();
                    latestTime = p->time();
                    latestTimeUsec = p->timeMicroseconds();
                    it = m_eventQueue.erase(it);
                } else {
                    break;
                }
            }
            Q_EMIT pe->device()->pointerMotion(delta, deltaNonAccel, latestTime, latestTimeUsec, pe->device());
            break;
        }
        case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
            PointerEvent *pe = static_cast<PointerEvent *>(event.get());
            Q_EMIT pe->device()->pointerMotionAbsolute(pe->absolutePos(workspace()->geometry().size()), pe->time(), pe->device());
            break;
        }
        case LIBINPUT_EVENT_TOUCH_DOWN: {
#ifndef KWIN_BUILD_TESTING
            TouchEvent *te = static_cast<TouchEvent *>(event.get());
            const auto *output = te->device()->output();
            const QPointF globalPos = devicePointToGlobalPosition(te->absolutePos(output->modeSize()), output);
            Q_EMIT te->device()->touchDown(te->id(), globalPos, te->time(), te->device());
            break;
#endif
        }
        case LIBINPUT_EVENT_TOUCH_UP: {
            TouchEvent *te = static_cast<TouchEvent *>(event.get());
            Q_EMIT te->device()->touchUp(te->id(), te->time(), te->device());
            break;
        }
        case LIBINPUT_EVENT_TOUCH_MOTION: {
#ifndef KWIN_BUILD_TESTING
            TouchEvent *te = static_cast<TouchEvent *>(event.get());
            const auto *output = te->device()->output();
            const QPointF globalPos = devicePointToGlobalPosition(te->absolutePos(output->modeSize()), output);
            Q_EMIT te->device()->touchMotion(te->id(), globalPos, te->time(), te->device());
            break;
#endif
        }
        case LIBINPUT_EVENT_TOUCH_CANCEL: {
            Q_EMIT event->device()->touchCanceled(event->device());
            break;
        }
        case LIBINPUT_EVENT_TOUCH_FRAME: {
            Q_EMIT event->device()->touchFrame(event->device());
            break;
        }
        case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN: {
            PinchGestureEvent *pe = static_cast<PinchGestureEvent *>(event.get());
            Q_EMIT pe->device()->pinchGestureBegin(pe->fingerCount(), pe->time(), pe->device());
            break;
        }
        case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE: {
            PinchGestureEvent *pe = static_cast<PinchGestureEvent *>(event.get());
            Q_EMIT pe->device()->pinchGestureUpdate(pe->scale(), pe->angleDelta(), pe->delta(), pe->time(), pe->device());
            break;
        }
        case LIBINPUT_EVENT_GESTURE_PINCH_END: {
            PinchGestureEvent *pe = static_cast<PinchGestureEvent *>(event.get());
            if (pe->isCancelled()) {
                Q_EMIT pe->device()->pinchGestureCancelled(pe->time(), pe->device());
            } else {
                Q_EMIT pe->device()->pinchGestureEnd(pe->time(), pe->device());
            }
            break;
        }
        case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN: {
            SwipeGestureEvent *se = static_cast<SwipeGestureEvent *>(event.get());
            Q_EMIT se->device()->swipeGestureBegin(se->fingerCount(), se->time(), se->device());
            break;
        }
        case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE: {
            SwipeGestureEvent *se = static_cast<SwipeGestureEvent *>(event.get());
            Q_EMIT se->device()->swipeGestureUpdate(se->delta(), se->time(), se->device());
            break;
        }
        case LIBINPUT_EVENT_GESTURE_SWIPE_END: {
            SwipeGestureEvent *se = static_cast<SwipeGestureEvent *>(event.get());
            if (se->isCancelled()) {
                Q_EMIT se->device()->swipeGestureCancelled(se->time(), se->device());
            } else {
                Q_EMIT se->device()->swipeGestureEnd(se->time(), se->device());
            }
            break;
        }
        case LIBINPUT_EVENT_GESTURE_HOLD_BEGIN: {
            HoldGestureEvent *he = static_cast<HoldGestureEvent *>(event.get());
            Q_EMIT he->device()->holdGestureBegin(he->fingerCount(), he->time(), he->device());
            break;
        }
        case LIBINPUT_EVENT_GESTURE_HOLD_END: {
            HoldGestureEvent *he = static_cast<HoldGestureEvent *>(event.get());
            if (he->isCancelled()) {
                Q_EMIT he->device()->holdGestureCancelled(he->time(), he->device());
            } else {
                Q_EMIT he->device()->holdGestureEnd(he->time(), he->device());
            }
            break;
        }
        case LIBINPUT_EVENT_SWITCH_TOGGLE: {
            SwitchEvent *se = static_cast<SwitchEvent *>(event.get());
            switch (se->state()) {
            case SwitchEvent::State::Off:
                Q_EMIT se->device()->switchToggledOff(se->time(), se->timeMicroseconds(), se->device());
                break;
            case SwitchEvent::State::On:
                Q_EMIT se->device()->switchToggledOn(se->time(), se->timeMicroseconds(), se->device());
                break;
            default:
                Q_UNREACHABLE();
            }
            break;
        }
        case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
        case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
        case LIBINPUT_EVENT_TABLET_TOOL_TIP: {
            auto *tte = static_cast<TabletToolEvent *>(event.get());

            KWin::InputRedirection::TabletEventType tabletEventType;
            switch (event->type()) {
            case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
                tabletEventType = KWin::InputRedirection::Axis;
                break;
            case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
                tabletEventType = KWin::InputRedirection::Proximity;
                break;
            case LIBINPUT_EVENT_TABLET_TOOL_TIP:
            default:
                tabletEventType = KWin::InputRedirection::Tip;
                break;
            }

            if (workspace()) {
#ifndef KWIN_BUILD_TESTING
                Output *output = tte->device()->output();
                if (!output && workspace()->activeWindow()) {
                    output = workspace()->activeWindow()->output();
                }
                if (!output) {
                    output = workspace()->activeOutput();
                }
                const QPointF globalPos =
                    devicePointToGlobalPosition(tte->transformedPosition(output->modeSize()),
                                                output);
#else
                const QPointF globalPos;
#endif
                Q_EMIT event->device()->tabletToolEvent(tabletEventType,
                                                        globalPos, tte->pressure(),
                                                        tte->xTilt(), tte->yTilt(), tte->rotation(),
                                                        tte->isTipDown(), tte->isNearby(), createTabletId(tte->tool(), event->device()->groupUserData()), tte->time());
            }
            break;
        }
        case LIBINPUT_EVENT_TABLET_TOOL_BUTTON: {
            auto *tabletEvent = static_cast<TabletToolButtonEvent *>(event.get());
            Q_EMIT event->device()->tabletToolButtonEvent(tabletEvent->buttonId(),
                                                          tabletEvent->isButtonPressed(),
                                                          createTabletId(tabletEvent->tool(), event->device()->groupUserData()));
            break;
        }
        case LIBINPUT_EVENT_TABLET_PAD_BUTTON: {
            auto *tabletEvent = static_cast<TabletPadButtonEvent *>(event.get());
            Q_EMIT event->device()->tabletPadButtonEvent(tabletEvent->buttonId(),
                                                         tabletEvent->isButtonPressed(),
                                                         {event->device()->groupUserData()});
            break;
        }
        case LIBINPUT_EVENT_TABLET_PAD_RING: {
            auto *tabletEvent = static_cast<TabletPadRingEvent *>(event.get());
            tabletEvent->position();
            Q_EMIT event->device()->tabletPadRingEvent(tabletEvent->number(),
                                                       tabletEvent->position(),
                                                       tabletEvent->source() == LIBINPUT_TABLET_PAD_RING_SOURCE_FINGER,
                                                       {event->device()->groupUserData()});
            break;
        }
        case LIBINPUT_EVENT_TABLET_PAD_STRIP: {
            auto *tabletEvent = static_cast<TabletPadStripEvent *>(event.get());
            Q_EMIT event->device()->tabletPadStripEvent(tabletEvent->number(),
                                                        tabletEvent->position(),
                                                        tabletEvent->source() == LIBINPUT_TABLET_PAD_STRIP_SOURCE_FINGER,
                                                        {event->device()->groupUserData()});
            break;
        }
        default:
            // nothing
            break;
        }
    }
}

void Connection::updateScreens()
{
    QMutexLocker locker(&m_mutex);
    for (auto device : qAsConst(m_devices)) {
        applyScreenToDevice(device);
    }
}

void Connection::applyScreenToDevice(Device *device)
{
#ifndef KWIN_BUILD_TESTING
    QMutexLocker locker(&m_mutex);
    if (!device->isTouch()) {
        return;
    }

    Output *deviceOutput = nullptr;
    const QVector<Output *> outputs = kwinApp()->platform()->enabledOutputs();
    // let's try to find a screen for it
    if (outputs.count() == 1) {
        deviceOutput = outputs.constFirst();
    }
    if (!deviceOutput && !device->outputName().isEmpty()) {
        // we have an output name, try to find a screen with matching name
        for (Output *output : outputs) {
            if (output->name() == device->outputName()) {
                deviceOutput = output;
                break;
            }
        }
    }
    if (!deviceOutput) {
        // do we have an internal screen?
        Output *internalOutput = nullptr;
        for (Output *output : outputs) {
            if (output->isInternal()) {
                internalOutput = output;
                break;
            }
        }
        auto testScreenMatches = [device](const Output *output) {
            const auto &size = device->size();
            const auto &screenSize = output->physicalSize();
            return std::round(size.width()) == std::round(screenSize.width())
                && std::round(size.height()) == std::round(screenSize.height());
        };
        if (internalOutput && testScreenMatches(internalOutput)) {
            deviceOutput = internalOutput;
        }
        // let's compare all screens for size
        for (Output *output : outputs) {
            if (testScreenMatches(output)) {
                deviceOutput = output;
                break;
            }
        }
        if (!deviceOutput) {
            // still not found
            if (internalOutput) {
                // we have an internal id, so let's use that
                deviceOutput = internalOutput;
            } else if (!outputs.isEmpty()) {
                // just take first screen, we have no clue
                deviceOutput = outputs.constFirst();
            }
        }
    }

    device->setOutput(deviceOutput);

    // TODO: this is currently non-functional even on DRM. Needs orientation() override there.
    device->setOrientation(Qt::PrimaryOrientation);
#else
    Q_UNUSED(device)
#endif
}

void Connection::applyDeviceConfig(Device *device)
{
    KConfigGroup defaults = m_config->group("Libinput").group("Defaults");
    if (defaults.isValid()) {
        if (device->isAlphaNumericKeyboard() && defaults.hasGroup("Keyboard")) {
            defaults = defaults.group("Keyboard");
        }
        if (device->isPointer() && defaults.hasGroup("Pointer")) {
            defaults = defaults.group("Pointer");
        }
        if (device->isTouchpad() && defaults.hasGroup("Touchpad")) {
            defaults = defaults.group("Touchpad");
        }

        device->setDefaultConfig(defaults);
    }

    // pass configuration to Device
    device->setConfig(m_config->group("Libinput").group(QString::number(device->vendor())).group(QString::number(device->product())).group(device->name()));
    device->loadConfiguration();
}

void Connection::slotKGlobalSettingsNotifyChange(int type, int arg)
{
    if (type == 3 /**SettingsChanged**/ && arg == 0 /** SETTINGS_MOUSE */) {
        m_config->reparseConfiguration();
        for (auto it = m_devices.constBegin(), end = m_devices.constEnd(); it != end; ++it) {
            if ((*it)->isPointer()) {
                applyDeviceConfig(*it);
            }
        }
    }
}

QStringList Connection::devicesSysNames() const
{
    QStringList sl;
    for (Device *d : qAsConst(m_devices)) {
        sl.append(d->sysName());
    }
    return sl;
}

}
}

#include "connection.moc"
