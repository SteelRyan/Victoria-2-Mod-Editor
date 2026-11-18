// No dialog box, ever, from a check program (the user's rule: no pop-ups; 2026-10-19 a Debug build's Qt assertion
// showed "Debug Error!" on the user's screen, twice): Qt's fatal messages (Q_ASSERT, qFatal) are printed to stderr and
// end the program at once — before Qt's own code can open its box; the C runtime's asserts / errors / aborts go to
// stderr; a crash ends the program without Windows' crash box. Included by every check program (one static
// initializer runs before main).
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <crtdbg.h>
#include <dbghelp.h>
#include <cstring>
#pragma comment(lib, "dbghelp.lib")
#endif
#include <QtGlobal>
#include <QString>
#include <cstdio>
#include <cstdlib>

namespace nopopup {
inline void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const QByteArray m = msg.toLocal8Bit();
    std::fprintf(stderr, "%s%s (%s:%d)\n", type == QtFatalMsg ? "FATAL: " : "", m.constData(),
                 ctx.file ? ctx.file : "?", ctx.line);
    std::fflush(stderr);
    if (type == QtFatalMsg) {
#ifdef _WIN32
        TerminateProcess(GetCurrentProcess(), 3);      // (no abort(): the Debug runtime would show its box)
#endif
        std::_Exit(3);
    }
}
#if defined(_WIN32) && !defined(__SANITIZE_ADDRESS__)
// A crash prints where it happened to stderr (module!function+offset, with file:line where a .pdb is next to the
// exe — tools\pdb_build.ps1), then ends the program with the exception's code. (Not in the ASan build: ASan reports.)
inline LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    HANDLE proc = GetCurrentProcess(), th = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, nullptr, TRUE);
    std::fprintf(stderr, "CRASH: exception 0x%08lX at %p (thread %lu)\n", ep->ExceptionRecord->ExceptionCode,
                 ep->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 f{};
    f.AddrPC.Offset = ctx.Rip;
    f.AddrPC.Mode = AddrModeFlat;
    f.AddrFrame.Offset = ctx.Rbp;
    f.AddrFrame.Mode = AddrModeFlat;
    f.AddrStack.Offset = ctx.Rsp;
    f.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 48 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, th, &f, &ctx, nullptr, SymFunctionTableAccess64,
                                          SymGetModuleBase64, nullptr); ++i) {
        const DWORD64 a = f.AddrPC.Offset;
        if (!a)
            break;
        char mod[MAX_PATH] = "?";
        HMODULE hm = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(a), &hm))
            GetModuleFileNameA(hm, mod, MAX_PATH);
        const char *base = std::strrchr(mod, '\\');
        base = base ? base + 1 : mod;
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512] = {};
        auto *s = reinterpret_cast<SYMBOL_INFO *>(buf);
        s->SizeOfStruct = sizeof(SYMBOL_INFO);
        s->MaxNameLen = 511;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 ln{};
        ln.SizeOfStruct = sizeof(ln);
        DWORD ld = 0;
        if (SymFromAddr(proc, a, &disp, s)) {
            if (SymGetLineFromAddr64(proc, a, &ld, &ln))
                std::fprintf(stderr, "  #%d %s!%s+0x%llx (%s:%lu)\n", i, base, s->Name, (unsigned long long)disp,
                             ln.FileName, ln.LineNumber);
            else
                std::fprintf(stderr, "  #%d %s!%s+0x%llx\n", i, base, s->Name, (unsigned long long)disp);
        } else
            std::fprintf(stderr, "  #%d %s+0x%llx\n", i, base, (unsigned long long)(a - DWORD64(hm)));
    }
    std::fflush(stderr);
    TerminateProcess(proc, ep->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
struct Guard {
    Guard()
    {
#ifdef _WIN32
#ifndef __SANITIZE_ADDRESS__
        SetUnhandledExceptionFilter(crash_filter);
#endif
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
        qInstallMessageHandler(handler);
    }
};
inline Guard guard;
}  // namespace nopopup
