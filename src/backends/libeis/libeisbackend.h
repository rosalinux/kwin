/*
    SPDX-FileCopyrightText: 2021 David Redondo <kde@david-redondo>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "inputbackend.h"

extern "C" {
struct eis;
struct eis_seat;
}

namespace KWin
{
class AbstractOutput;
namespace Libeis
{
class Device;
}

class LibeisBackend : public InputBackend
{
    Q_OBJECT
public:
    explicit LibeisBackend(QObject *parent = nullptr);
    ~LibeisBackend() override;
    void initialize() override;

private:
    void handleEvents();
    void addDevice (eis_seat *seat, AbstractOutput *output);
    eis *m_eis = nullptr;
    QMap<eis_seat*, QVector<Libeis::Device*>> m_seatToDevices;
};

}
