// provbuild (C++ only, 2026-11-16): new province buildings on the base game's own files — buildings.txt, the
// technologies, the province window (province_interface.gui) and its background picture.
// Usage: med_provbuild_check <game folder> <scratch folder>   (pictures written there to be looked at)
#include "nopopup.h"
#include "core/provbuild.h"
#include "core/script.h"
#include "map/imgio.h"

#include <QDir>
#include <QGuiApplication>
#include <QRegularExpression>
#include <cstdio>

using namespace Qt::StringLiterals;
namespace pb = provbuild;

static int fails = 0;
static void ok(bool c, const QString &what)
{
    std::printf("%s %s\n", c ? "ok  " : "FAIL", qUtf8Printable(what));
    if (!c)
        ++fails;
}
static QString norm(QString t) { return t.replace(u"\r\n"_s, u"\n"_s); }

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "\\Fonts");   // (offscreen has no fonts of its own)
    QGuiApplication app(argc, argv);
    if (argc < 3)
        return 2;
    const QDir game(QString::fromLocal8Bit(argv[1]));
    const QDir out(QString::fromLocal8Bit(argv[2]));
    QDir().mkpath(out.path());

    // ---- buildings.txt
    const QString b = norm(script::read_text(game.filePath(u"common/buildings.txt"_s)));
    ok(pb::province_keys(b) == QStringList({u"fort"_s, u"naval_base"_s, u"railroad"_s}), u"the game's three"_s);
    const auto fx = pb::effects(b, u"fort"_s);
    ok(fx.size() == 1 && fx[0].key == u"fort_level", u"fort's effect: fort_level"_s);
    const auto rx = pb::effects(b, u"railroad"_s);
    ok(rx.size() == 2 && rx[0].key == u"infrastructure" && rx[1].key == u"movement_cost", u"railroad's effects"_s);
    const QString b2 = pb::add_building(b, u"testbank"_s, u"fort"_s);
    ok(pb::province_keys(b2).size() == 4 && pb::province_keys(b2).last() == u"testbank", u"a 4th, last"_s);
    {
        QStringList got;
        for (const auto &l : pb::lines_of(b2, u"testbank"_s))
            got << l.key + u'=' + (l.block ? u"{}"_s : l.value);
        ok(got.join(u' ') == u"type=testbank goods_cost={} time=1080 visibility=yes onmap=no max_level=6 province=yes",
           u"new one, like the fort: "_s + got.join(u' '));
        ok(pb::effects(b2, u"testbank"_s).empty(), u"new one: no effects"_s);
    }
    {
        QString t = b;
        while (t.endsWith(u'\n') || t.endsWith(u' ') || t.endsWith(u'\t'))
            t.chop(1);
        QString r = pb::remove_building(b2, u"testbank"_s);
        while (r.endsWith(u'\n'))
            r.chop(1);
        ok(r == t, u"taken out again: the file as it was"_s);
    }

    // ---- technologies
    std::vector<std::pair<QString, QString>> files;
    for (const QString &f : QDir(game.filePath(u"technologies"_s)).entryList({u"*.txt"_s}, QDir::Files, QDir::Name))
        files.emplace_back(u"technologies/"_s + f, norm(script::read_text(game.filePath(u"technologies/"_s + f))));
    {
        const auto u = pb::unlocks(files, u"fort"_s);
        long long act = 0, add = 0;
        for (const auto &x : u) {
            act += x.activates;
            add += x.adds;
        }
        ok(act == 1 && add == 5, u"fort: one tech activates, five add a level (%1, %2)"_s.arg(act).arg(add));
        const auto st = pb::level_steps(u, 6);
        // (measured: activate_building gives no level — the fort reaches level 5 by technology)
        ok(st.size() == 6 && st.front().second == u"activates it (no level by itself)" && st[1].second == u"level 1" &&
               st.back().second == u"level 5",
           u"fort: activated, then levels 1 … 5 ("_s + st.back().second + u')');
        ok(pb::unlocks(files, u"naval_base"_s).size() == 6, u"naval base: six techs"_s);
    }
    ok(pb::techs(files).size() > 100, u"every technology read (%1)"_s.arg(pb::techs(files).size()));
    for (auto &[rel, text] : files) {
        if (!text.contains(u"private_banks"))
            continue;
        const QString t2 = pb::set_unlock(text, u"private_banks"_s, u"testbank"_s, true, 2);
        const auto u = pb::unlocks({{rel, t2}}, u"testbank"_s);
        ok(u.size() == 1 && u[0].activates && u[0].adds == 2 && u[0].tech == u"private_banks",
           u"private banks unlocks it, two more levels"_s);
        const auto st = pb::level_steps(u, 3);
        ok(st.size() == 1 && st[0].second == u"activates it, levels 1–2",
           u"…activates it, levels 1–2: "_s + (st.empty() ? QString() : st[0].second));
        ok(pb::set_unlock(t2, u"private_banks"_s, u"testbank"_s, false, 0) == text, u"…and taken off: as it was"_s);
        const QString t3 = pb::set_unlock(t2, u"private_banks"_s, u"testbank"_s, false, 1);
        const auto u3 = pb::unlocks({{rel, t3}}, u"testbank"_s);
        ok(u3.size() == 1 && !u3[0].activates && u3[0].adds == 1, u"…activate off, one level"_s);
    }

    // ---- the province window
    const QString gui = norm(script::read_text(game.filePath(u"interface/province_interface.gui"_s)));
    const pb::Window w = pb::read_window(gui);
    ok(w.found, u"window found "_s + w.why);
    ok(w.icons == QStringList({u"build_icon0"_s, u"build_icon1"_s, u"build_icon2"_s}), u"three picture slots"_s);
    ok(w.fits == 3, u"three rows fit (%1)"_s.arg(w.fits));
    ok(w.view_y == -528 && w.view_h == 490 && w.part_y == 343 && w.part_h == 180 && w.bg_y == -19 &&
           w.bg_sprite == u"GFX_provinceview_bg",
       u"window %1 %2, part %3 %4, background %5 %6"_s.arg(w.view_y).arg(w.view_h).arg(w.part_y).arg(w.part_h)
           .arg(w.bg_y).arg(w.bg_sprite));
    const QString g2 = pb::add_icon(gui, 3, u"GFX_province_testbank_strip"_s);
    const pb::Window w2 = pb::read_window(g2);
    ok(w2.icons.size() == 4 && w2.icons.first() == u"build_icon3", u"build_icon3 first in the row"_s);
    ok(pb::add_icon(g2, 3, u"x"_s) == g2, u"…not twice"_s);
    ok(g2.contains(u"name = \"build_icon3\"\n\t\t\tspriteType = \"GFX_province_testbank_strip\""_s), u"…its lines"_s);
    ok(w.step == 35 && w.room == 105 && w.slot_sprites.size() == 3 && w.slot_sprites[1] == u"GFX_province_navalbase_strip",
       u"rows of %1 px, %2 px of room, slot 1 shows %3"_s.arg(w.step).arg(w.room).arg(w.slot_sprites.value(1)));
    const pb::Source src = pb::source_of(g2);
    ok(!src.noted && src.sprite == u"GFX_provinceview_bg" && src.top == 362 && src.room == 105 && src.step == 35 &&
           src.pref == 35,
       u"the background's rows: from y %1, %2 high, %3 a row"_s.arg(src.top).arg(src.room).arg(src.step));
    {
        const pb::Fit f = pb::plan_fit(pb::read_window(g2), 4, 35);
        ok(f.step == 35 && f.delta == 35 && f.height == 525 && f.fits_768, u"4 rows: "_s + f.words);
        const pb::Fit f6 = pb::plan_fit(pb::read_window(g2), 7, 35);
        ok(f6.step == 35 && f6.fits_768, u"7 rows: "_s + f6.words);
        const pb::Fit f9 = pb::plan_fit(pb::read_window(g2), 8, 35);
        ok(f9.step == 32 && f9.fits_768, u"8 rows: lower rows — "_s + f9.words);
        const pb::Fit f12 = pb::plan_fit(pb::read_window(g2), 10, 35);
        ok(f12.step == pb::MIN_ROW && !f12.fits_768, u"10 rows: too tall even at 30 — "_s + f12.words);
    }
    const QString g3 = pb::fit_text(g2, 4, 35, u"GFX_provinceview_bg_modeditor"_s, src);
    const pb::Window w3 = pb::read_window(g3);
    ok(w3.view_y == -563 && w3.view_h == 525 && w3.part_h == 215 && w3.part_y == 343 && w3.fits == 4 &&
           w3.bg_sprite == u"GFX_provinceview_bg_modeditor",
       u"fitted to 4 rows: window %1 %2, part %3 %4, fits %5"_s.arg(w3.view_y).arg(w3.view_h).arg(w3.part_y).arg(w3.part_h)
           .arg(w3.fits));
    {
        const pb::Source s3 = pb::source_of(g3);
        ok(s3.noted && s3.sprite == u"GFX_provinceview_bg" && s3.top == 362 && s3.room == 105 && s3.step == 35,
           u"…the original background noted in the file"_s);
        // fitted again (6 rows, then back to 3): one comment line, at the same indentation; 3 rows = the game's places
        const QString g6 = pb::fit_text(g3, 6, 35, u"GFX_provinceview_bg_modeditor"_s, s3);
        ok(g6.count(u"ModEditor province window"_s) == 1 && pb::read_window(g6).fits == 6 &&
               pb::read_window(g6).view_y == -633,
           u"…6 rows: the window %1"_s.arg(pb::read_window(g6).view_y));
        const QString back = pb::fit_text(g6, 3, 35, u"GFX_provinceview_bg"_s, s3);
        QString plain = back;
        plain.remove(QRegularExpression(u"[ \\t]*# ModEditor province window[^\\n]*\\n"_s));
        ok(plain == g2, u"…back to 3 rows: the file as before (but the comment line)"_s);
        const QString g30 = pb::fit_text(g3, 4, 30, u"GFX_provinceview_bg_modeditor"_s, s3);
        const pb::Window w30 = pb::read_window(g30);
        ok(w30.step == 30 && w30.room == 120 && w30.view_y == -543 && w30.view_h == 505,
           u"…4 rows of 30 px: window %1 %2"_s.arg(w30.view_y).arg(w30.view_h));
    }
    {
        const QString m = pb::move_building(b2, u"testbank"_s, 0);
        ok(pb::province_keys(m) == QStringList({u"testbank"_s, u"fort"_s, u"naval_base"_s, u"railroad"_s}),
           u"moved first: "_s + pb::province_keys(m).join(u", "_s));
        const QString m2 = pb::move_building(m, u"testbank"_s, 3);
        ok(pb::province_keys(m2) == pb::province_keys(b2) && pb::lines_of(m2, u"testbank"_s).size() == 7,
           u"…and last again: "_s + pb::province_keys(m2).join(u", "_s));
        const QString s = pb::set_slot_sprite(g2, 0, u"GFX_x"_s);
        ok(pb::read_window(s).slot_sprites.value(0) == u"GFX_x" && pb::set_slot_sprite(s, 0, u"GFX_x"_s) == s,
           u"slot 0 shows another sprite"_s);
    }
    // the same positions as the hand-made test mod (Buildings test 2026-11-16, variant B3), if it is there
    const QString b3 = game.filePath(u"mod/MEBuild_B3/interface/province_interface.gui"_s);
    if (QFileInfo::exists(b3)) {
        const QStringList mine = g3.split(u'\n'), hand = norm(script::read_text(b3)).split(u'\n');
        QStringList pm, ph;
        for (const QString &l : mine)
            if ((l.contains(u"position") || l.contains(u"size =")) && !l.contains(u"ModEditor province window"))
                pm << l.simplified();
        for (const QString &l : hand)
            if (l.contains(u"position") || l.contains(u"size ="))
                ph << l.simplified();
        ok(pm == ph, u"every position / size as in the hand-made test mod B3"_s);
    }
    {
        const auto bg = imgio::read_image(game.filePath(u"gfx/interface/province_bg.dds"_s));
        ok(bg && bg->size() == QSize(416, 548), u"background read"_s);
        if (bg) {
            const QImage g = pb::fit_bg(*bg, src, 4, 35);
            const QImage a = bg->convertToFormat(QImage::Format_ARGB32);
            ok(g.size() == QSize(416, 583), u"background a row taller"_s);
            ok(g.copy(0, 0, 416, 467) == a.copy(0, 0, 416, 467) && g.copy(0, 467, 416, 35) == a.copy(0, 432, 416, 35) &&
                   g.copy(0, 502, 416, 81) == a.copy(0, 467, 416, 81),
               u"…the third row's strip doubled, the rest moved down"_s);
            g.save(out.filePath(u"bg_grown.png"_s));
            ok(pb::fit_bg(*bg, src, 3, 35) == a, u"…3 rows of 35: the picture as it was"_s);
            const QImage g30 = pb::fit_bg(*bg, src, 4, 30);
            ok(g30.size() == QSize(416, 548 - 105 + 120) && g30.copy(0, 482, 416, 81) == a.copy(0, 467, 416, 81),
               u"…4 rows of 30 px: %1 high, its foot moved"_s.arg(g30.height()));
            g30.save(out.filePath(u"bg_30.png"_s));
            pb::fit_bg(*bg, src, 2, 35).save(out.filePath(u"bg_2rows.png"_s));
        }
    }
    {
        const auto fort = imgio::read_image(game.filePath(u"gfx/interface/province_fort_strip.dds"_s));
        QImage pic(64, 64, QImage::Format_ARGB32);
        pic.fill(QColor(170, 60, 40));
        const QImage s = pb::make_strip(pic, 6);
        ok(s.size() == QSize(280, 32) && fort && fort->size() == s.size(), u"strip: 7 frames of 40 x 32, as the fort's"_s);
        s.scaled(s.width() * 3, s.height() * 3).save(out.filePath(u"strip.png"_s));
        // the strip's picture without the level number
        const QImage plain = pb::plain_picture(s, 7);
        int off = 0;
        for (int y = 0; y < plain.height(); ++y)
            for (int x = 0; x < plain.width(); ++x)
                off += plain.pixelColor(x, y) != QColor(170, 60, 40);
        ok(plain.size() == QSize(40, 32) && off < 8, u"…its picture without the number (%1 px off)"_s.arg(off));
        plain.scaled(160, 128).save(out.filePath(u"strip_plain.png"_s));
        if (fort) {
            const QImage fp = pb::plain_picture(*fort, 7);
            ok(fp.size() == QSize(40, 32), u"…the fort's picture without its number"_s);
            fp.scaled(160, 128).save(out.filePath(u"fort_plain.png"_s));
        }
    }
    {
        // slots from build_icon3 on are drawn in every row (measured): parked outside the window
        const QString b5 = pb::add_building(b2, u"mill"_s, std::nullopt);
        auto every_row = [](const std::vector<pb::Problem> &ps) {
            for (const auto &p : ps)
                if (p.text.contains(u"in every row"))
                    return true;
            return false;
        };
        auto nm = [](const QString &k) { return k; };
        ok(!every_row(pb::window_problems(b2, g2, nm)), u"one slot from 3 on, written first: no warning"_s);
        const QString g4 = pb::add_icon(g2, 4, u"GFX_y"_s);
        ok(every_row(pb::window_problems(b5, g4, nm)), u"two of them: the warning"_s);
        const QString gp = pb::set_slot_place(pb::set_slot_place(g4, 3, pb::PARK_X, -4), 4, pb::PARK_X, -4);
        const pb::Window wp = pb::read_window(gp);
        ok(wp.parked(3) && wp.parked(4) && !wp.parked(0) && wp.icon_x == 23 && wp.icon_y == -4 &&
               gp.count(u"x= -3000"_s) == 2,
           u"…parked: the row's picture place still 23, -4 (%1, %2)"_s.arg(wp.icon_x).arg(wp.icon_y));
        ok(!every_row(pb::window_problems(b5, gp, nm)), u"…no warning"_s);
        ok(pb::set_slot_place(gp, 3, pb::PARK_X, -4) == gp, u"…parked again: nothing changes"_s);
        const auto bg = imgio::read_image(game.filePath(u"gfx/interface/province_bg.dds"_s));
        if (bg) {
            QImage g = pb::fit_bg(*bg, src, 5, 30);
            QImage pic(40, 32, QImage::Format_ARGB32);
            pic.fill(QColor(20, 200, 90));
            pb::paint_rows(g, wp, src.top, 30, {{3, pic}, {4, pic}});
            // row 4 (index 3): x = part 0 - background 0 + 23, y = 362 + 3 * 30 - 4
            ok(g.pixelColor(23 + wp.part_x - wp.bg_x + 5, 362 + 90 - 4 + 5) == QColor(20, 200, 90) &&
                   g.pixelColor(23 + wp.part_x - wp.bg_x + 5, 362 + 120 - 4 + 5) == QColor(20, 200, 90) &&
                   g.pixelColor(23 + wp.part_x - wp.bg_x + 5, 362 + 60 - 4 + 5) != QColor(20, 200, 90),
               u"rows 4 and 5 painted into the background, row 3 not"_s);
            g.save(out.filePath(u"bg_painted.png"_s));
        }
    }
    {
        const QString gfx = pb::add_sprites(QString(), {pb::sprite_block(u"GFX_a"_s, u"gfx/interface/a.tga"_s, 7)});
        ok(gfx == u"spriteTypes = {\n\tspriteType = {\n\t\tname = \"GFX_a\"\n\t\ttextureFile = "
                  u"\"gfx\\\\interface\\\\a.tga\"\n\t\tnoOfFrames = 7\n\t}\n}\n",
           u"a new .gfx: "_s + gfx);
        const QString g2x = pb::add_sprites(gfx, {pb::sprite_block(u"GFX_b"_s, u"gfx/interface/b.tga"_s, 1),
                                                  pb::sprite_block(u"GFX_a"_s, u"x"_s, 7)});
        ok(g2x.count(u"spriteType ="_s) == 2 && g2x.contains(u"GFX_b"_s), u"…a second sprite, the first not twice"_s);
    }
    std::printf("%s\n", fails ? "FAILED" : "PASS");
    return fails ? 1 : 0;
}
