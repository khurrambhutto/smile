#include "CameraFormatModel.h"

CameraFormatModel::CameraFormatModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CameraFormatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_formats.size();
}

QVariant CameraFormatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_formats.size()) {
        return {};
    }

    const CameraFormat &format = m_formats.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case DescriptionRole:
        return format.description();
    case PixelFormatRole:
        return format.pixelFormatName;
    case WidthRole:
        return format.width;
    case HeightRole:
        return format.height;
    case FramesPerSecondRole:
        return format.framesPerSecond;
    default:
        return {};
    }
}

QHash<int, QByteArray> CameraFormatModel::roleNames() const
{
    return {
        {DescriptionRole, QByteArrayLiteral("description")},
        {PixelFormatRole, QByteArrayLiteral("pixelFormat")},
        {WidthRole, QByteArrayLiteral("width")},
        {HeightRole, QByteArrayLiteral("height")},
        {FramesPerSecondRole, QByteArrayLiteral("framesPerSecond")},
    };
}

void CameraFormatModel::setFormats(const QList<CameraFormat> &formats)
{
    beginResetModel();
    m_formats = formats;
    endResetModel();
}

const QList<CameraFormat> &CameraFormatModel::formats() const
{
    return m_formats;
}

CameraFormat CameraFormatModel::formatAt(int index) const
{
    if (index < 0 || index >= m_formats.size()) {
        return {};
    }
    return m_formats.at(index);
}
