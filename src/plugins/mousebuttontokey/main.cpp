/*
    SPDX-FileCopyrightText: 2022 David Redondo <kde@david-redono.de>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include <main.h>
#include <plugin.h>

#include "mousebuttonrebindfilter.h"

class KWIN_EXPORT MouseButtonToKeyFactory : public KWin::PluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginFactory_iid FILE "metadata.json")
    Q_INTERFACES(KWin::PluginFactory)

public:
    explicit MouseButtonToKeyFactory(QObject *parent = nullptr)
        : PluginFactory(parent)
    {
    }

    KWin::Plugin *create() const override
    {
        switch (KWin::kwinApp()->operationMode()) {
        case KWin::Application::OperationModeXwayland:
            [[fallthrough]];
        case KWin::Application::OperationModeWaylandOnly:
            return new MouseButtonRebindFilter();
        case KWin::Application::OperationModeX11:
            [[fallthrough]];
        default:
            return nullptr;
        }
    }
};

#include "main.moc"
