// eventtemplates against Python, the C++ side of tools/check_eventtemplates.py: prints the same lines.
#include "nopopup.h"
#include "pyform.h"
#include "core/eventtemplates.h"

#include <cstdio>
#include <tuple>

using namespace Qt::StringLiterals;
using pyform::v;

int main()
{
    namespace tp = eventtemplates;
    QStringList out;
    auto names_of = [&](const tp::Names &names) {
        for (const auto &[a, b] : names)
            out << u"  %1 = %2"_s.arg(a, v(b));
    };
    for (const tp::Template &t : tp::TEMPLATES) {
        out << u"template %1 | %2 | %3 | %4 | %5"_s.arg(t.key, v(t.name), v(t.what), QString::number(t.ids),
                                                        v(tp::NAME.value(t.key)));
        const tp::Made m = tp::make(t.key, {u"3100"_s, u"3101"_s, u"3102"_s});
        out << u"make %1 %2 | %3"_s.arg(t.key, v(m.first), v(m.text));
        names_of(m.names);
    }
    for (const auto &[eid, kind, ch] : {std::tuple{u"77"_s, u"country_event"_s, 1}, std::tuple{u"78"_s, u"province_event"_s, 2},
                                        std::tuple{u"x_1"_s, u"country_event"_s, 0}}) {
        const auto [text, names] = tp::follow_up(eid, kind, ch);
        out << u"follow %1 %2 %3 | %4"_s.arg(eid, kind, QString::number(ch), v(text));
        names_of(names);
    }
    out << u"block "_s + v(tp::event_block(u"country_event"_s, u"9"_s, 12, false, 3, {u"t = 1"_s}, {u"a = b"_s},
                                            u"pic"_s, {{11, {u"x = y"_s}}}));
    out << u"call "_s + v(tp::call(u"55"_s, 7));
    const QByteArray b = (out.join(u'\n') + u'\n').toUtf8();
    std::fwrite(b.constData(), 1, size_t(b.size()), stdout);
    return 0;
}
