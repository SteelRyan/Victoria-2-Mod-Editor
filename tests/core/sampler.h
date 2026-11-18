// A small sampling profiler for the checks (Windows x64, C++ only, 2026-11-10 — the performance round): a thread
// stops the watched thread about every millisecond, walks its stack (the unwind tables, as the debugger does) and
// counts which functions were on it. Names come from the .pdb (build in build\pdb, RelWithDebInfo). Never shipped.
//   prof::Sampler s(GetCurrentThread());  s.on = true;  …  s.on = false;  s.report(40);
#pragma once
#include <windows.h>
#include <dbghelp.h>
#include <timeapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")

namespace prof {

class Sampler {
public:
    explicit Sampler(HANDLE thread)
    {
        DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS);
        worker = std::thread([this] { loop(); });
    }
    ~Sampler()
    {
        quit = true;
        worker.join();
        CloseHandle(target);
    }
    std::atomic<bool> on{false};

    void clear()
    {
        std::lock_guard<std::mutex> g(mx);
        stacks.clear();
    }
    size_t samples()
    {
        std::lock_guard<std::mutex> g(mx);
        return stacks.size();
    }
    // the functions most often on the stack (inclusive), with their share of the samples, and the top frames (self)
    void report(int top = 40, const char *title = "")
    {
        std::lock_guard<std::mutex> g(mx);
        if (stacks.empty()) {
            std::printf("[profile %s] no samples\n", title);
            return;
        }
        static bool init = false;
        if (!init) {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            SymInitialize(GetCurrentProcess(), nullptr, TRUE);
            init = true;
        }
        std::unordered_map<DWORD64, std::string> names;
        auto name = [&](DWORD64 a) -> const std::string & {
            auto it = names.find(a);
            if (it != names.end())
                return it->second;
            char buf[sizeof(SYMBOL_INFO) + 512];
            auto *si = reinterpret_cast<SYMBOL_INFO *>(buf);
            si->SizeOfStruct = sizeof(SYMBOL_INFO);
            si->MaxNameLen = 500;
            DWORD64 disp = 0;
            std::string n;
            if (SymFromAddr(GetCurrentProcess(), a, &disp, si))
                n = si->Name;
            else {
                char m[64];
                std::snprintf(m, sizeof m, "?%llx", (unsigned long long)a);
                n = m;
            }
            return names.emplace(a, n).first->second;
        };
        std::map<std::string, long> incl, self;
        size_t busy = 0;
        for (const auto &st : stacks) {
            // (waiting for events / sleeping: not work, left out)
            const std::string &top0 = name(st[0]);
            if (top0 == "ZwDelayExecution" || top0 == "NtUserMsgWaitForMultipleObjectsEx" ||
                top0 == "NtWaitForSingleObject" || top0 == "ZwWaitForSingleObject" || top0 == "NtWaitForMultipleObjects")
                continue;
            ++busy;
            std::vector<const std::string *> seen;
            for (size_t i = 0; i < st.size(); ++i) {
                const std::string &n = name(st[i] - (i ? 1 : 0));   // (a return address points after the call)
                if (i == 0)
                    ++self[n];
                if (std::find(seen.begin(), seen.end(), &n) == seen.end()) {
                    seen.push_back(&n);
                    ++incl[n];
                }
            }
        }
        auto print = [&](const std::map<std::string, long> &m, const char *what) {
            std::vector<std::pair<long, std::string>> v;
            for (const auto &[k, c] : m)
                v.emplace_back(c, k);
            std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            std::printf("[profile %s] %s, %zu busy samples of %zu (~1 ms each)\n", title, what, busy, stacks.size());
            for (int i = 0; i < top && i < int(v.size()); ++i)
                std::printf("  %5.1f %%  %6ld  %s\n", 100.0 * double(v[size_t(i)].first) / double(std::max<size_t>(1, busy)),
                            v[size_t(i)].first, v[size_t(i)].second.substr(0, 150).c_str());
        };
        print(incl, "on the stack (inclusive)");
        print(self, "running itself (self)");
    }

private:
    HANDLE target = nullptr;
    std::thread worker;
    std::atomic<bool> quit{false};
    std::mutex mx;
    std::vector<std::vector<DWORD64>> stacks;

    void loop()
    {
        timeBeginPeriod(1);
        // (Sleep(1) came out ~15 ms: a high-resolution waitable timer gives the 1 ms)
        HANDLE tick = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        DWORD64 pcs[128];
        while (!quit) {
            if (on) {
                int n = 0;
                if (SuspendThread(target) != DWORD(-1)) {
                    // (nothing may allocate while it is stopped: it may hold the heap's lock)
                    CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_FULL;
                    if (GetThreadContext(target, &ctx)) {
                        for (; n < 128 && ctx.Rip; ++n) {
                            pcs[n] = ctx.Rip;
                            DWORD64 base = 0;
                            PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
                            if (!fe) {
                                ctx.Rip = *reinterpret_cast<DWORD64 *>(ctx.Rsp);
                                ctx.Rsp += 8;
                            } else {
                                PVOID hd = nullptr;
                                DWORD64 ef = 0;
                                RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fe, &ctx, &hd, &ef, nullptr);
                            }
                        }
                    }
                    ResumeThread(target);
                }
                if (n) {
                    std::lock_guard<std::mutex> g(mx);
                    stacks.emplace_back(pcs, pcs + n);
                }
            }
            if (tick) {
                LARGE_INTEGER due;
                due.QuadPart = -10000;                   // 1 ms (100 ns units, relative)
                SetWaitableTimer(tick, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(tick, 50);
            } else {
                Sleep(1);
            }
        }
        if (tick)
            CloseHandle(tick);
        timeEndPeriod(1);
    }
};

}  // namespace prof
