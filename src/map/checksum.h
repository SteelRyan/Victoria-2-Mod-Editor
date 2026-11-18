// checksum.py — The game's checksum: the four letters in the main menu ("V2 v3.04 (UPPR)"). Players can only play
// together when theirs are the same.
//   predict()   works the letters out exactly as the game does (C++ only, 2026-11-09: checked against the game's
//               own number on vanilla and four mods, tests/map/gamesum_check.cpp).
//               It replaces the Python editor's editor_code(), which was a code of the editor's own.
//   Probe       measures the real letters: starts the game with the mods, minimised as soon as its window shows,
//               reads "Checksum is" from logs/setup.log and ends it again (about a minute).
#pragma once
#include <QString>
#include <QStringList>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QProcess;

namespace checksum {

extern const QString PROBE;                          // an empty mod with a settings folder of its own
extern const QString LETTERS;
extern const QString SETTINGS;
extern const double TIMEOUT;                         // seconds the game may take to say its checksum

// Documents › Paradox Interactive › Victoria II (the game's settings folders), where Windows really keeps it.
QString documents_dir();

// ---- the game's own checksum, the same number the game works out (checked: tests/map/gamesum_check.cpp)
quint32 file_value(const QString &path);            // one file's value (0 when it cannot be read)
quint32 bytes_value(const QByteArray &data);
QString letters(qint32 sum);                         // the four letters of a sum

struct ModIn {                                       // one ticked mod, as its .mod file says
    QString modfile, dir, user_dir;                  // dir: the mod's folder; user_dir: "" when the .mod has none
    QStringList replace;                             // replace_path values, only "\" made "/" (as the game)
    QStringList odd_keys;                            // `replace` / `extend` lines: the editor cannot work those out
};
ModIn mod_in(const QString &game, const QString &modfile);   // modfile: the .mod's full path

struct Prediction {
    bool ok = false;
    QString why;                                     // not ok: why it cannot be worked out
    qint32 steady = 0;                               // every start once the settings folder holds this map's cache
    qint32 next = 0;                                 // the next start on this computer (its settings folder as it is)
    qint32 fresh = 0;                                // a new settings folder (what Probe measures)
    QString next_why;                                // why next differs from steady ("" when the same)
    QString settings;                                // the settings folder looked at
    quint32 map_key = 0;                             // the map cache's key (map/cache/time)
    long long files = 0;                             // files counted
    QStringList notes;                               // what the editor had to assume
};
// mods: the ticked mods, parent first (a submod after the mods it depends on)
Prediction predict(const QString &game, const std::vector<ModIn> &mods, const QString &docs = {});

using Result = std::pair<QString, QString>;          // ("ok", letters) or ("error", why)

class Probe {                                        // one measuring start of the game
public:
    Probe(const QString &game, const QStringList &modfiles, const QStringList &user_dirs, const QString &docs = {},
          const QString &exe = QStringLiteral("v2game.exe"));
    ~Probe();
    static std::optional<std::pair<int, int>> place;   // (x, y) the window goes to before it is minimised (tests)
    static std::function<void(const QString &)> dispose;   // where the probe's leftovers go (tests: a folder of theirs)
    QString game, docs, exe, probe_dir, probe_mod;
    QStringList modfiles, user_dirs, folders;
    double t0 = 0.0;                                  // (monotonic seconds; 0: not started)
    bool minimised = false;
    std::optional<Result> result;
    void start();                                     // raises fs::OSError when the game cannot be started
    std::optional<Result> poll();                     // None while it goes on
    Result finish(const Result &result = {QStringLiteral("error"), QStringLiteral("stopped")});

private:
    std::unique_ptr<QProcess> proc;
};

double monotonic();                                   // time.monotonic()
QString stamp();                                      // now as "%Y-%m-%d %H:%M"

}  // namespace checksum
