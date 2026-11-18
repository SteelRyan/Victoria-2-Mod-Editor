// iconstrips.py, its window half (the data half is map\iconstrips_data): a strip's pictures as pixmaps and the picker
// for `icon = …` fields; the Icon strips page of the Library (StripsPage, _Cell).
#pragma once
#include "map/iconstrips_data.h"

#include <QFrame>
#include <QPixmap>
#include <QPushButton>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

class QComboBox;
class QLabel;
class QSize;
class QTreeWidget;
class QTreeWidgetItem;
namespace widgets {
class Stepper;
class WrapRow;
}

namespace iconstrips {

// Strips.frames(name, height): [QPixmap] of a strip's pictures (scaled to `height` when given)
std::vector<QPixmap> frames(Strips &strips, const QString &name, int height = 0);

// C++ only (the user 2026-11-18: "it always has to tell me it's this many pixels times this many pixels … that's really
// important information"): the size of one picture of a strip, and a big sign that says it
std::optional<QSize> frame_size(Strips &strips, const QString &name);
QLabel *size_sign(const std::optional<QSize> &size, const QString &what = QString());
QString size_sign_text(const std::optional<QSize> &size, const QString &what = QString());
QString picture_filter();
// (C++ only, the user 2026-11-18: "open up a mod and export its pictures into my library") a picture saved into the
// library folder as "<mod> - <what>.png" (a number added when the name is taken) -> its path, empty when it failed
QString to_library(const QImage &img, const QString &mod_name, const QString &what);                   // the file picker's "Pictures (*.png *.dds …)"

class _Cell : public QFrame {
    Q_OBJECT
public:
    _Cell(long long number, const QPixmap &pm, const QStringList *used, QWidget *parent = nullptr);
    long long number;
    void mark(bool on);
Q_SIGNALS:
    void clicked(long long number);

protected:
    void mousePressEvent(QMouseEvent *e) override;
};

// Library → Icon strips. `change(label, files, texts)` queues pictures and .gfx texts as one step.
class StripsPage : public QWidget {
    Q_OBJECT
public:
    using ChangeFn = std::function<void(const QString &, const StripFiles &, const OrderedMap<QString, QString> &)>;
    StripsPage(moddata::Mod &mod, ChangeFn change, QWidget *parent = nullptr);
    moddata::Mod &mod;
    ChangeFn change;
    document::Document *doc = nullptr;
    Strips *strips = nullptr;
    QString cur;
    long long frame = 1;
    std::optional<std::tuple<QString, qint64, long long, std::vector<std::pair<long long, QStringList>>>> _sig;
    std::vector<_Cell *> cells;
    long long _missing = 0;
    std::optional<std::pair<QString, long long>> _del_armed;
    QTreeWidget *lst;
    QLabel *title, *sub, *note, *warn, *grow_note, *del_msg, *lib_note, *msg;
    QPushButton *fix_btn, *add_btn, *del_btn, *put_btn;
    QWidget *grid_host;
    widgets::WrapRow *grid;
    widgets::Stepper *count;
    QComboBox *filler, *lib_box, *put_box;

    QLabel *_bold(const QString &text);
    void refresh(document::Document *doc);
    std::pair<std::vector<std::pair<QString, long long>>, long long> _problems(const Use &use, const Sprites &sps);
    void _fill_list();
    void _fill_library();
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void _filler_changed();
    void _show();
    void _mark(long long n);
    void _disarm();
    void _remove();
    std::optional<QImage> _library_image(QComboBox *box);
    void _do_extend(const std::vector<Filler> &fillers);
    void _add();
    void _fix();
    void _put();
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);
};

class IconPicker : public QPushButton {
    // The picture a number stands for; a click shows every picture of the strip with its number to pick from.
    Q_OBJECT
public:
    IconPicker(Strips &strips, const QString &sprite, std::optional<long long> value, QWidget *parent = nullptr);
    Strips &strips;
    QString sprite;
    std::optional<long long> value;
    QFrame *_pop = nullptr;
    void _show();
    void _open();
Q_SIGNALS:
    void picked(long long n);
};

}  // namespace iconstrips
