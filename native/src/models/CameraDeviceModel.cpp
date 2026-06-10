#include "CameraDeviceModel.h"

CameraDeviceModel::CameraDeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CameraDeviceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_devices.size();
}

QVariant CameraDeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size()) {
        return {};
    }

    const CameraDevice &device = m_devices.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case DisplayNameRole:
        return device.displayName;
    case DevicePathRole:
        return device.path;
    case DriverNameRole:
        return device.driverName;
    case BusInfoRole:
        return device.busInfo;
    default:
        return {};
    }
}

QHash<int, QByteArray> CameraDeviceModel::roleNames() const
{
    return {
        {DisplayNameRole, QByteArrayLiteral("displayName")},
        {DevicePathRole, QByteArrayLiteral("devicePath")},
        {DriverNameRole, QByteArrayLiteral("driverName")},
        {BusInfoRole, QByteArrayLiteral("busInfo")},
    };
}

void CameraDeviceModel::setDevices(const QList<CameraDevice> &devices)
{
    beginResetModel();
    m_devices = devices;
    endResetModel();
}

const QList<CameraDevice> &CameraDeviceModel::devices() const
{
    return m_devices;
}

CameraDevice CameraDeviceModel::deviceAt(int index) const
{
    if (index < 0 || index >= m_devices.size()) {
        return {};
    }
    return m_devices.at(index);
}
