// checksum::predict against the game's OWN numbers (C++ only, 2026-11-09): what v2game.exe wrote into
// Documents/…/<settings folder>/logs/setup.log ("Checksum is V2 v3.04 (WKMI)    -232297464") for runs of the game.
// Also the letters rule and the per-file value. Reads the game folders only, writes nothing.
// Usage: med_gamesum <work folder>          (prints PASS / the differences)
#include "../core/nopopup.h"
#include "core/pypath.h"
#include "map/checksum.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <cstdio>

using namespace Qt::StringLiterals;

static int bad = 0;
static void check(const QString &what, bool ok, const QString &got = {})
{
    std::printf("  %s %s%s\n", ok ? "ok  " : "FAIL", qPrintable(what), ok || got.isEmpty() ? "" : qPrintable(u"  (got "_s + got + u")"_s));
    bad += ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // (2026-11-18) --mod <game> <the .mod's full path> [<docs>]: the editor's predictions for one mod, to compare with
    // the game's setup.log ("Checksum is …") after a first start and after later starts
    if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == u"--mod") {
        const QString g = QString::fromLocal8Bit(argv[2]), mf = QString::fromLocal8Bit(argv[3]);
        const QString docs = argc >= 5 ? QString::fromLocal8Bit(argv[4]) : QString();
        const checksum::Prediction p = checksum::predict(g, {checksum::mod_in(g, mf)}, docs);
        if (!p.ok) {
            std::printf("not ok: %s\n", p.why.toUtf8().constData());
            return 1;
        }
        std::printf("fresh %s %d | next %s %d | steady %s %d | files %lld | settings %s | next_why %s\n",
                    checksum::letters(p.fresh).toUtf8().constData(), p.fresh,
                    checksum::letters(p.next).toUtf8().constData(), p.next,
                    checksum::letters(p.steady).toUtf8().constData(), p.steady, p.files,
                    p.settings.toUtf8().constData(), p.next_why.toUtf8().constData());
        return 0;
    }
    if (argc < 2) {
        std::printf("usage: med_gamesum <work folder>\n");
        return 2;
    }
    const QString work = QString::fromLocal8Bit(argv[1]);
    const QString nodocs = pypath::join(work, u"zz no settings folder here"_s);   // (never made)

    // the letters: (sum >> 4i) % 26 with the sign kept, as the game's sar / idiv
    for (const auto &[sum, want] : std::vector<std::pair<qint32, QString>>{
             {-538948172, u"WNVU"_s}, {-226878055, u"BZHL"_s}, {656671097, u"RHNE"_s}, {1888464268, u"COPS"_s},
             {300715324, u"AJPS"_s}, {-563131863, u"VKEW"_s}})
        check(u"letters %1 = %2"_s.arg(sum).arg(want), checksum::letters(sum) == want, checksum::letters(sum));
    // the per-file value: 0x11 alone -> (0x11 << 16) | 0x11; the vanilla map cache key 2D 98 63 5A -> 25297820
    check(u"value of one byte 0x11"_s, checksum::bytes_value(QByteArray(1, '\x11')) == 0x110011u);
    check(u"value of 2D 98 63 5A"_s, checksum::bytes_value(QByteArray::fromHex("2D98635A")) == 25297820u);

    // vanilla, the work-area install: a new settings folder (the game folder's own 1-byte map/cache/time) and a
    // settings folder with the map cache (the zzc_* and 10-27 runs)
    const QString game = pypath::join(work, u"Victoria 2"_s);
    checksum::Prediction p = checksum::predict(game, {}, nodocs);
    check(u"vanilla, new settings folder: -563131863 VKEW"_s, p.ok && p.fresh == -563131863, QString::number(p.fresh));
    check(u"vanilla, with the map cache: -538948172 WNVU"_s, p.ok && p.steady == -538948172, QString::number(p.steady));
    check(u"vanilla map cache key 5A63982D"_s, p.map_key == 0x5A63982Du, QString::number(p.map_key, 16));
    check(u"vanilla: next start = new settings folder (no folder there)"_s, p.next == p.fresh && !p.next_why.isEmpty());

    // the map cut test mod (11-09 03:10, a new settings folder): definitions = "../mod/…" counts 0 in the key
    const QString mods = pypath::join(game, u"mod"_s);
    if (QFileInfo(pypath::join(mods, u"zzw_cs2337_0.mod"_s)).isFile()) {
        p = checksum::predict(game, {checksum::mod_in(game, pypath::join(mods, u"zzw_cs2337_0.mod"_s))}, nodocs);
        check(u"zzw_cs2337_0, new settings folder: 1888464268 COPS"_s, p.ok && p.fresh == 1888464268, QString::number(p.fresh));
        check(u"zzw_cs2337_0 map cache key 68FF23C3 (its settings folder's time file)"_s, p.map_key == 0x68FF23C3u,
              QString::number(p.map_key, 16));
    } else {
        std::printf("  (zzw_cs2337_0 not in the work-area game: skipped)\n");
    }
    // one-file test mods: zzc_war_cb (history/wars) 300715324 AJPS, zzc_nat_gfxculture (common/countries: twice)
    for (const auto &[name, want] : std::vector<std::pair<QString, qint32>>{
             {u"zzc_war_cb"_s, 300715324}, {u"zzc_nat_gfxculture"_s, -585035599}, {u"zzc_pop_type"_s, -563131863}}) {
        const QString mf = pypath::join(mods, name + u".mod"_s);
        if (!QFileInfo(mf).isFile())
            continue;
        p = checksum::predict(game, {checksum::mod_in(game, mf)}, nodocs);
        check(u"%1, new settings folder: %2"_s.arg(name).arg(want), p.ok && p.fresh == want, QString::number(p.fresh));
    }

    // the user's installs (read only): runs with a settings folder holding the map cache
    for (const QString &g : {u"C:/Program Files/Victoria 2"_s, QDir::homePath() + u"/Desktop/Victoria 2"_s}) {
        for (const auto &[mod, want] : std::vector<std::pair<QString, qint32>>{
                 {u"WICLBHSQP 1.0.mod"_s, -232297464}, {u"ZRM's Legacy.mod"_s, 656671097}}) {
            const QString mf = pypath::join(g, {u"mod"_s, mod});
            if (!QFileInfo(mf).isFile())
                continue;
            p = checksum::predict(g, {checksum::mod_in(g, mf)}, nodocs);
            check(u"%1 in %2, with the map cache: %3"_s.arg(mod, g).arg(want), p.ok && p.steady == want,
                  QString::number(p.steady));
        }
    }
    // the mod + its 4K submod (11-09 16:29 run: the same WKMI); the settings folder "WICLBHSQP 1.0_WICLBHSQP 1.0 4K"
    // in the real Documents is found and its map cache read (read only)
    {
        const QString g = u"C:/Program Files/Victoria 2"_s;
        const QString a = pypath::join(g, {u"mod"_s, u"WICLBHSQP 1.0.mod"_s}), b = pypath::join(g, {u"mod"_s, u"WICLBHSQP 1.0 4K.mod"_s});
        if (QFileInfo(a).isFile() && QFileInfo(b).isFile()) {
            p = checksum::predict(g, {checksum::mod_in(g, a), checksum::mod_in(g, b)});
            check(u"WICLBHSQP 1.0 + 4K: the next start here = -232297464 (its settings folder's cache)"_s,
                  p.ok && p.next == -232297464 && p.steady == -232297464, QString::number(p.next));
            check(u"… settings folder WICLBHSQP 1.0_WICLBHSQP 1.0 4K"_s, p.settings.endsWith(u"WICLBHSQP 1.0_WICLBHSQP 1.0 4K"_s),
                  p.settings);
        }
    }
    // a .mod with `replace = …`: not promised
    checksum::ModIn odd;
    odd.modfile = u"x.mod"_s;
    odd.odd_keys << u"replace = \"common\""_s;
    p = checksum::predict(game, {odd}, nodocs);
    check(u"`replace =` in a .mod: not worked out"_s, !p.ok && !p.why.isEmpty());

    std::printf("gamesum: %s\n", bad ? qPrintable(QString::number(bad) + u" FAILED"_s) : "PASS");
    return bad ? 1 : 0;
}
