// popinfluence.py, its window half: the chart (Chart — the cards on both sides of the sample province), the case
// (CasePanel — "what if") and the page (PopInfluencePage, a page of its own on the rail). The chances read as cards,
// the step groups and the card texts: core\popinfluence.
#pragma once
#include "core/ordered_map.h"
#include "core/popcase.h"
#include "core/popfacts.h"
#include "core/popinfluence.h"
#include "core/popplace.h"
#include "core/popsim.h"
#include "core/script.h"
#include "grid/pyset.h"

#include <QHash>
#include <QRectF>
#include <QSet>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPainter;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTimer;

namespace document { class Document; }
namespace moddata { class Mod; }
namespace mapview { class MapData; }
namespace regionimport { class SourceMap; }
namespace scriptstore { class ScriptStore; }
namespace widgets {
class Section;
class WrapRow;
}

namespace popinfluence {

using Rgb = std::tuple<int, int, int>;
extern const QString GREEN, RED, GREY, AMBER;

// the headline number of the chart: the sums of the cards that hold, "how" ("sum" / "product" / "per": one row a
// source pop, "per"), and what holds it down ("note")
struct Net {
    double pos = 0.0, neg = 0.0, net = 0.0;
    QString how, note;
    std::vector<std::pair<QString, Net>> per;
};
struct SampleRow {
    QString name;
    Rgb rgb;
    double before = 0.0, after = 0.0;
};
struct Sample {                                      // the sample province in the middle card
    std::vector<SampleRow> rows;
    QString head, headline;
    std::optional<QString> mark;                     // the picked pop's row (its name)
    std::vector<std::pair<QString, QString>> notes;  // (text, kind: "" / "warn")
};
struct FocusTier {
    QString key, name;
    double value = 0.0;
};
struct FocusCard {                                   // the national focus on the picked pop
    QString name;
    std::vector<FocusTier> tiers;
    std::optional<long long> pick;
    std::optional<double> units;
};
struct Focus {                                       // a national focus on a pop type: (its key, pop type, value)
    QString key, pop;
    double value = 0.0;
    bool operator==(const Focus &o) const { return key == o.key && pop == o.pop && value == o.value; }
};

class Chart : public QWidget {
    Q_OBJECT
public:
    explicit Chart(QWidget *parent = nullptr);
    bool scenario = true;
    QSet<long long> ticked;                          // the cards (numbers) that hold in the case
    QString result;                                  // what the middle card says the chance comes to
    std::optional<Net> net;
    std::optional<Sample> sample;
    std::function<QString(const Card &)> rate, warn;
    QString title, sub, base;
    std::vector<Card> mods;
    std::optional<long long> sel;
    std::function<QString(const QString &)> names, key_name;
    std::function<QString(const QString &, double)> fmt;
    struct Box {
        QRectF r;
        long long i;
        int col;
    };
    struct LadBox {
        QRectF r;
        long long n;
        int col;
        std::vector<std::pair<QRectF, long long>> rows;
    };
    std::vector<Box> _boxes;
    std::vector<Ladder> _lads;
    std::vector<LadBox> _lad_boxes;
    std::set<long long> open;
    std::optional<FocusCard> focus_card;
    std::optional<QRectF> _focus_rect;
    std::vector<std::pair<QRectF, long long>> _focus_rows;
    bool focus_open = false;
    QHash<QString, std::pair<double, double>> _ty;
    std::vector<std::pair<double, double>> _ty_notes;
    QRectF _mid;

    void set_data(const QString &title, const QString &sub, const QString &base, const std::vector<Card> &mods,
                  std::optional<long long> sel);
    std::tuple<std::vector<long long>, std::vector<long long>, std::vector<long long>> sides() const;
    QFont _font(int px, bool bold = false) const;
    int _th(const QString &text, int px, bool bold = false) const;
    std::optional<std::tuple<QString, QString, QString>> net_lines() const;
    void _layout();
    QSize sizeHint() const override;
    void _box(QPainter &p, bool on, const QRectF &rect);
    void _paint_focus(QPainter &p, const QFont &f, const QFont &fb, const QFont &big, const QFontMetrics &fm);
    std::pair<std::vector<long long>, std::optional<long long>> lad_state(long long n) const;
    QString lad_title(const Ladder &L) const;
    QString lad_row(const Ladder &L, long long k, long long ci) const;
    void _paint_ladders(QPainter &p, const QFont &f, const QFont &fb, const QFont &big, const QFontMetrics &fm);
    void _paint_sample(QPainter &p, const QRectF &mid, const QFont &f, const QFont &fb);
    QRectF _tick(const QRectF &r) const;
    std::optional<long long> _at(const QPointF &pos) const;
    QString tip_at(const QPointF &pos) const;

Q_SIGNALS:
    void picked(long long i);                        // a modifier's number (-1: the middle card)
    void toggled(long long i);                       // a card's tick box: the case is to be set so that it holds / not
    void ladderPicked(const std::vector<long long> &cards, const std::vector<long long> &hold);
    void focusPicked(long long j);                   // the national focus card: the tier picked (-1: none)

protected:
    void resizeEvent(QResizeEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    bool event(QEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
};

// ---------------------------------------------------------------------------------------------- the case
class CasePanel : public QWidget {
    Q_OBJECT
public:
    CasePanel(popcase::Case &c, std::function<QString(const QString &)> words,
              std::function<QString(const QString &)> name, QWidget *parent = nullptr);
    popcase::Case &cs;
    std::function<QString(const QString &)> words, name;
    std::function<QString(const QString &)> fact_note;
    std::optional<std::tuple<QStringList, QStringList, QStringList, QStringList>> _sig;
    OrderedMap<QString, std::pair<QSlider *, QLabel *>> sliders;
    OrderedMap<QString, QPushButton *> fact_btn;
    std::vector<Focus> _tiers;                       // (the focus box's items: their place in this list)
    QLabel *nv_lbl, *focus_lbl, *facts_lbl;
    QComboBox *nv, *focus;
    QWidget *grid_host, *facts_host;
    QGridLayout *grid;
    widgets::WrapRow *facts_row;
    QPushButton *more_btn, *reset_btn;

    void _reset();
    void _nv_picked(int i);
    void _slid(const QString &key, int pos);
    void show_focuses(const std::vector<Focus> &tiers, const std::optional<Focus> &picked, const QString &pop_name,
                      bool every = false);
    void show_for(const QSet<QString> &used, const QSet<QString> &also, const QSet<QString> &facts,
                  const QStringList &nvs);
Q_SIGNALS:
    void changed();
    void focusPicked(const std::optional<Focus> &f);   // the national focus picked here (or none)
};

// ---------------------------------------------------------------------------------------------- the page
class PopInfluencePage : public QWidget {
    Q_OBJECT
public:
    explicit PopInfluencePage(moddata::Mod &mod, QWidget *parent = nullptr);
    ~PopInfluencePage() override;
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    std::optional<QString> pop, head;
    QString target;
    std::optional<long long> sel;
    QString dir = QStringLiteral("in");
    std::optional<QString> source;
    QString view_class = QStringLiteral("poor");
    std::vector<Chance> chances;
    // (C++ only, 2026-11-10: the files are read again on every visit of the page — parsed again only when a text changed;
    //  the same results, ~60 ms less per visit on Ferrum Mare)
    std::optional<std::pair<QStringList, std::vector<std::optional<QString>>>> _chances_key;
    mutable std::map<QString, std::pair<QString, std::shared_ptr<const script::Nodes>>> _parse_memo;
    std::shared_ptr<const script::Nodes> _parsed(const QString &rel) const;   // store->text(rel) parsed (raises as parse)
    bool _armed = false;
    long long size = 100000;
    popcase::Case cs;
    long long months = 120;
    std::optional<Focus> focus;
    std::vector<Focus> focuses;
    std::map<long long, QPushButton *> time_btn;
    popfacts::Defs defs;
    std::map<std::pair<QString, QString>, std::unique_ptr<popcase::Block>> _blocks;   // (rel, path joined)
    struct Merged {
        std::vector<const Chance *> key;
        Chance ch;
        std::vector<std::vector<std::pair<const Chance *, size_t>>> at;
    };
    std::optional<Merged> _merged_memo;
    std::optional<std::pair<const document::Document *, QSet<QString>>> _flags;
    std::optional<double> sample_now_limit;
    QStringList _tset;
    QHash<QString, std::pair<Rgb, QString>> _info;
    OrderedMap<QString, long long> _start;
    QStringList nvs;
    QString src = QStringLiteral("made");
    std::unique_ptr<popplace::Place> place;
    mapview::MapData *md = nullptr;
    std::map<popplace::Group, popcase::Case> _gcases;
    QTimer *_timer;

    // the block in view (Python's ch: several source pops' blocks merged into one, or one of the files' own)
    struct View {
        const Chance *ch = nullptr;
        bool merged = false;
        std::vector<const Chance *> blocks;
        const std::vector<std::vector<std::pair<const Chance *, size_t>>> *at = nullptr;
    };

    QListWidget *pops;
    widgets::WrapRow *heads;
    OrderedMap<QString, QPushButton *> head_btn;
    std::map<QString, QPushButton *> dir_btn, class_btn, src_btn;
    QLabel *target_lbl, *place_lbl, *d_title, *d_file, *result, *fact, *c_head, *c_where, *base_lbl, *msg;
    QComboBox *targets, *focus_box, *quick;
    regionimport::SourceMap *pmap;
    Chart *chart;
    CasePanel *case_panel;
    QWidget *size_row, *share_row;
    QLineEdit *size_edit, *share_edit, *f_edit, *base_edit, *a_factor;
    widgets::Section *card_sec;
    QPlainTextEdit *c_edit, *a_cond;
    QPushButton *del_btn, *add_btn;

    // data
    QString name(const QString &key) const;
    QString key_name(const QString &key) const;
    QString fmt(const QString &key, double v) const;
    QString words(const QString &cond) const;
    const QSet<QString> &flags_set();
    QString fact_note(const QString &fact);
    QStringList files() const;
    QStringList types() const;
    std::vector<const Chance *> into_blocks(const QString &pop) const;
    const Merged &_merged(const std::vector<const Chance *> &blocks);
    std::optional<View> cur();
    std::vector<std::pair<const Chance *, size_t>> at_of(const View &v, long long i) const;
    popcase::Tree tree(const Card &m) const;
    popcase::Block &block(const Chance &c);
    void refresh(document::Document *doc);
    void _read_focuses();
    std::vector<Focus> tiers() const;
    QString strata_of(const std::optional<View> &v) const;
    popcase::Shares shares0() const;
    // the view
    void _show();
    Net _net(const View &v, const grid::PySet<long long> &held);
    QString _sample_text() const;
    // the sample: made up or a real place
    double _tot() const;
    void _set_source(const QString &k);
    void _paint_place_map();
    std::vector<long long> _place_group(long long pid);
    void _place_picked();
    void _take_place(bool fill);
    void _place_hover(int pid);
    const popcase::Case &_gcase(const popplace::Group &g);
    using NumFn = std::function<QHash<QString, double>(const QString &, const popcase::Shares &,
                                                       const std::optional<popplace::Group> &)>;
    using WgtFn = std::function<popsim::Weights(const QString &, const popcase::Shares &,
                                                const std::optional<popplace::Group> &)>;
    std::pair<NumFn, QHash<QString, popcase::Block *>> _numbers_fn();
    WgtFn _weights_fn();
    Sample _sample(const View &v);
    Sample _sample_place(const View &v, const NumFn &nfn, const QString &when);
    std::vector<std::pair<QString, QString>> _limit(const QString &x, const OrderedMap<QString, double> &after,
                                                    const WgtFn &wfn);
    QHash<QString, std::pair<Rgb, QString>> _type_info(const QStringList &types) const;
    // the numbers
    double _share() const;
    QString _card_rate(const Card &m);
    QString _card_warn(const Card &m);
    void _size_done();
    void _share_done();
    // the case, set by clicks on the cards
    void _card_clicked(long long i);
    void _ladder(const std::vector<long long> &cards, const std::vector<long long> &hold);
    void _focus_tier(long long j);
    void _focus_set(const std::optional<Focus> &f);
    void _focus_picked(int i);
    void _set_months(long long m);
    QStringList _cond_lines(const View &v, long long i) const;
    // picking
    void _set_pop(const QString &pk);
    void _set_head(const QString &h);
    void _set_dir(const QString &k);
    void _set_class(const QString &k);
    void _set_target(const std::optional<QString> &t);
    void _pick(long long i);
    void show_place(const QString &pop, const QString &head, const QString &target = QString(),
                    const QString &way = QStringLiteral("out"), const std::optional<QString> &source = std::nullopt);
    // changes (into the pop files)
    void _say(const QString &text);
    QString _label(const View &v) const;
    std::optional<QString> _number(const QString &s) const;
    void _emit(const QString &label, const std::vector<std::tuple<QString, qsizetype, qsizetype, QString>> &edits);
    void _base_done();
    void _factor_done();
    QString _indent(const QString &text, qsizetype at) const;
    void _cond_done();
    void _remove();
    void _quick(int i);
    void _add();

Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void scriptsEdit(const OrderedMap<QString, QString> &texts, const QString &label);
    void nameEdit(const QString &key, const QString &text);
};

}  // namespace popinfluence
