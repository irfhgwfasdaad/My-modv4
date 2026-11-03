#pragma once

#include <string>

// Crash and exception logging for Sync's Toolkit.
//
// guard_depth is raised while the toolkit is inside one of its own guarded
// calls, so exceptions the toolkit catches itself are not reported as crashes.
// context is a short label of what the current thread was doing; it is written
// into the crash report so a crash can be traced back to a feature.
namespace crash {

inline thread_local int         guard_depth = 0;
inline thread_local const char* context     = "none";

void        Install(const std::string& dir);
int         Count();        // fatal looking exceptions seen this session
const char* ThisSession();  // one line summary of the latest one, or ""
const char* Previous();     // one line summary of the last crash from an earlier session, or ""
void        Clear();        // delete the crash files and forget everything

} // namespace crash
