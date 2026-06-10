#pragma once

#include "camera/CameraFormat.h"

#include <QAbstractListModel>

class CameraFormatModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        DescriptionRole = Qt::UserRole + 1,
        PixelFormatRole,
        WidthRole,
        HeightRole,
        FramesPerSecondRole
    };

    explicit CameraFormatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setFormats(const QList<CameraFormat> &formats);
    const QList<CameraFormat> &formats() const;
    CameraFormat formatAt(int index) const;

private:
    QList<CameraFormat> m_formats;
};
