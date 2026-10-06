#include "pch.h"

#include "Engine/Config/EngineConfig.h"

namespace sw
{
    namespace
    {
        EngineConfig s_activeEngineConfig{};
    } // namespace

    void EngineConfig::setActive( const EngineConfig& config )
    {
        s_activeEngineConfig = config;
    }

    const EngineConfig& EngineConfig::getActive()
    {
        return s_activeEngineConfig;
    }
} // namespace sw
