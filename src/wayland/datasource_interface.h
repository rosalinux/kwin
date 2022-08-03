/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2020 David Edmundson <davidedmundson@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include "kwin_export.h"

#include "abstract_data_source.h"
#include "datadevicemanager_interface.h"

namespace KWaylandServer
{
class DataSourceInterfacePrivate;

/**
 * @brief Represents the Resource for the wl_data_source interface.
 */
class KWIN_EXPORT DataSourceInterface : public AbstractDataSource
{
    Q_OBJECT
public:
    virtual ~DataSourceInterface();

    void accept(const QString &mimeType) override;
    void requestData(const QString &mimeType, qint32 fd) override;
    void cancel() override;

    QStringList mimeTypes() const override;

    static DataSourceInterface *get(wl_resource *native);

    /**
     * @returns The Drag and Drop actions supported by this DataSourceInterface.
     */
    DataDeviceManagerInterface::DnDActions supportedDragAndDropActions() const override;

    void dropPerformed() override;
    void dndFinished() override;
    void dndAction(DataDeviceManagerInterface::DnDAction action) override;
    void dndCancelled() override;

    wl_resource *resource() const;

    wl_client *client() const override;

    bool isAccepted() const override;
    void setAccepted(bool accepted);

private:
    friend class DataDeviceManagerInterfacePrivate;
    explicit DataSourceInterface(DataDeviceManagerInterface *parent, wl_resource *parentResource);

    std::unique_ptr<DataSourceInterfacePrivate> d;
};

}

Q_DECLARE_METATYPE(KWaylandServer::DataSourceInterface *)
