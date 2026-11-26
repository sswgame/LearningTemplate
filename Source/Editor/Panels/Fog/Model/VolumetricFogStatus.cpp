#include "pch.h"

#include "Editor/Panels/Fog/Model/VolumetricFogStatus.h"

#include "Engine/Localization/LocText.h"

namespace sw::editor
{
    VolumetricFogSeverity VolumetricFogStatus::getSeverity( FogVolumeState state )
    {
        switch ( state )
        {
            case FogVolumeState::NoFog:
            case FogVolumeState::Disabled:
            case FogVolumeState::Underwater:
                return VolumetricFogSeverity::Neutral;
            case FogVolumeState::NoDevice:
            case FogVolumeState::MissingPipeline:
            case FogVolumeState::BufferFailed:
                return VolumetricFogSeverity::Problem;
            case FogVolumeState::Active:
                return VolumetricFogSeverity::Active;
        }
        return VolumetricFogSeverity::Problem;
    }
} // namespace sw::editor
