#pragma once

#include <Windows.h>

namespace Scan {

    struct Pattern {
        char* pattern;
        char* mask;
        int len;

        bool operator<(const Pattern& other) const {
            if (len != other.len) {
                return len < other.len;
            }

            for (int i = 0; i < len; i++) {
                if (pattern[i] != other.pattern[i]) {
                    return pattern[i] < other.pattern[i];
                }
            }

            for (int i = 0; i < len; i++) {
                if (mask[i] != other.mask[i]) {
                    return mask[i] < other.mask[i];
                }
            }

            return false;
        }
    };

    typedef struct Pattern Pattern;
    
    Pattern Parse(const char* pattern);

    typedef struct {
        bool found;
        uintptr_t loc;
        char store[4];
    } ScanResult;

    namespace Internal {
        ScanResult ScanModule(const char* moduleName, Pattern pattern);
        // Counts matches (stops at max_count). Used to refuse to hook ambiguous patterns.
        int CountMatches(const char* moduleName, const char* pattern, int max_count);
        inline ScanResult ScanModule(const char* moduleName, const char* pattern) {
            return ScanModule(moduleName, Parse(pattern));
        }
    }
}