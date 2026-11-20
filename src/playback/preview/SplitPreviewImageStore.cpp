#include "playback/preview/SplitPreviewImageStore.h"

#include <QMutex>
#include <QMutexLocker>

namespace {
QMutex s_mutex;
QImage s_frame;
}

namespace SplitPreviewImageStore {

void setFrame(const QImage &frame)
{
    QMutexLocker lock(&s_mutex);
    s_frame = frame;
}

void clear()
{
    QMutexLocker lock(&s_mutex);
    s_frame = QImage();
}

QImage frame()
{
    QMutexLocker lock(&s_mutex);
    return s_frame;
}

} // namespace SplitPreviewImageStore
