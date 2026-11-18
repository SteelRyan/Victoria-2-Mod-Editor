// update — "a new version is out" (C++ only, the user 2026-11-15). Once a day the editor asks GitHub which release
// of SteelRyan/Victoria-2-Mod-Editor is the newest and compares its tag (v1.0.2) with its own version (version.h).
// It ONLY tells: the start window shows a note with a button that opens the releases page — a fixed address built
// into the program, never one taken from the answer. Nothing is downloaded or run. Safe by design: the worst a
// faked answer can do is show a wrong note. Switched off by the start window's tick ("update/check" = false).
#pragma once
#include <QObject>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <array>
#include <memory>
#include <optional>

namespace update {

inline const QString RELEASES_PAGE = QStringLiteral("https://github.com/SteelRyan/Victoria-2-Mod-Editor/releases/latest");

// "v1.2.3" / "1.2.3" / "v1.2" → {1, 2, 3} / {1, 2, 0}; anything else (letters, more parts, huge numbers) → nothing
std::optional<std::array<int, 3>> parse_version(const QString &tag);
QString text_of(const std::array<int, 3> &v);              // {1, 2, 0} → "1.2.0"
// the newer of the two, as text, when `tag` is newer than `mine`; nothing otherwise (or when either is not a version)
std::optional<QString> newer_than(const QString &tag, const QString &mine);

// the newest release's tag_name from GitHub's API (blocking: run it off the window's thread). WinHTTP over HTTPS,
// the certificate checked by Windows, no redirects followed, 10 s timeouts, the answer cut at 256 KB.
// Nothing on any failure.
std::optional<QString> fetch_latest_tag();

struct Shared;                                         // what the asking thread hands back

// one per program run (made in main, so test programs never ask the internet)
class Checker : public QObject {
    Q_OBJECT
public:
    explicit Checker(QSettings &settings, QObject *parent = nullptr);
    ~Checker() override;
    static Checker *instance();                         // nullptr when none was made (the checks)

    bool enabled() const;
    void set_enabled(bool on);                          // the tick: on → ask now if a day has passed
    std::optional<QString> newer() const;               // the newer version, unless the user hid that one
    void hide_this_one();                               // "✕": no note again until an even newer version is out
    void start();                                       // asks if switched on and a day has passed since the last answer

Q_SIGNALS:
    void changed();

private:
    QSettings &settings;
    std::shared_ptr<Shared> _job;
    QTimer _poll;
    void _collect();
};

}  // namespace update
