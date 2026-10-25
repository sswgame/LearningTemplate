#include "pch.h"

#include "Engine/Config/GameConfig.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    const GameConfig& GameConfig::getActive()
    {
        return engine::getSubsystem<GameConfig>();
    }
} // namespace sw
