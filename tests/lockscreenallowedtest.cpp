/*
    SPDX-FileCopyrightText: 2022 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "qwayland-kde-lockscreenallowed-v1.h"
#include <QWaylandClientExtensionTemplate>
#include <QtWidgets>
#include <qpa/qplatformnativeinterface.h>

class WaylandAboveLockscreen : public QWaylandClientExtensionTemplate<WaylandAboveLockscreen>, public QtWayland::kde_lockscreenallowed_v1
{
public:
    WaylandAboveLockscreen()
        : QWaylandClientExtensionTemplate<WaylandAboveLockscreen>(1)
    {
        QMetaObject::invokeMethod(this, "addRegistryListener");
    }

    void allowWindow(QWindow *window)
    {
        QPlatformNativeInterface *native = qGuiApp->platformNativeInterface();
        wl_surface *surface = reinterpret_cast<wl_surface *>(native->nativeResourceForWindow(QByteArrayLiteral("surface"), window));
        allow(surface);
    }
};

int main(int argc, char *argv[])
{

    QApplication app(argc, argv);
    QWidget window1(nullptr, Qt::Window);
    window1.setWindowTitle("Window 1");
    window1.setLayout(new QVBoxLayout);
    QPushButton p("Lock && Raise the Window 2");
    window1.layout()->addWidget(&p);
    window1.show();

    WaylandAboveLockscreen aboveLockscreen;
    Q_ASSERT(aboveLockscreen.isInitialized());

    QWidget window2(nullptr, Qt::Window);
    window2.setWindowTitle("Window 2");
    window2.setLayout(new QVBoxLayout);
    QPushButton p2("Close");
    window2.layout()->addWidget(&p2);

    auto raiseWindow2 = [&] {
        aboveLockscreen.allowWindow(window2.windowHandle());
    };

    QObject::connect(&p, &QPushButton::clicked, &app, [&] {
        QProcess::execute("loginctl", {"lock-session"});
        window2.showFullScreen();
        QTimer::singleShot(3000, &app, raiseWindow2);
    });

    QObject::connect(&p2, &QPushButton::clicked, &window2, &QWidget::close);

    return app.exec();
}
