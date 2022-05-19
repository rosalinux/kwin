/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 MBition GmbH
    SPDX-FileContributor: Kai Uwe Broulik <kai_uwe.broulik@mbition.io>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screenedgeeffect.h"
// Qt
#include <QQuickItem>
#include <QStandardPaths>
#include <QTimer>
// KWin
#include <kwinoffscreenquickview.h>

namespace KWin
{

ScreenEdgeEffect::ScreenEdgeEffect()
    : Effect()
    , m_cleanupTimer(new QTimer(this))
{
    connect(effects, &EffectsHandler::screenEdgeApproaching, this, &ScreenEdgeEffect::edgeApproaching);
    m_cleanupTimer->setInterval(5000);
    m_cleanupTimer->setSingleShot(true);
    connect(m_cleanupTimer, &QTimer::timeout, this, &ScreenEdgeEffect::cleanup);
    connect(effects, &EffectsHandler::screenLockingChanged, this, [this](bool locked) {
        if (locked) {
            cleanup();
        }
    });
}

ScreenEdgeEffect::~ScreenEdgeEffect()
{
    cleanup();
}

void ScreenEdgeEffect::cleanup()
{
    for (auto *glow : std::as_const(m_borders)) {
        effects->addRepaint(glow->geometry());
    }
    qDeleteAll(m_borders);
    m_borders.clear();
}

void ScreenEdgeEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    effects->prePaintScreen(data, presentTime);
    for (auto *glow : std::as_const(m_borders)) {
        if (qFuzzyIsNull(glow->opacity())) {
            continue;
        }
        data.paint += glow->geometry();
    }
}

void ScreenEdgeEffect::paintScreen(int mask, const QRegion &region, ScreenPaintData &data)
{
    effects->paintScreen(mask, region, data);
    for (auto *border : std::as_const(m_borders)) {
        effects->renderOffscreenQuickView(border);
    }
}

void ScreenEdgeEffect::edgeApproaching(ElectricBorder border, qreal factor, const QRect &geometry)
{
    auto it = m_borders.find(border);
    if (it != m_borders.end()) {
        auto *border = *it;
        // need to update
        effects->addRepaint(border->geometry());

        if (border->geometry() != geometry) {
            border->setGeometry(geometry);
            effects->addRepaint(geometry);
        }

        border->setOpacity(factor);

        if (qFuzzyIsNull(factor)) {
            m_cleanupTimer->start();
        } else {
            m_cleanupTimer->stop();
        }
    } else if (!qFuzzyIsNull(factor)) {
        // need to generate new Glow
        auto *glow = createGlow(border, factor, geometry);
        if (glow) {
            m_borders.insert(border, glow);
            effects->addRepaint(geometry);
        }
    }
}

OffscreenQuickScene *ScreenEdgeEffect::createGlow(ElectricBorder border, qreal factor, const QRect &geometry)
{
    QString state;
    switch (border) {
    case ElectricTop:
        state = QStringLiteral("topedge");
        break;
    case ElectricBottom:
        state = QStringLiteral("bottomedge");
        break;
    case ElectricLeft:
        state = QStringLiteral("leftedge");
        break;
    case ElectricRight:
        state = QStringLiteral("rightedge");
        break;
    case ElectricTopLeft:
        state = QStringLiteral("topleftcorner");
        break;
    case ElectricTopRight:
        state = QStringLiteral("toprightcorner");
        break;
    case ElectricBottomLeft:
        state = QStringLiteral("bottomleftcorner");
        break;
    case ElectricBottomRight:
        state = QStringLiteral("bottomrightcorner");
        break;
    default:
        break;
    }

    QString fileName;
    switch (border) {
    case ElectricTop:
    case ElectricBottom:
    case ElectricLeft:
    case ElectricRight:
        fileName = QStringLiteral("EdgeGlow.qml");
        break;
    case ElectricTopLeft:
    case ElectricTopRight:
    case ElectricBottomLeft:
    case ElectricBottomRight:
        fileName = QStringLiteral("CornerGlow.qml");
        break;
    default:
        break;
    }

    auto *glow = new OffscreenQuickScene(nullptr);
    glow->setOpacity(factor);
    glow->setGeometry(geometry);
    glow->setSource(QUrl::fromLocalFile(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kwin/effects/screenedge/qml/%1").arg(fileName))), QVariantMap{{QStringLiteral("state"), state}});

    auto *glowItem = glow->rootItem();
    if (!glowItem) {
        delete glow;
        return nullptr;
    }

    return glow;
}

bool ScreenEdgeEffect::isActive() const
{
    return !m_borders.isEmpty() && !effects->isScreenLocked();
}

} // namespace
