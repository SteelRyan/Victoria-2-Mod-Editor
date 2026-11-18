// mismatchbox.py — Drawing: "Where the maps disagree" — the legend of the mismatch colours with how many pixels each
// kind has, a one-click fix per kind (asks first: the first press says what will happen, a second one within 10
// seconds does it), and, on the Provinces layer, a switch that shows them on the province map too (the tester,
// 2026-10-15). (Python reads panel.mismatch() / panel.doc; here they are passed in.) Also mismatch.py's ProvMismatchView,
// here because it is one of the map widget's pixel layers.
#pragma once
#include "ui/mapview.h"
#include "ui/widgets.h"

#include <QString>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QLabel;
class QVBoxLayout;

namespace document {
class Document;
}
namespace mismatch {
class Mismatch;
}

namespace mismatchbox {

extern const std::map<QString, QString> HOW;

// Drawing → Provinces with "Show them on the province map too": the province colours, the mismatches of all four kinds
// in their colours over them.
class ProvMismatchView : public mapview::PixelLayer {
public:
    ProvMismatchView(const mapview::MapData &md, const mapview::ColorTable &lut, mismatch::Mismatch &mm)
        : md(md), lut(lut), mm(mm) {}
    const mapview::MapData &md;
    mapview::ColorTable lut;
    mismatch::Mismatch &mm;
    mapview::RgbImage colors(long long y0, long long y1, long long x0, long long x1) override;
    std::optional<std::vector<Top>> on_top(long long y0, long long y1, long long x0, long long x1) override;
};

class MismatchBox : public widgets::Section {
    Q_OBJECT
public:
    MismatchBox(std::function<mismatch::Mismatch *()> mismatch, document::Document *doc, QWidget *parent = nullptr);
    std::function<mismatch::Mismatch *()> mismatch;
    document::Document *doc;
    QString layer = QStringLiteral("prov");
    std::pair<std::optional<QString>, double> _armed{std::nullopt, 0.0};
    std::optional<std::pair<QString, std::vector<std::pair<QString, long long>>>> _sig;
    QVBoxLayout *rows;
    QCheckBox *show_cb;
    QLabel *how;
    void refresh(const QString &layer);
    void _fix(const QString &kind);

Q_SIGNALS:
    void message(const QString &text);
    void changed();                         // the map should be drawn again (the Provinces-layer switch)
};

}  // namespace mismatchbox
