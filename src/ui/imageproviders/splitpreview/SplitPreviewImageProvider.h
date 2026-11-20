#pragma once

#include <QQuickImageProvider>

// Serves the A/B split preview comparison frame from SplitPreviewImageStore,
// as image://splitpreview/frame?rev=...
class SplitPreviewImageProvider : public QQuickImageProvider
{
public:
    SplitPreviewImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
