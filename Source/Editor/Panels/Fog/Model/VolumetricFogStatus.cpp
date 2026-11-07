#include "pch.h"

#include "Editor/Panels/Fog/Model/VolumetricFogStatus.h"

#include "Engine/Localization/LocText.h"

namespace sw::editor
{
    const utf8* VolumetricFogStatus::describe( FogVolumeState state )
    {
        switch ( state )
        {
            case FogVolumeState::NoFog:
                return SW_LOCKEY( "EditorVolumetricFog", "StateNoFog" );
            case FogVolumeState::Disabled:
                return SW_LOCKEY( "EditorVolumetricFog", "StateDisabled" );
            case FogVolumeState::Underwater:
                return SW_LOCKEY( "EditorVolumetricFog", "StateUnderwater" );
            case FogVolumeState::NoDevice:
                return SW_LOCKEY( "EditorVolumetricFog", "StateNoDevice" );
            case FogVolumeState::MissingPipeline:
                return SW_LOCKEY( "EditorVolumetricFog", "StateMissingPipeline" );
            case FogVolumeState::BufferFailed:
                return SW_LOCKEY( "EditorVolumetricFog", "StateBufferFailed" );
            case FogVolumeState::Active:
                return SW_LOCKEY( "EditorVolumetricFog", "StateActive" );
        }
        return "";
    }

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
