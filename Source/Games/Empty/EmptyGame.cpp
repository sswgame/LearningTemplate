#include "pch.h"

#include "Games/Empty/EmptyGame.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    void EmptyGame::configureBootstrap( BootstrapConfig& outConfig )
    {
        outConfig._packRoot = "game/empty";
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::EmptyGame );
