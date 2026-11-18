// Python's difflib, the part the editor uses: SequenceMatcher (isjunk None, autojunk on) over the letters of two
// texts, and get_close_matches — line by line from CPython's Lib/difflib.py, so the "meant: …?" hints are the same.
#pragma once
#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>
#include <tuple>
#include <vector>

namespace py {

class SequenceMatcher {
public:
    SequenceMatcher() = default;
    SequenceMatcher(const QString &a, const QString &b) { set_seqs(a, b); }
    void set_seqs(const QString &a, const QString &b)
    {
        set_seq1(a);
        set_seq2(b);
    }
    void set_seq1(const QString &a);
    void set_seq2(const QString &b);
    std::tuple<qsizetype, qsizetype, qsizetype> find_longest_match(qsizetype alo, qsizetype ahi, qsizetype blo,
                                                                   qsizetype bhi) const;
    std::vector<std::tuple<qsizetype, qsizetype, qsizetype>> get_matching_blocks();
    double ratio();
    double quick_ratio();
    double real_quick_ratio() const;

private:
    QString a_, b_;
    QHash<QChar, std::vector<qsizetype>> b2j_;
    std::optional<std::vector<std::tuple<qsizetype, qsizetype, qsizetype>>> matching_blocks_;
    std::optional<QHash<QChar, qsizetype>> fullbcount_;
    void chain_b();
};

// get_close_matches(word, possibilities, n=3, cutoff=0.6): the best n with a ratio >= cutoff, best first
QStringList get_close_matches(const QString &word, const QStringList &possibilities, qsizetype n = 3,
                              double cutoff = 0.6);

}  // namespace py
