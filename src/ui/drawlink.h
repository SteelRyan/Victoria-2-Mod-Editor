// What the Draw panel's boxes (terrainpanel, riverpanel) read from the panel: Python passes the DrawPanel itself and
// they read panel.sel / panel.view / panel.edit_btn, call panel.mismatch() and emit panel.zoomRequested. The
// DrawPanel derives from this; the checks give the boxes a stand-in.
#pragma once
#include <Qt>
#include <vector>

class QPushButton;
namespace mapview {
class MapView;
}
namespace mismatch {
class Mismatch;
}

namespace drawlink {

// The key that makes the wheel change the brush size on the map (C++ only, the user's wish 2026-11-09: Shift + wheel
// = brush size, Ctrl + wheel = zoom like the plain wheel; the Python editor: Ctrl). The checks press it where the
// Python editor's checks press Ctrl.
inline constexpr Qt::KeyboardModifier BRUSH_KEY = Qt::ShiftModifier;

struct Panel {
    virtual ~Panel() = default;
    std::vector<long long> sel, sea_sel;
    mapview::MapView *view = nullptr;
    QPushButton *edit_btn = nullptr;
    virtual mismatch::Mismatch *mismatch() = 0;
    virtual void zoom_requested(double x, double y) = 0;   // (Python: panel.zoomRequested.emit(x, y))
};

}  // namespace drawlink
