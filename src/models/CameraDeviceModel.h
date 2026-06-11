#pragma once

#include "camera/CameraDevice.h"

#include <QAbstractListModel>

class CameraDeviceModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        DisplayNameRole = Qt::UserRole + 1,
        DevicePathRole,
        DriverNameRole,
        BusInfoRole
    };

    explicit CameraDeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDevices(const QList<CameraDevice> &devices);
    const QList<CameraDevice> &devices() const;
    CameraDevice deviceAt(int index) const;

private:
    QList<CameraDevice> m_devices;
};
