#include "crash.h"

#include <Windows.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Everything in the exception handler uses fixed buffers on the stack and plain
// Win32 calls, because it may run while the game's heap is damaged.

namespace crash {

static char g_report[MAX_PATH];
static char g_old[MAX_PATH];
static char g_last[MAX_PATH];

static std::atomic<int> g_count { 0 };
static char g_session[160] = {};
static char g_prev[160]    = {};

static LPTOP_LEVEL_EXCEPTION_FILTER g_prev_filter = nullptr;
static PVOID g_veh = nullptr;

static const char* CodeName(DWORD c) {
    switch (c) {
        case 0xC0000005: return "ACCESS VIOLATION";
        case 0xC0000006: return "IN PAGE ERROR";
        case 0xC000001D: return "ILLEGAL INSTRUCTION";
        case 0xC0000094: return "INTEGER DIVIDE BY ZERO";
        case 0xC0000096: return "PRIVILEGED INSTRUCTION";
        case 0xC00000FD: return "STACK OVERFLOW";
        case 0xC0000374: return "HEAP CORRUPTION";
        case 0xC0000409: return "FAST FAIL";
        case 0xC000008C: return "ARRAY BOUNDS";
        default:         return "EXCEPTION";
    }
}

static bool IsFatal(DWORD c) {
    switch (c) {
        case 0xC0000005: case 0xC0000006: case 0xC000001D: case 0xC0000094:
        case 0xC0000096: case 0xC00000FD: case 0xC0000374: case 0xC0000409:
        case 0xC000008C:
            return true;
        default:
            return false;
    }
}

static void ModuleOf(const void* addr, char* name, size_t n, unsigned long long* rva) {
    name[0] = '\0';
    *rva = (unsigned long long)(uintptr_t)addr;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(addr, &mbi, sizeof(mbi)) || !mbi.AllocationBase) {
        snprintf(name, n, "unknown");
        return;
    }
    char path[MAX_PATH] = {};
    if (GetModuleFileNameA((HMODULE)mbi.AllocationBase, path, MAX_PATH)) {
        const char* slash = strrchr(path, '\\');
        snprintf(name, n, "%s", slash ? slash + 1 : path);
    } else {
        snprintf(name, n, "unknown");
    }
    *rva = (unsigned long long)((uintptr_t)addr - (uintptr_t)mbi.AllocationBase);
}

static void WriteText(const char* path, const char* text, bool append) {
    HANDLE h = CreateFileA(path, append ? FILE_APPEND_DATA : GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           append ? OPEN_ALWAYS : CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, text, (DWORD)strlen(text), &written, nullptr);
    CloseHandle(h);
}

static void Report(const char* kind, PEXCEPTION_POINTERS ep, bool force) {
    int n = ++g_count;
    if (n > 20 && !force) return;
    if (!ep || !ep->ExceptionRecord) return;

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    const void* addr = ep->ExceptionRecord->ExceptionAddress;
    char mod[64];
    unsigned long long rva = 0;
    ModuleOf(addr, mod, sizeof(mod), &rva);

    char buf[3072];
    int len = 0;
    len += snprintf(buf + len, sizeof(buf) - len,
        "[%llu s] %s #%d\r\n  %s (0x%08lX)\r\n  at %s+0x%llX\r\n  thread %lu, toolkit was doing: %s\r\n",
        (unsigned long long)(GetTickCount64() / 1000), kind, n, CodeName(code), (unsigned long)code,
        mod, rva, (unsigned long)GetCurrentThreadId(), context ? context : "unknown");

    if (code == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR how = ep->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR where = ep->ExceptionRecord->ExceptionInformation[1];
        len += snprintf(buf + len, sizeof(buf) - len, "  tried to %s address 0x%llX\r\n",
                        how == 0 ? "read" : (how == 1 ? "write" : "run"), (unsigned long long)where);
    }

    void* frames[16] = {};
    USHORT cnt = RtlCaptureStackBackTrace(0, 16, frames, nullptr);
    len += snprintf(buf + len, sizeof(buf) - len, "  call stack:\r\n");
    for (USHORT i = 0; i < cnt && len < (int)sizeof(buf) - 160; i++) {
        char fm[64];
        unsigned long long frva = 0;
        ModuleOf(frames[i], fm, sizeof(fm), &frva);
        len += snprintf(buf + len, sizeof(buf) - len, "    %s+0x%llX\r\n", fm, frva);
    }
    len += snprintf(buf + len, sizeof(buf) - len, "\r\n");
    WriteText(g_report, buf, true);

    char summary[160];
    snprintf(summary, sizeof(summary), "%s AT %s+0x%llX", CodeName(code), mod, rva);
    for (char* p = summary; *p; ++p) *p = (char)toupper((unsigned char)*p);
    snprintf(g_session, sizeof(g_session), "%s", summary);
    WriteText(g_last, summary, false);
}

static LONG CALLBACK Veh(PEXCEPTION_POINTERS ep) {
    if (guard_depth > 0) return EXCEPTION_CONTINUE_SEARCH;           // caught by the toolkit itself
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    if (!IsFatal(ep->ExceptionRecord->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;

    static thread_local bool busy = false;
    if (busy) return EXCEPTION_CONTINUE_SEARCH;
    busy = true;
    Report("EXCEPTION NOT CAUGHT BY THE TOOLKIT", ep, false);
    busy = false;
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI Unhandled(PEXCEPTION_POINTERS ep) {
    static thread_local bool busy = false;
    if (!busy) {
        busy = true;
        Report("UNHANDLED EXCEPTION (GAME CRASH)", ep, true);
        busy = false;
    }
    if (g_prev_filter) return g_prev_filter(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

void Install(const std::string& dir) {
    snprintf(g_report, sizeof(g_report), "%scrash_report.txt", dir.c_str());
    snprintf(g_old,    sizeof(g_old),    "%scrash_report_old.txt", dir.c_str());
    snprintf(g_last,   sizeof(g_last),   "%slast_crash.txt", dir.c_str());

    // Summary of the last crash from an earlier session.
    HANDLE h = CreateFileA(g_last, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD got = 0;
        ReadFile(h, g_prev, sizeof(g_prev) - 1, &got, nullptr);
        g_prev[got < sizeof(g_prev) ? got : sizeof(g_prev) - 1] = '\0';
        for (char* p = g_prev; *p; ++p) if (*p == '\r' || *p == '\n') *p = '\0';
        CloseHandle(h);
    }

    // Keep the previous session's full report next to the new one.
    MoveFileExA(g_report, g_old, MOVEFILE_REPLACE_EXISTING);

    if (!g_veh) g_veh = AddVectoredExceptionHandler(1, Veh);
    g_prev_filter = SetUnhandledExceptionFilter(Unhandled);
}

int         Count()       { return g_count.load(); }
const char* ThisSession() { return g_session; }
const char* Previous()    { return g_prev; }

void Clear() {
    DeleteFileA(g_report);
    DeleteFileA(g_old);
    DeleteFileA(g_last);
    g_count = 0;
    g_session[0] = '\0';
    g_prev[0] = '\0';
}

} // namespace crash
