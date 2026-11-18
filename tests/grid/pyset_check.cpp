// grid/pyset.h against Python's sets, the C++ side of tools/check_pyset.py.  Usage: med_pyset <ops.txt>
#include "../core/nopopup.h"
#include "grid/pyset.h"

#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    std::ifstream in(argv[1]);
    std::string line, out;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string w, kind;
        long long n;
        s >> w >> kind >> n;
        if (w == "hash") {                               // hash(str) with PYTHONHASHSEED=0
            std::string word;
            std::getline(s >> std::ws, word);
            out += std::to_string(grid::py_hash(QString::fromStdString(word))) + "\n";
            continue;
        }
        if (w == "srun") {                               // a set of texts
            grid::PySet<QString> ss;
            for (long long i = 0; i < n; ++i) {
                std::getline(in, line);
                const std::string op = line.substr(0, 1), word = line.substr(2);
                const QString q = QString::fromStdString(word);
                if (op == "a")
                    ss.add(q);
                else if (op == "p") {
                    if (!ss.empty())
                        ss.pop();
                } else
                    ss.discard(q);
                if (i % 25 == 24 || i == n - 1) {
                    QStringList l;
                    for (const QString &k : ss.items())
                        l << k;
                    out += l.join(u' ').toStdString() + "\n";
                }
            }
            continue;
        }
        grid::PySet<long long> si;
        grid::PySet<std::pair<std::int64_t, std::int64_t>> st;
        for (long long i = 0; i < n; ++i) {
            std::getline(in, line);
            std::istringstream o(line);
            std::string op;
            long long a = 0, b = 0;
            o >> op >> a;
            if (kind == "t")
                o >> b;
            if (op == "p") {
                if (kind == "i" && !si.empty())
                    si.pop();
                else if (kind != "i" && !st.empty())
                    st.pop();
            } else if (kind == "i")
                op == "a" ? si.add(a) : (void)si.discard(a);
            else
                op == "a" ? st.add({a, b}) : (void)st.discard({a, b});
            if (i % 50 == 49 || i == n - 1) {
                std::string l;
                if (kind == "i")
                    for (long long k : si.items())
                        l += (l.empty() ? "" : " ") + std::to_string(k);
                else
                    for (const auto &[x, y] : st.items())
                        l += (l.empty() ? "" : " ") + std::to_string(x) + "," + std::to_string(y);
                out += l + "\n";
            }
        }
        // the other set: set(s), s - t, t - s
        std::getline(in, line);
        std::istringstream os(line);
        std::string ow;
        long long on;
        os >> ow >> on;
        grid::PySet<long long> ti;
        grid::PySet<std::pair<std::int64_t, std::int64_t>> tt;
        std::vector<long long> oi;                       // (the other list in its order, for set(dict))
        std::vector<std::pair<std::int64_t, std::int64_t>> ot;
        for (long long i = 0; i < on; ++i) {
            std::getline(in, line);
            std::istringstream o(line);
            long long a = 0, b = 0;
            o >> a;
            if (kind == "t") {
                o >> b;
                tt.add({a, b});
                ot.push_back({a, b});
            } else {
                ti.add(a);
                oi.push_back(a);
            }
        }
        auto walk_i = [&](const grid::PySet<long long> &z) {
            std::string l;
            for (long long k : z.items())
                l += (l.empty() ? "" : " ") + std::to_string(k);
            out += l + "\n";
        };
        auto walk_t = [&](const grid::PySet<std::pair<std::int64_t, std::int64_t>> &z) {
            std::string l;
            for (const auto &[x, y] : z.items())
                l += (l.empty() ? "" : " ") + std::to_string(x) + "," + std::to_string(y);
            out += l + "\n";
        };
        if (kind == "i") {
            walk_i(grid::PySet<long long>::copy_of(si));
            walk_i(si.difference(ti));
            walk_i(ti.difference(si));
            std::vector<long long> k;                    // set(dict.fromkeys(other + other[:3]))
            for (size_t j = 0; j < oi.size() + std::min<size_t>(3, oi.size()); ++j) {
                const long long x = oi[j < oi.size() ? j : j - oi.size()];
                if (std::find(k.begin(), k.end(), x) == k.end())
                    k.push_back(x);
            }
            walk_i(grid::PySet<long long>::from_keys(k));
        } else {
            walk_t(grid::PySet<std::pair<std::int64_t, std::int64_t>>::copy_of(st));
            walk_t(st.difference(tt));
            walk_t(tt.difference(st));
            std::vector<std::pair<std::int64_t, std::int64_t>> k;
            for (size_t j = 0; j < ot.size() + std::min<size_t>(3, ot.size()); ++j) {
                const auto x = ot[j < ot.size() ? j : j - ot.size()];
                if (std::find(k.begin(), k.end(), x) == k.end())
                    k.push_back(x);
            }
            walk_t(grid::PySet<std::pair<std::int64_t, std::int64_t>>::from_keys(k));
        }
    }
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
