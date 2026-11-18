// reidwin.py — Scripts → 🔢 IDs: the ID system and Re-ID. Line by line.
#include "ui/reidwin.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;

namespace reidwin {

static QStringList sorted_lower(QStringList l)   // sorted(…, key=str.lower)
{
    std::stable_sort(l.begin(), l.end(), [](const QString &a, const QString &b) { return py::lower(a) < py::lower(b); });
    return l;
}

ReIdWindow::ReIdWindow(moddata::Mod &mod_, QWidget *parent) : QWidget(parent, Qt::Window), mod(mod_)
{
    setObjectName(u"reidWin"_s);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(u"QWidget#reidWin { background: %1; }"_s.arg(theme::BG));
    setWindowTitle(u"Event ids — ModEditor"_s);
    resize(1180, 760);
    _timer = new QTimer(this);
    _timer->setSingleShot(true);
    _timer->setInterval(150);
    connect(_timer, &QTimer::timeout, this, [this] { _preview(); });
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 10, 14, 10);
    outer->addWidget(label(u"Event ids: the mod's ID system, and Re-ID"_s, u"title"_s));
    outer->addWidget(label(
        u"Pick how this mod numbers its events. Re-ID numbers the events of the files ticked anew by it — every "
        u"call of an event (country_event = …, province_event = …, common/on_actions.txt, war goals, rebels, "
        u"issues) follows, so nothing breaks; the text keys (EVTNAME…) keep their names. The system is written "
        u"into the mod (two files in its own folder, read by the editor and by people, not by the game: the "
        u"checksum stays) and every new event made in the editor gets the next id of its file's block."_s,
        u"sub"_s));
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 6, 8, 0);
    ll->addWidget(_bold(u"The model"_s));
    auto *row = new QHBoxLayout();
    row->setSpacing(0);
    for (const auto &[k, text] : reid::MODELS) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == model);
        const QString kk = k;
        connect(b, &QPushButton::clicked, this, [this, kk] { _set_model(kk); });
        row->addWidget(b);
        seg[k] = b;
        seg_order.emplace_back(k, b);
    }
    ll->addLayout(row);
    model_note = label(QString(), u"sub"_s);
    ll->addWidget(model_note);
    row = new QHBoxLayout();
    row->addWidget(label(u"First id"_s, u"sub"_s, false));
    start = new widgets::Stepper(1, 99999999, 10000);
    connect(start, &widgets::Stepper::valueChanged, this, [this](int) { _timer->start(); });
    row->addWidget(start);
    row->addSpacing(16);
    block_lbl = label(u"Ids per file"_s, u"sub"_s, false);
    row->addWidget(block_lbl);
    block = new widgets::Stepper(10, 1000000, 1000);
    connect(block, &widgets::Stepper::valueChanged, this, [this](int) { _timer->start(); });
    row->addWidget(block);
    row->addStretch(1);
    ll->addLayout(row);
    ll->addWidget(_bold(u"Renumber the events of these files"_s));
    files = new QListWidget();
    files->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    files->setWordWrap(true);
    connect(files, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { _timer->start(); });
    ll->addWidget(files, 1);
    row = new QHBoxLayout();
    for (const auto &[text, on] : {std::pair{u"Tick all"_s, true}, std::pair{u"Tick none"_s, false}}) {
        auto *b = new QPushButton(text);
        const bool v = on;
        connect(b, &QPushButton::clicked, this, [this, v] { _tick_all(v); });
        row->addWidget(b);
    }
    row->addStretch(1);
    ll->addLayout(row);
    split->addWidget(left);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    auto *right = new QWidget();
    right->setObjectName(u"sideInner"_s);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(10, 6, 10, 6);
    rl->addWidget(_bold(u"What Re-ID does"_s));
    summary = new QLabel(QString());
    summary->setWordWrap(true);
    summary->setTextFormat(Qt::RichText);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rl->addWidget(summary);
    row = new QHBoxLayout();
    go_btn = new QPushButton(u"Renumber — one step (Ctrl+Z undoes it)"_s);
    go_btn->setObjectName(u"primary"_s);
    connect(go_btn, &QPushButton::clicked, this, [this] { _go(); });
    row->addWidget(go_btn);
    sys_btn = new QPushButton(u"Only write the system down"_s);
    sys_btn->setToolTip(u"Keep every id as it is; write the model into the mod so new events follow it"_s);
    connect(sys_btn, &QPushButton::clicked, this, [this] { _system_only(); });
    row->addWidget(sys_btn);
    row->addStretch(1);
    rl->addLayout(row);
    msg = label(QString(), u"sub"_s);
    rl->addWidget(msg);
    rl->addWidget(_bold(u"The system as it is written down for people (%1)"_s.arg(reid::DOC_FILE)));
    doc_view = new QLabel(QString());
    doc_view->setWordWrap(true);
    doc_view->setTextFormat(Qt::PlainText);
    doc_view->setTextInteractionFlags(Qt::TextSelectableByMouse);
    doc_view->setStyleSheet(u"QLabel { font-family: %1; color: %2; background: %3; padding: 8px; }"_s.arg(
        theme::MONO, theme::INK, theme::PANEL));
    rl->addWidget(doc_view);
    rl->addStretch(1);
    sc->setWidget(right);
    split->addWidget(sc);
    split->setSizes({520, 640});
}

QLabel *ReIdWindow::_bold(const QString &text)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

// ------------------------------------------------------------------ data
reid::System ReIdWindow::system() const
{
    reid::System s;
    s.model = model;
    s.start = start->value();
    s.block = block->value();
    s.files = _known_files;
    return s;
}

QStringList ReIdWindow::picked_files() const
{
    QStringList out;
    for (int i = 0; i < files->count(); ++i)
        if (files->item(i)->checkState() == Qt::Checked)
            out << files->item(i)->data(Qt::UserRole).toString();
    return out;
}

void ReIdWindow::open_for(document::Document *d)
{
    doc = d;
    store = d ? &d->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!store)
        return;
    const std::optional<reid::System> sysd = reid::read_system(store->text(reid::SYSTEM_FILE));
    _known_files = sysd ? sysd->files : std::vector<std::pair<QString, long long>>{};
    if (sysd) {
        model = sysd->model;
        for (const auto &[st, v] : {std::pair{start, sysd->start}, std::pair{block, sysd->block}}) {
            st->blockSignals(true);
            st->setValue(v);
            st->blockSignals(false);
        }
    }
    for (const auto &[k, b] : seg_order)
        b->setChecked(k == model);
    OrderedMap<QString, QStringList> per;
    for (const auto &[i, lst] : reid::defined(mod, *store))
        for (const auto &p : lst)
            per[std::get<0>(p)] << i;
    QStringList rels;
    for (const auto &kv : per)
        rels << kv.first;
    files->blockSignals(true);
    files->clear();
    for (const QString &rel : sorted_lower(rels)) {
        const QStringList &ids = *per.get(rel);
        std::vector<long long> nums;
        for (const QString &i : ids)
            if (py::isdigit(i))
                nums.push_back(py::to_int(i));
        std::sort(nums.begin(), nums.end());
        auto *it = new QListWidgetItem(u"%1 — %2 event%3%4"_s.arg(
            rel, QString::number(ids.size()), ids.size() == 1 ? QString() : u"s"_s,
            !nums.empty() ? u", %1 – %2"_s.arg(nums.front()).arg(nums.back()) : QString()));
        it->setData(Qt::UserRole, rel);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
        files->addItem(it);
    }
    files->blockSignals(false);
    msg->setText(sysd ? u"This mod has an ID system already (%1)."_s.arg(reid::MODELS.value(sysd->model))
                      : u"This mod has no ID system yet: pick one, then Renumber or only write it down."_s);
    _preview();
}

void ReIdWindow::_set_model(const QString &k)
{
    model = k;
    for (const auto &[kk, b] : seg_order)
        b->setChecked(kk == k);
    _timer->start();
}

void ReIdWindow::_tick_all(bool on)
{
    files->blockSignals(true);
    for (int i = 0; i < files->count(); ++i)
        files->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    files->blockSignals(false);
    _preview();
}

// ------------------------------------------------------------------ the preview
void ReIdWindow::_preview()
{
    if (!store)
        return;
    block->setEnabled(model == u"blocks");
    block_lbl->setEnabled(model == u"blocks");
    static const std::map<QString, QString> NOTES = {
        {u"blocks"_s, u"Each file gets a block of ids of its own (the first file from the first id, the next from "
                      u"the first id + the ids per file …). A new file gets the next free block — files and ids "
                      u"stay apart, so a file's events are easy to find by their number."_s},
        {u"running"_s, u"Every event of the mod one after the other from the first id, file by file. No gaps."_s},
        {u"tidy"_s, u"Each file keeps its lowest id and its events are numbered on from there, in their order — gaps "
                    u"closed, the rest as it was."_s}};
    model_note->setText(NOTES.at(model));
    const QStringList fl = picked_files();
    reid::Plan pl = reid::plan(mod, *store, system(), fl);
    auto [texts, cnt] = reid::rewrite(mod, *store, pl.mapping);
    _plan = PlanNow{pl.mapping, pl.sys, texts, cnt};
    const eventtools::Ids calls = reid::calls(mod, *store);
    qsizetype moved_calls = 0;
    for (const auto &kv : pl.mapping)
        if (const auto *c = calls.get(kv.first))
            moved_calls += qsizetype(c->size());
    const qsizetype nfiles = QSet<QString>(fl.begin(), fl.end()).size();
    const qsizetype nmap = pl.mapping.size();
    QStringList lines = {u"<b>%1 event%2 get a new id</b> in %3 file%4 — %5 call%6 follow%7 them (in %8 file%9 in all)."_s
                             .arg(QString::number(nmap), nmap == 1 ? QString() : u"s"_s, QString::number(nfiles),
                                  fl.size() == 1 ? QString() : u"s"_s, QString::number(moved_calls),
                                  moved_calls == 1 ? QString() : u"s"_s, moved_calls == 1 ? u"s"_s : QString(),
                                  QString::number(cnt.files), cnt.files == 1 ? QString() : u"s"_s)};
    const eventtools::Ids dfn = reid::defined(mod, *store);
    OrderedMap<QString, std::vector<std::pair<long long, long long>>> by_file;
    for (const auto &[old, nw] : pl.mapping) {
        const QString rel = std::get<0>((*dfn.get(old))[0]);
        by_file[rel].emplace_back(py::isdigit(old) ? py::to_int(old) : 0, py::to_int(nw));
    }
    QStringList rels;
    for (const auto &kv : by_file)
        rels << kv.first;
    QString rows;
    for (const QString &rel : sorted_lower(rels).mid(0, 60)) {
        const auto &v = *by_file.get(rel);
        long long a0 = v[0].first, a1 = v[0].first, b0 = v[0].second, b1 = v[0].second;
        for (const auto &[a, b] : v) {
            a0 = std::min(a0, a);
            a1 = std::max(a1, a);
            b0 = std::min(b0, b);
            b1 = std::max(b1, b);
        }
        rows += u"<tr><td>%1</td><td style='color:%2'>%3 – %4</td><td>→</td><td style='color:%5'><b>%6 – %7</b></td></tr>"_s
                    .arg(rel, theme::INK_DIM, QString::number(a0), QString::number(a1), theme::GOLD,
                         QString::number(b0), QString::number(b1));
    }
    if (!rows.isEmpty())
        lines << u"<table cellspacing='6'>%1</table>"_s.arg(rows);
    if (by_file.size() > 60)
        lines << u"… and %1 more files"_s.arg(by_file.size() - 60);
    for (const QString &n : pl.notes)
        lines << u"<span style='color:%1'>⚠ %2</span>"_s.arg(theme::WARN, n);
    if (pl.mapping.isEmpty())
        lines << u"<span style='color:%1'>Every id already follows the model — nothing to renumber.</span>"_s.arg(theme::OK);
    summary->setText(lines.join(u"<br>"_s));
    go_btn->setEnabled(!pl.mapping.isEmpty());

    doc_view->setText(reid::document(mod, *store, pl.sys, &_plan->texts));
}

reid::Texts ReIdWindow::_system_texts(const reid::System &sysd, const reid::Texts *after)
{
    reid::Texts out;
    out.insert(reid::SYSTEM_FILE, reid::write_system(sysd));
    out.insert(reid::DOC_FILE, reid::document(mod, *store, sysd, after));
    return out;
}

void ReIdWindow::_go()
{
    if (!_plan || _plan->mapping.isEmpty())
        return;
    const PlanNow p = *_plan;
    reid::Texts changes = p.texts;
    for (const auto &[rel, t] : _system_texts(p.sys, &p.texts))
        changes.insert(rel, t);
    Q_EMIT scriptsEdit(changes, u"Re-ID: %1 events numbered anew (%2), %3 calls followed"_s.arg(
                                    QString::number(p.mapping.size()), py::lower(reid::MODELS.value(p.sys.model)),
                                    QString::number(p.cnt.calls)));
    msg->setText(u"Done — %1 events, %2 calls, %3 files; the system is written into the mod (Apply writes it all, "
                 u"Ctrl+Z takes it back)."_s.arg(QString::number(p.mapping.size()), QString::number(p.cnt.calls),
                                                 QString::number(p.cnt.files)));
    _known_files = p.sys.files;
    QTimer::singleShot(0, this, [this] { open_for(doc); });
}

void ReIdWindow::_system_only()
{
    if (!store)
        return;
    const reid::Plan pl = reid::plan(mod, *store, system(), {});
    Q_EMIT scriptsEdit(_system_texts(pl.sys, nullptr), u"Event id system: %1"_s.arg(reid::MODELS.value(model)));
    _known_files = pl.sys.files;
    msg->setText(u"The system is written into the mod (on Apply); new events follow it."_s);
}

}  // namespace reidwin
