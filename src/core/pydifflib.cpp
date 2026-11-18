// Python's difflib (SequenceMatcher, get_close_matches), from CPython's Lib/difflib.py.
#include "core/pydifflib.h"

#include <QSet>
#include <algorithm>
#include <stdexcept>

namespace py {

static double calculate_ratio(qsizetype matches, qsizetype length)
{
    return length ? 2.0 * double(matches) / double(length) : 1.0;
}

void SequenceMatcher::set_seq1(const QString &a)
{
    if (a == a_ && matching_blocks_)
        return;
    a_ = a;
    matching_blocks_.reset();
}

void SequenceMatcher::set_seq2(const QString &b)
{
    b_ = b;
    matching_blocks_.reset();
    fullbcount_.reset();
    chain_b();
}

void SequenceMatcher::chain_b()
{
    // b2j: element -> the positions where it occurs in b; isjunk None, so no junk — only the "popular" elements of
    // a b of 200 or more (more than 1% + 1 of it) are left out (autojunk)
    b2j_.clear();
    for (qsizetype i = 0; i < b_.size(); ++i)
        b2j_[b_[i]].push_back(i);
    const qsizetype n = b_.size();
    if (n >= 200) {
        const qsizetype ntest = n / 100 + 1;
        QList<QChar> popular;
        for (auto it = b2j_.cbegin(); it != b2j_.cend(); ++it)
            if (qsizetype(it.value().size()) > ntest)
                popular << it.key();
        for (QChar c : popular)
            b2j_.remove(c);
    }
}

std::tuple<qsizetype, qsizetype, qsizetype> SequenceMatcher::find_longest_match(qsizetype alo, qsizetype ahi,
                                                                                qsizetype blo, qsizetype bhi) const
{
    qsizetype besti = alo, bestj = blo, bestsize = 0;
    QHash<qsizetype, qsizetype> j2len;
    static const std::vector<qsizetype> nothing;
    for (qsizetype i = alo; i < ahi; ++i) {
        QHash<qsizetype, qsizetype> newj2len;
        const auto it = b2j_.constFind(a_[i]);
        const std::vector<qsizetype> &js = it == b2j_.cend() ? nothing : it.value();
        for (qsizetype j : js) {
            // a[i] matches b[j]
            if (j < blo)
                continue;
            if (j >= bhi)
                break;
            const qsizetype k = j2len.value(j - 1, 0) + 1;
            newj2len.insert(j, k);
            if (k > bestsize) {
                besti = i - k + 1;
                bestj = j - k + 1;
                bestsize = k;
            }
        }
        j2len = std::move(newj2len);
    }
    // (no junk: the extensions with non-junk elements are all there is; the junk ones find nothing)
    while (besti > alo && bestj > blo && a_[besti - 1] == b_[bestj - 1]) {
        --besti;
        --bestj;
        ++bestsize;
    }
    while (besti + bestsize < ahi && bestj + bestsize < bhi && a_[besti + bestsize] == b_[bestj + bestsize])
        ++bestsize;
    return {besti, bestj, bestsize};
}

std::vector<std::tuple<qsizetype, qsizetype, qsizetype>> SequenceMatcher::get_matching_blocks()
{
    if (matching_blocks_)
        return *matching_blocks_;
    const qsizetype la = a_.size(), lb = b_.size();
    std::vector<std::tuple<qsizetype, qsizetype, qsizetype, qsizetype>> queue{{0, la, 0, lb}};
    std::vector<std::tuple<qsizetype, qsizetype, qsizetype>> blocks;
    while (!queue.empty()) {
        const auto [alo, ahi, blo, bhi] = queue.back();
        queue.pop_back();
        const auto x = find_longest_match(alo, ahi, blo, bhi);
        const auto [i, j, k] = x;
        if (k) {
            blocks.push_back(x);
            if (alo < i && blo < j)
                queue.emplace_back(alo, i, blo, j);
            if (i + k < ahi && j + k < bhi)
                queue.emplace_back(i + k, ahi, j + k, bhi);
        }
    }
    std::sort(blocks.begin(), blocks.end());
    // adjacent blocks collapsed into one
    std::vector<std::tuple<qsizetype, qsizetype, qsizetype>> out;
    qsizetype i1 = 0, j1 = 0, k1 = 0;
    for (const auto &[i2, j2, k2] : blocks) {
        if (i1 + k1 == i2 && j1 + k1 == j2) {
            k1 += k2;
        } else {
            if (k1)
                out.emplace_back(i1, j1, k1);
            i1 = i2;
            j1 = j2;
            k1 = k2;
        }
    }
    if (k1)
        out.emplace_back(i1, j1, k1);
    out.emplace_back(la, lb, 0);
    matching_blocks_ = out;
    return out;
}

double SequenceMatcher::ratio()
{
    qsizetype matches = 0;
    for (const auto &t : get_matching_blocks())
        matches += std::get<2>(t);
    return calculate_ratio(matches, a_.size() + b_.size());
}

double SequenceMatcher::quick_ratio()
{
    if (!fullbcount_) {
        fullbcount_.emplace();
        for (QChar c : b_)
            (*fullbcount_)[c] += 1;
    }
    QHash<QChar, qsizetype> avail;
    qsizetype matches = 0;
    for (QChar c : a_) {
        const qsizetype numb = avail.contains(c) ? avail.value(c) : fullbcount_->value(c, 0);
        avail.insert(c, numb - 1);
        if (numb > 0)
            ++matches;
    }
    return calculate_ratio(matches, a_.size() + b_.size());
}

double SequenceMatcher::real_quick_ratio() const
{
    const qsizetype la = a_.size(), lb = b_.size();
    return calculate_ratio(std::min(la, lb), la + lb);
}

QStringList get_close_matches(const QString &word, const QStringList &possibilities, qsizetype n, double cutoff)
{
    if (n <= 0)
        throw std::invalid_argument("n must be > 0");
    if (!(0.0 <= cutoff && cutoff <= 1.0))
        throw std::invalid_argument("cutoff must be in [0.0, 1.0]");
    std::vector<std::pair<double, QString>> result;
    SequenceMatcher s;
    s.set_seq2(word);
    for (const QString &x : possibilities) {
        s.set_seq1(x);
        if (s.real_quick_ratio() >= cutoff && s.quick_ratio() >= cutoff && s.ratio() >= cutoff)
            result.emplace_back(s.ratio(), x);
    }
    // heapq.nlargest(n, result): the largest (score, x) first — equal scores: the larger text first
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return b < a; });
    QStringList out;
    for (qsizetype i = 0; i < qsizetype(result.size()) && i < n; ++i)
        out << result[size_t(i)].second;
    return out;
}

}  // namespace py
