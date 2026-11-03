#include "scan.h"
#include "logging.h"

#include <Windows.h>
#include <Psapi.h>
#include <stdio.h>
#include <map>
#include <string.h>

std::map<Scan::Pattern, Scan::ScanResult> ScanCache = {};

Scan::Pattern Scan::Parse(const char* pattern) {
    int pattern_len = (strlen(pattern) + 1) / 3;
    char* pattern_bytes = new char[pattern_len];
    char* mask = new char[pattern_len];

    for (int i = 0; i < strlen(pattern); i++) {
        if (pattern[i] == ' ') {
            continue;
        }
        else if (pattern[i] == '?') {
            mask[(i + 1) / 3] = '?';
            i += 2;
        }
        else if (pattern [i] == '*') {
            mask[(i + 1) / 3] = '*';
            i += 2;
        }
        else {
            char byte = (char)strtol(&pattern[i], 0, 16) & 0xFF;
            pattern_bytes[(i + 1) / 3] = byte;
            mask[(i + 1) / 3] = 'x';
            i += 2;
        }
    }

    return Pattern { pattern_bytes, mask, pattern_len };
}

Scan::ScanResult Scan::Internal::ScanModule(const char* moduleName, Scan::Pattern pattern) {
    if (ScanCache.contains(pattern)) {
        DEBUG("Cached pattern found.");
        return ScanCache[pattern];
    }

    HMODULE module = GetModuleHandleA(moduleName);
    MODULEINFO info {};
    if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) {
        Scan::ScanResult failed {};
        return failed;
    }

    uintptr_t base = (uintptr_t)module;

    char store[4] = {0};
    uintptr_t loc = 0;

    for (unsigned int i = 0; i < info.SizeOfImage - pattern.len; i++) {
        bool found = true;
        int store_used = 0;

        for (int j = 0; j < pattern.len; j++) {
            if (pattern.mask[j] == '*') {
                if (store_used < sizeof(store)) {
                    store[store_used++] = *(char*)(base + i + j);
                }
                continue;
            }

            if (pattern.mask[j] != '?' && pattern.pattern[j] != *(char*)(base + i + j)) {
                found = false;
                break;
            }
        }

        if (found) {
            // DEBUG("Found 0x%.8X", i);
            loc = base + i;
            break;
        }
    }

    Scan::ScanResult result {};
    result.found = loc != 0;
    result.loc = loc;
    memcpy(result.store, store, sizeof(result.store));

    ScanCache.insert(std::pair(pattern, result));

    return result;
}

// Counts how many places in the module match the pattern (stops early at max_count).
// Reads are guarded so a bad page can never take the game down.
int Scan::Internal::CountMatches(const char* moduleName, const char* patternText, int max_count) {
    Scan::Pattern pattern = Scan::Parse(patternText);
    HMODULE module = GetModuleHandleA(moduleName);
    MODULEINFO info {};
    if (!module || !GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) {
        return -1;
    }
    uintptr_t base = (uintptr_t)module;
    int count = 0;
    __try {
        for (unsigned int i = 0; i + pattern.len < info.SizeOfImage; i++) {
            bool found = true;
            for (int j = 0; j < pattern.len; j++) {
                if (pattern.mask[j] == '*' || pattern.mask[j] == '?') continue;
                if (pattern.pattern[j] != *(char*)(base + i + j)) { found = false; break; }
            }
            if (found && ++count >= max_count) break;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return count;
}
