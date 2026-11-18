#include "pch.h"

#include "Engine/Graphics/RHI/RHIDiagnosticSwitch.h"

namespace sw
{
    expected<bool> resolveRHIDiagnosticSwitch( RHIDiagnosticSwitch configValue, CommandLineArgument argument, const CommandLineManager* pCommandLine, RHIDiagnosticBuild build )
    {
        bool bConfigValue = false;
        switch ( configValue )
        {
            case RHIDiagnosticSwitch::Off:
                bConfigValue = false;
                break;
            case RHIDiagnosticSwitch::On:
                bConfigValue = true;
                break;
            case RHIDiagnosticSwitch::DebugBuildOnly:
                bConfigValue = build == RHIDiagnosticBuild::Debug;
                break;
            case RHIDiagnosticSwitch::NonShippingOnly:
                bConfigValue = build != RHIDiagnosticBuild::Shipping;
                break;
            case RHIDiagnosticSwitch::None:
            default:
                return unexpected( Error{ ErrorKind::InvalidArgument, "the EngineConfig _rhi diagnostic switch is not set (Off, On, DebugBuildOnly or NonShippingOnly)",
                                          getCommandLineArgumentSpelling( argument ) } );
        }
        // 인자를 적지 않았으면 getArgument 가 false 를 돌려준다(ArgumentList 의 기본값을 쓰지 않는 줄) — 그때만 설정값이다.
        bool bCommandLineValue = false;
        if ( pCommandLine != nullptr && pCommandLine->getArgument( argument, bCommandLineValue ) )
            return bCommandLineValue;
        return bConfigValue;
    }
} // namespace sw
