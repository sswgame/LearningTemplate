#include "pch.h"

#include "Engine/Graphics/RHI/RHIValidationLayer.h"

#include "Core/CommandLine/CommandLineManager.h"

namespace sw
{
    expected<bool> resolveRHIValidationLayer( RHIValidationLayerMode configMode, const CommandLineManager* pCommandLine, bool bDebugBuild )
    {
        bool bConfigValue = false;
        switch ( configMode )
        {
            case RHIValidationLayerMode::Off:
                bConfigValue = false;
                break;
            case RHIValidationLayerMode::On:
                bConfigValue = true;
                break;
            case RHIValidationLayerMode::DebugBuildOnly:
                bConfigValue = bDebugBuild;
                break;
            case RHIValidationLayerMode::None:
            default:
                return unexpected( Error{ ErrorKind::InvalidArgument, "EngineConfig _rhi._validationLayer is not set (Off, On or DebugBuildOnly)" } );
        }
        // 인자를 적지 않았으면 getArgument 가 false 를 돌려준다(ArgumentList 의 기본값을 쓰지 않는 줄) — 그때만 설정값이다.
        bool bCommandLineValue = false;
        if ( pCommandLine != nullptr && pCommandLine->getArgument( CommandLineArgument::RHI_VALIDATION, bCommandLineValue ) )
            return bCommandLineValue;
        return bConfigValue;
    }
} // namespace sw
