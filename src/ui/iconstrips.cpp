// iconstrips.py, the window half (see iconstrips.h).
#include "ui/iconstrips.h"
#include "core/document.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "map/imgio.h"
#include "ui/colsizes.h"
#include "ui/moreviews.h"
#include "ui/textselect.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <map>

using namespace Qt::StringLiterals;

namespace iconstrips {

std::optional<QSize> frame_size(Strips &strips, const QString &name)
{
    const Sprites sps = strips.sprites();
    const Sprite *sp = sps.get(name);
    const auto img = sp ? strips.image(name, &sps) : std::nullopt;
    if (!sp || !img || img->isNull() || sp->frames < 1)
        return std::nullopt;
    return QSize(static_cast<int>(img->width() / sp->frames), img->height());
}

QLabel *size_sign(const std::optional<QSize> &size, const QString &what)
{
    auto *l = new QLabel(size_sign_text(size, what));
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setObjectName(u"sizeSign"_s);
    l->setStyleSheet(u"QLabel#sizeSign { background: #1b1f26; border: 2px solid %1; border-radius: 6px; padding: 8px 12px; }"_s
                         .arg(theme::GOLD));
    return l;
}

QString size_sign_text(const std::optional<QSize> &size, const QString &what)
{
    return (size ? u"<span style='font-size:20px; font-weight:600; color:%1'>%2 × %3 pixels</span>"
                                u"<br><span style='color:%4'>%5</span>"_s
                                    .arg(theme::GOLD)
                                    .arg(size->width())
                                    .arg(size->height())
                                    .arg(theme::INK_DIM,
                                         what.isEmpty() ? u"the size of every picture of this strip — a picture of "
                                                          u"another size is fitted to it"_s
                                                        : what)
                              : u"<span style='color:%1'>The strip's picture can't be read, so its size is not known."
                                u"</span>"_s.arg(theme::DANGER));
}

QString to_library(const QImage &img, const QString &mod_name, const QString &what)
{
    if (img.isNull())
        return {};
    QString base = u"%1 - %2"_s.arg(mod_name.trimmed(), what.trimmed());
    for (QChar &c : base)
        if (QStringLiteral("<>:\"/\\|?*").contains(c) || c.unicode() < 32)
            c = u'_';
    while (base.endsWith(u'.') || base.endsWith(u' '))   // (Windows drops them from a file name)
        base.chop(1);
    const QString folder = moreviews::library_dir(u"pictures"_s);
    QString p = pypath::join(folder, base + u".png"_s);
    for (int i = 2; QFileInfo::exists(p); ++i)
        p = pypath::join(folder, u"%1 (%2).png"_s.arg(base).arg(i));
    return img.save(p, "PNG") ? p : QString();
}

QString picture_filter()
{
    QStringList pats;
    for (const QString &e : moreviews::PIC_EXT)
        pats << u"*"_s + e;
    return u"Pictures (%1)"_s.arg(pats.join(u' '));
}

std::vector<QPixmap> frames(Strips &strips, const QString &name, int height)
{
    const Sprites sps = strips.sprites();
    const Sprite *sp = sps.get(name);
    const std::optional<QImage> img = strips.image(name);
    if (!sp || !img)
        return {};
    const long long n = sp->frames;
    const int fw = std::max(1, static_cast<int>(img->width() / n));
    std::vector<QPixmap> out;
    for (long long i = 0; i < n; ++i) {
        QImage part = img->copy(static_cast<int>(i) * fw, 0, fw, img->height());
        if (height && height != img->height())
            part = part.scaledToHeight(height, Qt::SmoothTransformation);
        out.push_back(QPixmap::fromImage(part));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- the picker
IconPicker::IconPicker(Strips &strips_, const QString &sprite_, std::optional<long long> value_, QWidget *parent)
    : QPushButton(parent), strips(strips_), sprite(sprite_), value(value_)
{
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(34);
    setStyleSheet(u"QPushButton { padding: 2px 8px; text-align: left; }"_s);
    connect(this, &QPushButton::clicked, this, [this] { _open(); });
    _show();
}

void IconPicker::_show()
{
    const std::vector<QPixmap> fr = frames(strips, sprite, 26);
    const long long nf = static_cast<long long>(fr.size());
    const bool ok = value && 1 <= *value && *value <= nf;
    if (ok) {
        const QPixmap &pm = fr[static_cast<size_t>(*value - 1)];
        setIcon(QIcon(pm));
        setIconSize(pm.size());
    } else {
        setIcon(QIcon());
    }
    const QString vs = value ? QString::number(*value) : u"None"_s;
    setText(u" %1  ▾"_s.arg(value ? QString::number(*value) : u"—"_s));
    setToolTip(ok ? u"Picture %1 of %2 (%3) — click to pick another"_s.arg(vs).arg(nf).arg(sprite)
                  : u"The strip %1 has %2 pictures — %3 is none of them. Click to pick one."_s.arg(sprite).arg(nf).arg(vs));
    if (!ok && !fr.empty())
        setStyleSheet(u"QPushButton { padding: 2px 8px; text-align: left; border: 2px solid #ff786e; }"_s);
}

void IconPicker::_open()
{
    const std::vector<QPixmap> fr = frames(strips, sprite, 32);
    if (fr.empty())
        return;
    auto *pop = new QFrame(this, Qt::Popup);
    pop->setObjectName(u"iconPop"_s);
    pop->setStyleSheet(u"#iconPop { background: %1; border: 2px solid %2; border-radius: 6px; }"_s.arg(theme::PANEL,
                                                                                                      theme::GOLD_DIM));
    auto *v = new QVBoxLayout(pop);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(6);
    std::map<std::optional<long long>, QStringList> users;
    const Use *use = use_of(sprite);
    if (use)
        for (const auto &[k, n] : strips.users(*use))
            users[n] << k;
    const int nf = static_cast<int>(fr.size());
    v->addWidget(widgets::label(
        u"%1 — %2 pictures. Gold number: free (nothing uses it yet)."_s.arg(use ? use->title : sprite).arg(nf), u"sub"_s));
    auto *sc = new QScrollArea(pop);
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setFrameShape(QFrame::NoFrame);
    auto *host = new QWidget(sc);
    auto *g = new QGridLayout(host);
    g->setContentsMargins(0, 0, 4, 0);
    g->setSpacing(4);
    const int cw = std::max(46, fr[0].width() + 14);
    const int cols = std::max(4, std::min(12, 600 / (cw + 4)));
    for (int i = 0; i < nf; ++i) {
        auto *b = new QPushButton(host);
        b->setFixedSize(cw, 62);
        b->setCursor(Qt::PointingHandCursor);
        const auto it = users.find(static_cast<long long>(i + 1));
        const QStringList *used = it != users.end() && !it->second.isEmpty() ? &it->second : nullptr;
        b->setToolTip(u"Picture %1%2"_s.arg(i + 1).arg(
            used ? u" — used by: "_s + used->mid(0, 12).join(u", "_s) + (used->size() > 12 ? u" …"_s : QString())
                 : u" — free"_s));
        b->setStyleSheet(u"QPushButton { padding: 0px; border: 2px solid %1; background: %2; }"
                         u"QPushButton:hover { border-color: %3; }"_s.arg(
                             value && i + 1 == *value ? theme::GOLD : theme::LINE, theme::BG, theme::GOLD));
        auto *bl = new QVBoxLayout(b);
        bl->setContentsMargins(2, 3, 2, 1);
        bl->setSpacing(0);
        auto *pic = new QLabel(b);
        pic->setPixmap(fr[static_cast<size_t>(i)]);
        pic->setAlignment(Qt::AlignCenter);
        pic->setAttribute(Qt::WA_TransparentForMouseEvents);
        bl->addWidget(pic, 1);
        auto *num = new QLabel(QString::number(i + 1), b);
        num->setAlignment(Qt::AlignCenter);
        num->setAttribute(Qt::WA_TransparentForMouseEvents);
        num->setStyleSheet(u"font-weight: 700; color: %1; background: transparent;"_s.arg(used ? theme::INK : theme::GOLD));
        bl->addWidget(num);
        connect(b, &QPushButton::clicked, this, [this, pop, n = i + 1] {
            pop->close();
            Q_EMIT picked(n);
        });
        g->addWidget(b, i / cols, i % cols);
    }
    sc->setWidget(host);
    const int rows = (nf + cols - 1) / cols;
    sc->setFixedSize(cols * (cw + 4) + 14, std::min(430, rows * 66 + 6));
    v->addWidget(sc);
    pop->adjustSize();
    const QPoint pt = mapToGlobal(QPoint(0, height() + 2));
    QScreen *scr = QGuiApplication::screenAt(pt);
    if (!scr)
        scr = QGuiApplication::primaryScreen();
    const QRect geo = scr->availableGeometry();
    const int x = std::min(std::max(geo.left() + 4, pt.x()), geo.right() - pop->width() - 4);
    const int y = pt.y() + pop->height() < geo.bottom() - 4
                      ? pt.y()
                      : std::max(geo.top() + 4, mapToGlobal(QPoint(0, 0)).y() - pop->height() - 2);
    pop->move(x, y);
    pop->show();
    _pop = pop;
}

// ---------------------------------------------------------------------------------------------- the page
_Cell::_Cell(long long number_, const QPixmap &pm, const QStringList *used, QWidget *parent) : QFrame(parent), number(number_)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setObjectName(u"stripCell"_s);
    setFixedSize(std::max(84, pm.width() + 16), pm.height() + 46);
    setCursor(Qt::PointingHandCursor);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(3, 4, 3, 3);
    v->setSpacing(1);
    auto *pic = new QLabel(this);
    pic->setPixmap(pm);
    pic->setAlignment(Qt::AlignCenter);
    v->addWidget(pic, 1);
    auto *num = new QLabel(QString::number(number), this);
    num->setAlignment(Qt::AlignCenter);
    num->setStyleSheet(u"font-weight: 700; font-size: 13px; color: %1;"_s.arg(used ? theme::INK : theme::GOLD));
    v->addWidget(num);
    auto *who = new widgets::ElideLabel(used ? used->join(u", "_s) : u"free"_s, this);
    who->setAlignment(Qt::AlignCenter);
    who->setStyleSheet(u"color: %1; font-size: 10px;"_s.arg(used ? theme::INK_DIM : theme::GOLD_DIM));
    v->addWidget(who);
    setToolTip(u"Picture %1 — %2"_s.arg(number).arg(used ? u"used by: "_s + used->join(u", "_s)
                                                         : u"free: nothing uses it yet"_s));
    mark(false);
}

void _Cell::mark(bool on)
{
    setStyleSheet(u"#stripCell { background: %1; border: 2px solid %2; border-radius: 4px; }"_s.arg(
        theme::BG, on ? theme::GOLD : theme::LINE));
}

void _Cell::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        Q_EMIT clicked(number);
}

StripsPage::StripsPage(moddata::Mod &mod_, ChangeFn change_, QWidget *parent)
    : QWidget(parent), mod(mod_), change(std::move(change_)), cur(USES[0].sprite)
{
    using widgets::label;
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    lay->addWidget(split, 1);
    auto *s = new widgets::Section();
    s->lay->addWidget(label(u"Icon strips"_s, u"title"_s));
    s->lay->addWidget(label(
        u"The game keeps its small pictures in strips: one picture holding many side by side, cut into equal "
        "parts. A line like “icon = 11” — or simply the place in a list — picks one. Pick a strip to see every "
        "picture with its number and what uses it. A new national value, focus, good … beyond the last picture "
        "needs the strip made longer: the picture wider by a whole frame AND the number of pictures in the "
        "interface file raised — the editor does both together, for the small and big versions too."_s,
        u"faint"_s));
    lst = new QTreeWidget();
    lst->setColumnCount(2);
    lst->setHeaderLabels({u"Strip"_s, u"Pictures"_s});
    lst->setUniformRowHeights(true);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    colsizes::make_resizable(lst, u"iconStrips"_s, {std::nullopt, 90}, true);
    connect(lst, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *prev) {
        _picked(it, prev);
    });
    s->lay->addWidget(lst, 1);
    split->addWidget(s);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    auto *right = new QWidget();
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    sc->setWidget(right);
    split->addWidget(sc);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 8);
    split->setSizes({460, 1200});
    s = new widgets::Section();
    title = label(QString(), u"title"_s);
    s->lay->addWidget(title);
    sub = label(QString(), u"faint"_s);
    s->lay->addWidget(sub);
    note = label(QString(), u"sub"_s);
    s->lay->addWidget(note);
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e; font-weight: 600;"_s);
    s->lay->addWidget(warn);
    fix_btn = new QPushButton(QString());
    fix_btn->setObjectName(u"apply"_s);
    connect(fix_btn, &QPushButton::clicked, this, [this] { _fix(); });
    fix_btn->hide();
    s->lay->addWidget(fix_btn, 0, Qt::AlignLeft);
    grid_host = new QWidget();
    grid = new widgets::WrapRow(grid_host, 6);
    s->lay->addWidget(grid_host);
    rl->addWidget(s);
    // --- more pictures
    s = new widgets::Section();
    s->lay->addWidget(_bold(u"Make the strip longer"_s));
    grow_note = label(QString(), u"faint"_s);
    s->lay->addWidget(grow_note);
    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    row->addWidget(label(u"Add"_s, u"sub"_s, false));
    count = new widgets::Stepper(1, 40, 1);
    row->addWidget(count);
    row->addWidget(label(u"picture(s), each"_s, u"sub"_s, false));
    filler = new QComboBox();
    filler->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    filler->addItem(u"a placeholder with its number (drawn by the editor)"_s, u"number"_s);
    filler->addItem(u"a copy of the picture marked above"_s, u"copy"_s);
    filler->addItem(u"a picture from your library folder"_s, u"image"_s);
    connect(filler, &QComboBox::activated, this, [this](int) { _filler_changed(); });
    row->addWidget(filler, 1);
    s->lay->addLayout(row);
    lib_box = new QComboBox();
    lib_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    s->lay->addWidget(lib_box);
    add_btn = new QPushButton(u"＋ Add to the strip"_s);
    add_btn->setObjectName(u"apply"_s);
    connect(add_btn, &QPushButton::clicked, this, [this] { _add(); });
    s->lay->addWidget(add_btn, 0, Qt::AlignLeft);
    rl->addWidget(s);
    // --- a picture out of the strip
    s = new widgets::Section();
    s->lay->addWidget(_bold(u"Take the picture marked above out of the strip"_s));
    s->lay->addWidget(label(
        u"For a picture nothing uses any more. The strip gets shorter by one, the pictures after it move one "
        "place down — and every line that named one of those (“icon = N”) is renumbered with them, so each entry "
        "keeps the picture it had. A picture something still uses is not taken out: the line below says who."_s,
        u"faint"_s));
    del_btn = new QPushButton(u"− Remove picture 1"_s);
    del_btn->setToolTip(u"Asks once more: click twice"_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { _remove(); });
    s->lay->addWidget(del_btn, 0, Qt::AlignLeft);
    del_msg = label(QString(), u"sub"_s);
    del_msg->setStyleSheet(u"color: #ff786e; font-weight: 600;"_s);
    del_msg->hide();
    s->lay->addWidget(del_msg);
    rl->addWidget(s);
    // --- a picture of the user's into a frame
    s = new widgets::Section();
    s->lay->addWidget(_bold(u"Put a picture of yours into the picture marked above"_s));
    s->lay->addWidget(label(
        u"The editor ships no pictures of its own: the placeholder is a framed number it draws itself, anything "
        "else comes from your own library folder (Library → Pictures explains it). The picture is scaled to the "
        "frame; a see-through background stays see-through."_s,
        u"faint"_s));
    row = new QHBoxLayout();
    row->setSpacing(8);
    put_box = new QComboBox();
    put_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row->addWidget(put_box, 1);
    put_btn = new QPushButton(u"Put it into picture 1"_s);
    connect(put_btn, &QPushButton::clicked, this, [this] { _put(); });
    row->addWidget(put_btn);
    auto *again = new QPushButton(u"Look again"_s);
    again->setToolTip(u"Read the library folder again"_s);
    connect(again, &QPushButton::clicked, this, [this] { _fill_library(); });
    row->addWidget(again);
    s->lay->addLayout(row);
    lib_note = label(QString(), u"faint"_s);
    s->lay->addWidget(lib_note);
    msg = label(QString(), u"sub"_s);
    msg->setStyleSheet(u"color: #ff786e;"_s);
    s->lay->addWidget(msg);
    rl->addWidget(s);
    rl->addStretch(1);
    _filler_changed();
}

QLabel *StripsPage::_bold(const QString &text)
{
    QLabel *h = widgets::label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

void StripsPage::refresh(document::Document *doc_)
{
    doc = doc_;
    strips = &strips_for(mod, *doc);
    _fill_list();
    _fill_library();
    _show();
}

std::pair<std::vector<std::pair<QString, long long>>, long long> StripsPage::_problems(const Use &use, const Sprites &sps)
{
    const Sprite *sp = sps.get(use.sprite);
    if (!sp)
        return {{}, 0};
    std::vector<std::pair<QString, long long>> over;
    for (const auto &[k, n] : strips->users(use))
        if (n && *n > sp->frames)
            over.emplace_back(k, *n);
    long long top = 0;
    for (const auto &o : over)
        top = std::max(top, o.second);
    return {over, !over.empty() ? top - sp->frames : 0};
}

void StripsPage::_fill_list()
{
    const Sprites sps = strips->sprites();
    lst->blockSignals(true);
    lst->clear();
    QFont bold(lst->font());
    bold.setBold(true);
    QTreeWidgetItem *pick = nullptr;
    auto head = [&](const QString &text) {
        auto *h = new QTreeWidgetItem(QStringList{text, QString()});
        h->setFont(0, bold);
        h->setForeground(0, QBrush(QColor(theme::GOLD)));
        h->setFlags(h->flags() & ~Qt::ItemIsSelectable);
        lst->addTopLevelItem(h);
        h->setExpanded(true);
        return h;
    };
    QTreeWidgetItem *h = head(u"Picked by the mod's lists"_s);
    QSet<QString> known;
    for (const Use &use : USES) {
        for (const QString &f : strips->family(use.sprite))
            known.insert(f);
        const Sprite *sp = sps.get(use.sprite);
        if (!sp)
            continue;
        const auto over = _problems(use, sps).first;
        auto *it = new QTreeWidgetItem(QStringList{use.title, QString::number(sp->frames)});
        it->setData(0, Qt::UserRole, use.sprite);
        it->setToolTip(0, u"%1 — %2"_s.arg(use.sprite, sp->tex));
        if (!over.empty()) {
            it->setForeground(0, QBrush(QColor(u"#ff786e"_s)));
            it->setText(1, u"%1  ⚠"_s.arg(sp->frames));
        }
        h->addChild(it);
        if (use.sprite == cur)
            pick = it;
    }
    h = head(u"Other strips of the game's windows"_s);
    QStringList names;
    for (const auto &kv : sps)
        names << kv.first;
    std::sort(names.begin(), names.end());
    for (const QString &name : names) {
        const Sprite &sp = *sps.get(name);
        if (sp.frames < 2 || known.contains(name))
            continue;
        auto *it = new QTreeWidgetItem(QStringList{QString(name).replace(u"GFX_"_s, u""_s), QString::number(sp.frames)});
        it->setData(0, Qt::UserRole, name);
        it->setToolTip(0, u"%1 — %2"_s.arg(name, sp.tex));
        it->setForeground(0, QBrush(QColor(theme::INK_DIM)));
        h->addChild(it);
        if (name == cur)
            pick = it;
    }
    h->setExpanded(pick && pick->parent() == h);
    if (pick)
        lst->setCurrentItem(pick);
    lst->blockSignals(false);
}

void StripsPage::_fill_library()
{
    const QString folder = moreviews::library_dir(u"pictures"_s);
    QStringList names;
    if (QFileInfo(folder).isDir())
        for (const QString &f : QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                                       QDir::NoSort)) {
            const QString l = py::lower(f);
            if (std::any_of(moreviews::PIC_EXT.begin(), moreviews::PIC_EXT.end(), [&](const QString &e) { return l.endsWith(e); }))
                names << f;
        }
    std::sort(names.begin(), names.end());
    for (QComboBox *box : {lib_box, put_box}) {
        const QVariant keep = box->currentData();
        box->clear();
        for (const QString &f : names)
            box->addItem(f, pypath::join(folder, f));
        if (names.isEmpty())
            box->addItem(u"(your library folder has no pictures yet)"_s, QVariant());
        const int i = box->findData(keep);
        if (i >= 0)
            box->setCurrentIndex(i);
    }
    lib_note->setText(u"Your library folder: %1"_s.arg(folder));
}

void StripsPage::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    const QString d = it ? it->data(0, Qt::UserRole).toString() : QString();
    if (!d.isEmpty()) {
        cur = d;
        frame = 1;
        msg->setText(QString());
        _show();
    }
}

void StripsPage::_filler_changed() { lib_box->setVisible(filler->currentData().toString() == u"image"); }

void StripsPage::_show()
{
    if (!strips)
        return;
    const Sprites sps = strips->sprites();
    const Sprite *sp = sps.get(cur);
    const Use *use = use_of(cur);
    add_btn->setEnabled(sp != nullptr);
    if (!sp) {
        title->setText(u"Pick a strip"_s);
        return;
    }
    const std::optional<QImage> img = strips->image(cur);
    const QStringList fam = strips->family(cur);
    title->setText(use ? use->title : cur);
    const long long fw = img ? img->width() / sp->frames : 0;
    sub->setText(u"%1 · %2 · %3 · %4 pictures of %5 × %6 · counted in %7 (noOfFrames)%8"_s.arg(
        cur, sp->tex, img ? u"%1 × %2"_s.arg(img->width()).arg(img->height()) : u"picture missing"_s,
        QString::number(sp->frames), QString::number(fw), QString::number(img ? img->height() : 0), sp->gfx,
        fam.size() > 1 ? u" · grows together with: "_s + fam.mid(1).join(u", "_s) : QString()));
    std::map<std::optional<long long>, QStringList> users;
    QStringList warns;
    if (use) {
        for (const auto &[k, n] : strips->users(*use))
            users[n] << k;
        const auto [over, miss] = _problems(*use, sps);
        note->setText((!use->note.isEmpty() ? use->note
                                            : u"A line “%1 = N” picks picture N."_s.arg(!use->key.isEmpty() ? use->key
                                                                                                           : u"icon"_s)) +
                      u"  Edited in: %1."_s.arg(use->where));
        if (!over.empty()) {
            QStringList parts;
            for (size_t i = 0; i < over.size() && i < 10; ++i)
                parts << u"%1 → %2"_s.arg(over[i].first).arg(over[i].second);
            warns << u"%1 of these point past the last picture (%2): %3. In the game they show a wrong or empty picture."_s
                         .arg(over.size())
                         .arg(sp->frames)
                         .arg(parts.join(u", "_s) + (over.size() > 10 ? u" …"_s : QString()));
        }
        fix_btn->setVisible(!over.empty());
        if (!over.empty()) {
            fix_btn->setText(u"Fix: add the %1 missing picture(s) as numbered placeholders"_s.arg(miss));
            _missing = miss;
        }
    } else {
        note->setText(u"A strip of the game's windows — nothing in the mod's lists picks from it by number."_s);
        fix_btn->hide();
    }
    for (qsizetype i = 1; i < fam.size(); ++i) {
        const QString &nm = fam[i];
        if (sps.get(nm)->frames != sp->frames)
            warns << u"%1 has %2 pictures, this one %3 — the game shows the wrong picture where it uses that strip."_s
                         .arg(nm)
                         .arg(sps.get(nm)->frames)
                         .arg(sp->frames);
    }
    if (!img)
        warns << u"The picture %1 isn't in the mod or the game."_s.arg(sp->tex);
    else if (fw * sp->frames != img->width())
        warns << u"The picture is %1 wide — not a whole number of frames for %2 pictures."_s.arg(img->width()).arg(sp->frames);
    warn->setText(warns.join(u'\n'));
    warn->setVisible(!warns.isEmpty());
    note->setVisible(true);
    frame = std::max<long long>(1, std::min(frame, sp->frames));
    const int h = img ? img->height() : 32;
    const int show_h = h <= 40 ? h * 2 : h <= 96 ? h : 96;
    std::vector<std::pair<long long, QStringList>> used_sig;
    for (const auto &[k, v] : users)
        if (k)
            used_sig.emplace_back(*k, v);
    const auto sig = std::make_tuple(cur, img ? img->cacheKey() : qint64(0), sp->frames, used_sig);
    if (sig != _sig) {
        _sig = sig;
        while (grid->count()) {
            QLayoutItem *li = grid->takeAt(0);
            if (QWidget *w = li ? li->widget() : nullptr) {
                w->hide();
                w->setParent(nullptr);
                w->deleteLater();
            }
            delete li;
        }
        cells.clear();
        const std::vector<QPixmap> pms = frames(*strips, cur, show_h);
        for (size_t i = 0; i < pms.size(); ++i) {
            const auto u = users.find(static_cast<long long>(i) + 1);
            auto *c = new _Cell(static_cast<long long>(i) + 1, pms[i], u != users.end() ? &u->second : nullptr, grid_host);
            connect(c, &_Cell::clicked, this, [this](long long n) { _mark(n); });
            grid->addWidget(c);
            c->show();
            cells.push_back(c);
        }
        grid_host->updateGeometry();
    }
    for (_Cell *c : cells)
        c->mark(c->number == frame);
    put_btn->setText(u"Put it into picture %1"_s.arg(frame));
    _disarm();
    grow_note->setText(
        u"Each added picture makes %1 %2 wider%3, and noOfFrames in %4 goes from %5 to %6 + the number added. The new "
        "pictures get the next numbers (%7 …) — then set “%8” of your new entry to that number%9."_s.arg(
            sp->tex.mid(sp->tex.lastIndexOf(u'/') + 1), QString::number(fw),
            fam.size() > 1 ? u" (and the same for %1)"_s.arg(fam.mid(1).join(u", "_s)) : QString(), sp->gfx,
            QString::number(sp->frames), QString::number(sp->frames), QString::number(sp->frames + 1),
            use && !use->key.isEmpty() ? use->key : u"icon"_s,
            use && use->order ? u" (a list picked by its order needs nothing more)"_s : QString()));
}

void StripsPage::_mark(long long n)
{
    frame = n;
    for (_Cell *c : cells)
        c->mark(c->number == n);
    put_btn->setText(u"Put it into picture %1"_s.arg(n));
    _disarm();
    del_msg->hide();
}

void StripsPage::_disarm()
{
    _del_armed.reset();
    del_btn->setText(u"− Remove picture %1"_s.arg(frame));
    del_btn->setStyleSheet(QString());
}

void StripsPage::_remove()
{
    // The marked picture out of the strip — on the second click.
    auto got = strips->remove(cur, frame);
    if (const QString *why = std::get_if<QString>(&got)) {
        _disarm();
        del_msg->setText(*why);
        del_msg->show();
        return;
    }
    const auto &[files, texts, moved] = std::get<0>(got);
    del_msg->hide();
    if (_del_armed != std::optional<std::pair<QString, long long>>(std::pair{cur, frame})) {
        _del_armed = std::pair{cur, frame};
        del_btn->setText(u"Click again: picture %1 goes%2"_s.arg(frame).arg(
            moved ? u", %1 line%2 renumbered"_s.arg(moved).arg(moved == 1 ? u""_s : u"s"_s) : QString()));
        del_btn->setStyleSheet(u"QPushButton { color: #ff786e; font-weight: 700; border: 2px solid #ff786e; }"_s);
        return;
    }
    const Use *use = use_of(cur);
    const long long n = frame;
    _del_armed.reset();
    change(u"%1 icons: picture %2 removed%3"_s.arg(use ? use->title : cur, QString::number(n),
                                                   moved ? u" (%1 line%2 renumbered)"_s.arg(moved).arg(
                                                               moved == 1 ? u""_s : u"s"_s)
                                                         : QString()),
           files, texts);
}

std::optional<QImage> StripsPage::_library_image(QComboBox *box)
{
    const QString path = box->currentData().toString();
    std::optional<QImage> img = !path.isEmpty() ? imgio::read_image(path) : std::nullopt;
    if (!img)
        msg->setText(u"Pick a picture of your library folder first (the folder has none, or this one can't be read)."_s);
    return img;
}

void StripsPage::_do_extend(const std::vector<Filler> &fillers)
{
    auto got = strips->extend(cur, fillers);
    if (const QString *why = std::get_if<QString>(&got)) {
        msg->setText(*why);
        return;
    }
    const auto &[files, texts] = std::get<0>(got);
    msg->setText(QString());
    const Use *use = use_of(cur);
    change(u"%1 icons: %2 more picture(s)"_s.arg(use ? use->title : cur).arg(fillers.size()), files, texts);
}

void StripsPage::_add()
{
    const QString how = filler->currentData().toString();
    const long long n = count->value();
    Filler f;
    if (how == u"image") {
        const auto img = _library_image(lib_box);
        if (!img)
            return;
        f.kind = u"image"_s;
        f.img = *img;
    } else if (how == u"copy") {
        f.kind = u"copy"_s;
        f.frame = frame;
    } else {
        f.kind = u"number"_s;
    }
    _do_extend(std::vector<Filler>(static_cast<size_t>(std::max<long long>(0, n)), f));
}

void StripsPage::_fix()
{
    Filler f;
    f.kind = u"number"_s;
    _do_extend(std::vector<Filler>(static_cast<size_t>(std::max<long long>(0, _missing)), f));
}

void StripsPage::_put()
{
    const auto img = _library_image(put_box);
    if (!img)
        return;
    const StripFiles files = strips->put(cur, frame, *img);
    if (!files.size()) {
        msg->setText(u"That picture can't be replaced (the strip's picture can't be read)."_s);
        return;
    }
    msg->setText(QString());
    const Use *use = use_of(cur);
    change(u"%1 icons: picture %2 replaced"_s.arg(use ? use->title : cur).arg(frame), files, {});
}

}  // namespace iconstrips
