// stockpilepage.py — Game rules → Stockpile slider: the trade window's "Maximum stockpile" slider
// (interface/country_trade.gui, scrollbarType sell_slider) set by the amount its right end should give — not by the
// slider's own numbers.
//
// MEASURED 2026-10-26 (Ferrum Mare, the work-area game copy, five runs, the number under the slider read off): the
// amount at slider value s is   2000 × e^(0.0038013 × (s − 2000))   — s runs from minValue (0) to maxValue:
//   maxValue 1500 → the slider does not move at all (its end, ≈ 299, lies under the 300.46 it starts on)
//   maxValue 2000 → right end 2,000.00 (the game's own)       maxValue 2500 → 13,382.17
//   maxValue 3000 → 89,508.80                                  maxValue 5000 → about 77 million (dragged near the end)
//   maxValue 20000 → the number overflows
// Each arrow click moves s by stepSize: the amount × e^(0.0038013 × stepSize) (stepSize 2: + 0.76 %).
// So a maximum M needs  maxValue = 2000 + ln(M / 2000) / 0.0038013  (one million: 3635; a billion: 5452 + 2000).
// Interface files do not count for the checksum (measured): a player can use another maximum than the others.
#pragma once
#include <QRectF>
#include <QString>
#include <QWidget>
#include <optional>
#include <utility>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
namespace document { class Document; }
namespace moddata { class Mod; }
namespace scriptstore { class ScriptStore; }

namespace stockpilepage {

extern const QString REL;
constexpr double K = 0.0038013;                  // measured: the amount grows by e^K per unit of the slider
constexpr double BASE = 2000.0;                  // the amount at slider value 2000
constexpr double LIMIT = 1e12;                   // the highest maximum offered (measured up to ~77 million)
extern const std::vector<std::pair<long long, QString>> PRESETS, STEPS;

double amount(double s);
long long max_value_for(double m);               // the slider's maxValue whose right end gives the amount m
std::optional<std::pair<double, double>> read(const QString &text);   // (maxValue, stepSize) of the slider, or None
QString write(const QString &text, long long max_value, long long step);
double clicks_to_double(double step);
QString short_(double v);                        // (Python: short) an amount in short words: 1, 940, 12,400, 3.2 million

class GameSlider : public QWidget {
    // The trade window's slider as the game draws it — "Maximum stockpile:", the arrows, the track, the knob and the
    // number box. Drag the knob: the number is what the game will show there.
    Q_OBJECT
public:
    explicit GameSlider(QWidget *parent = nullptr);
    double max_value = 2000, pos = 1.0, step = 2;
    void set_max(double mv, std::optional<double> step = std::nullopt);
    int _arrow(double x) const;
    QRectF _track() const;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    bool _drag = false;
    void _set_from(double x);
};

class StockpilePage : public QWidget {
    Q_OBJECT
public:
    explicit StockpilePage(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    int step_i = 1;
    double want = 2000.0;
    QLineEdit *edit;
    QLabel *edit_note, *table, *result;
    GameSlider *game;
    std::vector<QPushButton *> step_btn;
    QPushButton *use_btn;
    static QString _step_text(int i);
    void refresh(document::Document *doc);
    void _typed();
    void _set(double v);
    void _set_step(int i);
    void _show(const std::optional<std::pair<double, double>> &now = std::nullopt);
    void _use();
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // (as every page of Game rules)
    void nameEdit(const QString &key, const QString &text);
};

}  // namespace stockpilepage
