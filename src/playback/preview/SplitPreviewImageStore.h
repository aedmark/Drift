#pragma once

#include <QImage>

// The A/B split preview's active comparison frame (clipboard, reference snapshot,
// FX-bypassed composite, or track comparison), held in memory between the render /
// controller thread and the QML image provider that paints it.
namespace SplitPreviewImageStore {

void setFrame(const QImage &frame);
void clear();

QImage frame();

} // namespace SplitPreviewImageStore
