#include "Native.h"
#include "AssetHash.h"
#include "AssetManager.h"
#include "Localization.h"
#include "Actor.h"
#include "Transform.h"
#include "HeroSystem.h"

#include "ComponentRegistry.h"

#include "logging.h"

#define INIT_NATIVE(s) \
    DEBUG(#s); \
    Native::Initializers::s::Init(); \
    DEBUG("------");

void Native::Init() {
    INIT_NATIVE(AssetHash);
    INIT_NATIVE(AssetManager);
    INIT_NATIVE(HeroSystem);
    INIT_NATIVE(Actor);
    INIT_NATIVE(Localization);
    INIT_NATIVE(ComponentRegistry);
    INIT_NATIVE(ActorUtils);
}