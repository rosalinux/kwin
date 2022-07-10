/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "switcheritem.h"
// KWin
#include "composite.h"
#include "output.h"
#include "screens.h"
#include "tabboxhandler.h"
#include "workspace.h"
// Qt
#include <QAbstractItemModel>
#include <QTimer>

namespace KWin
{
namespace TabBox
{

SwitcherItem::SwitcherItem(QObject *parent)
    : QObject(parent)
    , m_model(nullptr)
    , m_item(nullptr)
    , m_visible(false)
    , m_allDesktops(false)
    , m_currentIndex(0)
    , m_hidingTimer(nullptr)
{
    m_selectedIndexConnection = connect(tabBox, &TabBoxHandler::selectedIndexChanged, this, [this] {
        if (isVisible()) {
            setCurrentIndex(tabBox->currentIndex().row());
        }
    });
    connect(screens(), &Screens::changed, this, &SwitcherItem::screenGeometryChanged);
    connect(Compositor::self(), &Compositor::compositingToggled, this, &SwitcherItem::compositingChanged);

    m_hidingTimer = new QTimer(this);
    m_hidingTimer->setSingleShot(true);
    m_hidingTimer->setInterval(0);
    m_hidingTimer->callOnTimeout([this]() {
        setVisible(false);
    });
    connect(this, &SwitcherItem::aboutToHide, m_hidingTimer, qOverload<>(&QTimer::start));
    connect(this, &SwitcherItem::aboutToShow, m_hidingTimer, &QTimer::stop);
}

SwitcherItem::~SwitcherItem()
{
    disconnect(m_selectedIndexConnection);
}

void SwitcherItem::setItem(QObject *item)
{
    if (m_item == item) {
        return;
    }
    m_item = item;
    Q_EMIT itemChanged();
}

void SwitcherItem::setModel(QAbstractItemModel *model)
{
    m_model = model;
    Q_EMIT modelChanged();
}

void SwitcherItem::setVisible(bool visible)
{
    if (m_visible == visible) {
        return;
    }
    if (visible) {
        Q_EMIT screenGeometryChanged();
    }
    m_hidingTimer->stop();
    m_visible = visible;
    Q_EMIT visibleChanged();
}

QRect SwitcherItem::screenGeometry() const
{
    return workspace()->activeOutput()->geometry();
}

void SwitcherItem::setCurrentIndex(int index)
{
    if (m_currentIndex == index) {
        return;
    }
    m_currentIndex = index;
    if (m_model) {
        tabBox->setCurrentIndex(m_model->index(index, 0));
    }
    Q_EMIT currentIndexChanged(m_currentIndex);
}

void SwitcherItem::setAllDesktops(bool all)
{
    if (m_allDesktops == all) {
        return;
    }
    m_allDesktops = all;
    Q_EMIT allDesktopsChanged();
}

void SwitcherItem::setNoModifierGrab(bool set)
{
    if (m_noModifierGrab == set) {
        return;
    }
    m_noModifierGrab = set;
    Q_EMIT noModifierGrabChanged();
}

int SwitcherItem::hidingDelay() const
{
    return m_hidingTimer->interval();
}

void SwitcherItem::setHidingDelay(int delay)
{
    if (m_hidingTimer->interval() == delay) {
        return;
    }
    m_hidingTimer->setInterval(delay);
    Q_EMIT hidingDelayChanged();
}

bool SwitcherItem::compositing()
{
    return Compositor::compositing();
}

}
}
