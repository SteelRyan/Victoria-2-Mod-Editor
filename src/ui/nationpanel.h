// nationpanel.py — Nation page: a nation at the scenario's start — names, capital, cultures, religion, government,
// national value, prestige, literacy, consciousness, ruling party, upper house, reforms, technologies and inventions —
// plus new nations made from a template nation. Everything is a normal undoable edit, written on Apply (history into
// the start date's block, see countryhist).
#pragma once
#include "core/countryhist.h"
#include "core/document.h"
#include "core/moddata.h"
#include "ui/dialogs.h"
#include "ui/widgets.h"

#include <QComboBox>
#include <QDialog>
#include <QRectF>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

class QCheckBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;

namespace locwin { class GameLook; }
namespace reformwin { class GameReformView; }

namespace nationextras {
class FlagsBox;
class PartiesBox;
}

namespace nationpanel {

using OptStr = std::optional<QString>;
using NationFn = std::function<void(countryhist::NationState &)>;
using Rgb = std::tuple<int, int, int>;

struct Stats {                                    // (provinces, states, people, [(culture, %)] top 3, capital name)
    long long n_prov = 0, n_states = 0, pops = 0;
    std::vector<std::pair<QString, long long>> cultures;
    QString capname;
};

QString pretty(const OptStr &key);
std::vector<dialogs::PickItem> nation_items(const moddata::Mod &mod);
std::vector<long long> stats_owned(document::Document &doc, const moddata::Mod &mod, const QString &tag);

class GovCombo : public QComboBox {
    // Closed: just the government form. Open: every form with the nation's name under it.
public:
    using QComboBox::QComboBox;
    static constexpr int SHORT = Qt::UserRole + 1;
    void paintEvent(QPaintEvent *e) override;
    void showPopup() override;
};

class TechTree : public QWidget {
    // The game's technology screen in mini: a tab per folder with a progress bar, and the chosen folder's lines as
    // columns. Green = the nation has it, yellow = the next one it can research, grey = not yet. Click a technology to
    // give it (and the ones above it); click the last green one to take it away. Hover: full name and year.
    Q_OBJECT
public:
    explicit TechTree(QWidget *parent = nullptr);
    static const QColor GREEN, YELLOW, GREY;
    static constexpr int TAB_H = 40, HEAD_H = 20, CELL_H = 34, GAP = 4;
    const countryhist::NationData *data = nullptr;
    const OrderedMap<QString, QString> *loc = nullptr;
    QSet<QString> owned;
    int folder = 0;
    bool enabled_edit = true;
    std::vector<std::pair<QRectF, QString>> _cells;
    std::vector<std::pair<QRectF, int>> _tabs;
    void set_data(const countryhist::NationData *data, const OrderedMap<QString, QString> *loc);
    void set_owned(const QSet<QString> &owned);
    int _rows() const;
    int _height() const;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    QString name(const QString &key) const;
Q_SIGNALS:
    void clicked(const QString &tech);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    std::optional<std::pair<QString, QString>> _hit(const QPointF &pos) const;   // ("tab", i) / ("tech", t)
};

class NewNationDialog : public QDialog {
    // Tag, name, adjective and colour of a new nation (a copy of the template nation).
    Q_OBJECT
public:
    NewNationDialog(moddata::Mod &mod, const QString &tmpl, countryhist::NewNations &nn, QWidget *parent = nullptr);
    moddata::Mod &mod;
    countryhist::NewNations &nn;
    Rgb color;
    QLineEdit *tag, *name, *adj;
    QPushButton *col_btn;
    QLabel *err;
    void _show_color();
    void _pick_color();
    void _ok();
    std::tuple<QString, QString, QString, Rgb> result_data() const;
};

class NationPanel : public QWidget {
    Q_OBJECT
public:
    explicit NationPanel(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    countryhist::NationData *data = nullptr;
    OptStr tag;
    bool _building = false;
    OrderedMap<QString, QString> _pending;
    QTimer *_timer;
    bool _built = false;
    std::optional<std::optional<QStringList>> _lists_sig;   // (outer None: never looked)
    long long _cult_seen = 0;
    std::optional<std::vector<std::pair<QString, QString>>> _psig;
    std::map<QString, OptStr> _gov_view;          // tag -> the government form shown in the name row
    std::vector<std::pair<QString, OptStr>> _name_keys;   // name / adj / gov -> localisation key
    OptStr _uh_drag;
    OrderedMap<QString, double> _uh_base;

    dialogs::PickCombo *picker, *capital, *primary, *add_culture, *religion;
    QLabel *title, *sub, *note, *name_where, *start_title, *nv_pic, *school_note, *uh_total, *tech_title;
    QPushButton *new_btn, *cap_pick;
    std::vector<std::pair<QString, std::pair<QLineEdit *, QLabel *>>> name_edits;
    GovCombo *gov_pick;
    widgets::Section *start_section, *reform_section, *tech_section;
    // the unciv reforms this nation has at the start (C++ only, the user 2026-11-18: "who has it, who has what, and
    // what is it are two different things" — what a reform does is Politics → Unciv reforms): the game's unciv reform
    // window, a click on an option gives it to the nation; a list under it as well
    widgets::Section *unciv_section;
    QLabel *unciv_title, *unciv_note;
    reformwin::GameReformView *unciv_view;
    QPushButton *unciv_list_btn;
    QWidget *unciv_list;
    QGridLayout *unciv_grid;
    std::vector<std::pair<QString, QComboBox *>> unciv_boxes;
    std::unique_ptr<locwin::GameLook> _look;
    QString _unciv_sig;
    void _fill_unciv(const countryhist::NationState &st);
    QWidget *accepted_host;
    widgets::WrapRow *accepted_row;
    QComboBox *government, *nv, *school, *party;
    QLineEdit *election;
    QCheckBox *civilized;
    std::vector<std::pair<QString, widgets::SliderField *>> sliders;
    std::vector<std::pair<QString, widgets::SliderField *>> uh;
    QGridLayout *reform_grid;
    ~NationPanel() override;
    std::vector<std::pair<QString, QComboBox *>> reform_boxes;
    TechTree *tree;
    nationextras::PartiesBox *parties_box;
    nationextras::FlagsBox *flags_box;

    void _nv_school();
    void _build();
    void refresh(document::Document *doc, countryhist::NationData *data, const OptStr &tag, const Stats &stats);
    void _fill(const Stats &stats);
    static void _select(QComboBox *combo, const OptStr &key);
    void _emit(const QString &text, const NationFn &fn);
    void set_capital(long long pid);
    void _scalar(const QString &key, const OptStr &value);
    void _later(const QString &key, const QString &value);
    void _uh_changed(const QString &key, double value);
    void _flush();
    void _election_done();
    void _accept(const OptStr &cul);
    void _unaccept(const QString &cul);
    void _reform(const QString &issue, const OptStr &opt);
    void _tech_clicked(const QString &tech);
    void _fill_gov_pick(const QString &tag, const OptStr &gov, const OptStr &shown);
    void _gov_view_changed();
    void _name_done(const QString &k, QLineEdit *ed);
    widgets::SliderField *slider(const QString &key) const;
    widgets::SliderField *uh_slider(const QString &key) const;

Q_SIGNALS:
    void edit(const QString &label, const QString &tag, const nationpanel::NationFn &fn);
    void nameRequested(const QString &key, const QString &text, const QString &section);
    void nationPicked(const QString &tag);        // show this nation (select its provinces)
    void newNationRequested(const QString &tag);  // template tag
    void capitalPickRequested();                  // the next map click picks the capital
    void placeRequested(const QString &kind, const QString &key);   // "nv" / "school", its key
};

}  // namespace nationpanel
