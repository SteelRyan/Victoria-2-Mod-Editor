// culturefile.py — Editing common/cultures.txt: new cultures, new culture groups, moving cultures between groups.
// Every change is a text splice; the rest of the file (comments, name lists, formatting) is untouched.
// religionfile.py — the same for common/religion.txt (its functions are here too, under their own namespace).
#pragma once
#include "core/moddata.h"
#include "core/script.h"

#include <QRegularExpression>
#include <QString>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

namespace culturefile {

using Rgb = std::tuple<int, int, int>;
using OptStr = std::optional<QString>;

extern const QRegularExpression KEY_RE;            // ^[a-z][a-z0-9_]*$ — a culture / religion / group key

QString _nl(const QString &text);
std::vector<const script::Node *> _groups(const script::Nodes &parsed);
std::pair<qsizetype, qsizetype> _line_span(const QString &text, qsizetype s, qsizetype e);
QString _insert_in_group(const QString &text, const QString &gid, QString block);
QString _culture_block(const QString &key, const Rgb &color, const OptStr &template_block, const OptStr &template_key,
                       const QString &nl);
// strip a trailing comment from a block's first line, keeping a "\r" at its end (both block makers do this)
QString _strip_first_comment(const QString &b);
// re.sub(pattern, repl, text, count=1), with Python's \s \d (Unicode) — the patterns of these two modules
QString _sub_first(const QString &pattern, const QString &text, const std::function<QString(const QStringList &)> &repl);

struct NewCulture {
    QString key, group;
    Rgb color;
    OptStr name, tmpl;
};
// -> the backup folder, or "written on Apply" (the editor: mod.pending_script)
QString apply_changes(moddata::Mod &mod, const std::vector<std::pair<QString, OptStr>> &new_groups,
                      const std::vector<NewCulture> &new_cultures,
                      const std::vector<std::pair<QString, QString>> &moves, const QString &backup_root);

extern const QString LOC_FILE;
extern const QString OLD_LOC_FILE;
void _write_localisation(moddata::Mod &mod, const std::vector<std::pair<QString, QString>> &entries,
                         const QString &bdir, const QString &filename = LOC_FILE, const OptStr &old = OLD_LOC_FILE);

}  // namespace culturefile

namespace religionfile {

using culturefile::OptStr;
using culturefile::Rgb;
extern const QString LOC_FILE;

QString _color(const Rgb &rgb);
QString _religion_block(const QString &key, const Rgb &rgb, long long icon, const OptStr &template_block,
                        const OptStr &template_key, const QString &nl);
struct NewReligion {
    QString key, group;
    Rgb color;
    OptStr name;
    std::optional<long long> icon;
};
QString apply_changes(moddata::Mod &mod, const std::vector<std::pair<QString, OptStr>> &new_groups,
                      const std::vector<NewReligion> &new_religions,
                      const std::vector<std::pair<QString, QString>> &moves, const QString &backup_root);

}  // namespace religionfile
