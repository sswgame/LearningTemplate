#include "pch.h"

#include "Engine/Config/GameConfig.h"

namespace sw
{
    namespace
    {
        GameConfig s_activeGameConfig{};
    } // namespace

    void GameConfig::setActive( const GameConfig& config )
    {
        s_activeGameConfig = config;
    }

    const GameConfig& GameConfig::getActive()
    {
        return s_activeGameConfig;
    }
} // namespace sw
