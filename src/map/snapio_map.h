// The map's picture types for the recovery file (core/snapio.h): new / deleted provinces, the bitmaps' changed
// pixels, added pictures and flags (pictures as PNG bytes, as recovery.py pickles a QImage).
#pragma once

namespace snapio {

void register_map();                                       // (once; registers the core's too)

}  // namespace snapio
