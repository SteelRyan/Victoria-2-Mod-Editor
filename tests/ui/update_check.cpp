// update_check: the "new version is out" note (update.h). Versions compared, GitHub asked for real (the answer is
// printed), and the start window's note + tick box driven with a settings file of its own (never the user's).
// Usage: med_update_check <out folder> [--offline]
#include "../core/nopopup.h"
#include "app/app.h"
#include "app/update.h"
#include "app/version.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QSettings>
#include <cstdio>

using namespace Qt::Literals::StringLiterals;

static int fails = 0;
static void expect(bool ok, const char *what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += ok ? 0 : 1;
}

int main(int argc, char *argv[])
{
    QApplication qapp(argc, argv);
    theme::apply(qapp);                                   // (the pictures as the editor draws them)
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : u"."_s;
    QDir().mkpath(out);
    const bool offline = qapp.arguments().contains(u"--offline"_s);

    std::printf("versions\n");
    using update::newer_than;
    using update::parse_version;
    expect(parse_version(u"v1.0.1"_s) == std::array<int, 3>{1, 0, 1}, "v1.0.1");
    expect(parse_version(u"1.2"_s) == std::array<int, 3>{1, 2, 0}, "1.2 = 1.2.0");
    expect(!parse_version(u"v1.0.1-beta"_s), "v1.0.1-beta refused");
    expect(!parse_version(u"<b>hi</b>"_s), "text refused");
    expect(!parse_version(u"1.2.3.4"_s), "four parts refused");
    expect(!parse_version(u"v99999.0.0"_s), "huge number refused");
    expect(newer_than(u"v1.0.2"_s, u"1.0.1"_s) == u"1.0.2"_s, "1.0.2 newer than 1.0.1");
    expect(newer_than(u"v1.1"_s, u"1.0.9"_s) == u"1.1.0"_s, "1.1 newer than 1.0.9");
    expect(!newer_than(u"v1.0.0"_s, u"1.0.1"_s), "1.0.0 (the beta) not newer");
    expect(!newer_than(u"v1.0.1"_s, u"1.0.1"_s), "same not newer");
    expect(!newer_than(u"v1.0.10"_s, u"1.0.10"_s) && newer_than(u"v1.0.10"_s, u"1.0.9"_s), "1.0.10 > 1.0.9 (numbers)");

    if (!offline) {
        std::printf("GitHub\n");
        const auto tag = update::fetch_latest_tag();
        std::printf("  newest release: %s\n", tag ? qUtf8Printable(*tag) : "(no answer)");
        expect(tag.has_value(), "an answer from api.github.com");
    }

    std::printf("start window\n");
    const QString ini = QDir(out).filePath(u"update_settings.ini"_s);
    QFile::remove(ini);
    QSettings settings(ini, QSettings::IniFormat);
    settings.setValue(u"update/last"_s, QDateTime::currentSecsSinceEpoch());   // (asked "today": no request here)
    settings.setValue(u"update/latest"_s, u"v1.0.2"_s);
    update::Checker checker(settings);
    {
        app::OpenDialog dlg(settings, QDir(out).filePath(u"no mods here"_s));
        dlg.show();
        QApplication::processEvents();
        expect(dlg.upd_strip->isVisible(), "a newer version: the note shows");
        std::printf("  note: %s\n", qUtf8Printable(dlg.upd_label->text()));
        expect(dlg.upd_box->isVisible() && dlg.upd_box->isChecked(), "the tick is on by default");
        dlg.grab().save(QDir(out).filePath(u"open_note.png"_s));
        dlg.upd_box->setChecked(false);
        QApplication::processEvents();
        expect(!dlg.upd_strip->isVisible(), "untick: the note goes");
        expect(settings.value(u"update/check"_s).toBool() == false, "untick: saved as off");
        settings.setValue(u"update/last"_s, 0);                      // a day ago: would ask, but it is off
        checker.start();
        expect(!checker.newer(), "off: nothing to show, nothing asked");
        settings.setValue(u"update/last"_s, QDateTime::currentSecsSinceEpoch());   // (so ticking does not ask)
        dlg.upd_box->setChecked(true);
        QApplication::processEvents();
        expect(dlg.upd_strip->isVisible(), "tick again: the note is back");
        checker.hide_this_one();
        QApplication::processEvents();
        expect(!dlg.upd_strip->isVisible(), "✕: this version hidden");
        settings.setValue(u"update/latest"_s, u"v1.0.3"_s);
        dlg.show_update();
        expect(dlg.upd_strip->isVisible(), "an even newer one shows again");
        settings.setValue(u"update/latest"_s, u"v1.0.0"_s);
        dlg.show_update();
        expect(!dlg.upd_strip->isVisible(), "the beta (1.0.0) shows nothing");
        dlg.grab().save(QDir(out).filePath(u"open_plain.png"_s));
    }
    std::printf(fails ? "update: FAIL (%d)\n" : "update: PASS\n", fails);
    std::fflush(stdout);
    return fails ? 1 : 0;
}
