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
    Libeis::Device *createDevice(eis_seat *seat);
    eis *m_eis = nullptr;
    QMap<eis_seat*, Libeis::Device*> m_seatToDevice;
};

}
