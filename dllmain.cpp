#include "ModSettings.h"

#include "menu.h"
#include "toolkit.h"
#include "crash.h"

#include "Native.h"

#include "utils.h"
#include "logging.h"

#include <Windows.h>

// Sync's Toolkit, made by Sync.

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        Native::Init();
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void script_enable() {
    try {
        crash::context = "script_enable";
        tk::Start();
        ModSettings::RegisterSaveCallback([]() { tk::SaveSettings(); });
        ModSettings::RegisterMenu<ToolkitMenu>();
    } catch (...) {
        // A failure here must never take the game down; the menu just will not appear.
    }
}
