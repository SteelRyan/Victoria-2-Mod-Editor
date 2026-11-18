// shortcutfacts.py — the game test's findings, as tables.
#include "core/shortcutfacts.h"

namespace shortcutfacts {

#include "core/shortcutfacts_data.inc"

const QString TESTED = QString::fromUtf16(TESTED_TEXT);

const OrderedMap<QString, QString> &WORD()
{
    static const OrderedMap<QString, QString> m = [] {
        OrderedMap<QString, QString> out;
        for (const auto &w : WORD_DATA)
            out.insert(QString::fromUtf16(w[0]), QString::fromUtf16(w[1]));
        return out;
    }();
    return m;
}

const OrderedMap<QString, Fact> &FACTS()
{
    static const OrderedMap<QString, Fact> m = [] {
        OrderedMap<QString, Fact> out;
        for (const FactRow &r : FACT_DATA)
            out.insert(QString::fromUtf16(r.key), {QString::fromUtf16(r.cls), QString::fromUtf16(FACT_TEXTS[r.text])});
        return out;
    }();
    return m;
}

}  // namespace shortcutfacts
