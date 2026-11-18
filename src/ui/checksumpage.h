// checksumpage — Tools → Checksum (C++ only, 2026-11-18, the user's wish): the game's checksum of the mod as opened,
// worked out the way the game does it (map/checksum). Two lines: the first start (no settings folder in Documents
// yet) and the checksum itself — every start after the first — written big.
#pragma once

#include <QWidget>

class QLabel;
namespace submodpage { class SubmodWorkspace; }

namespace checksumpage {

class ChecksumWorkspace : public QWidget {
public:
    explicit ChecksumWorkspace(submodpage::SubmodWorkspace &sub, QWidget *parent = nullptr);
    void refresh();                                  // works the letters out again (when the page is shown)

    QLabel *first = nullptr, *big = nullptr, *what = nullptr;

private:
    submodpage::SubmodWorkspace &sub;
};

}  // namespace checksumpage
