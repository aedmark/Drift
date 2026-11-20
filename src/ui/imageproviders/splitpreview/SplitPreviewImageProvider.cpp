#include "ui/imageproviders/splitpreview/SplitPreviewImageProvider.h"

#include "playback/preview/SplitPreviewImageStore.h"

SplitPreviewImageProvider::SplitPreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage SplitPreviewImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id); // query query param (e.g. ?rev=N) defeats QML URL caching

    QImage image = SplitPreviewImageStore::frame();
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }

    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0)
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (size)
        *size = image.size();
    return image;
}
