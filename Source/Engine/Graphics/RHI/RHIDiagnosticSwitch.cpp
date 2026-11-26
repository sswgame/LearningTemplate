#include "pch.h"

#include "Engine/Graphics/RHI/RHIDiagnosticSwitch.h"

namespace sw
{
    expected<bool> resolveRHIDiagnosticSwitch( RHIDiagnosticSwitch configValue, CommandLineArgument argument, const CommandLine* pCommandLine, RHIDiagnosticBuild build )
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
                return unexpected( Error{ ErrorKind::InvalidArgument, "the EngineConfig _rhi diagnostic switch is not set (Off, On, DebugBuildOnly or NonShippingOnly)",
                                          getCommandLineArgumentSpelling( argument ) } );
        }
        // 인자를 적지 않았으면 getArgument 가 false 를 돌려준다(ArgumentList 의 기본값을 쓰지 않는 줄) — 그때만 설정값이다.
        bool bCommandLineValue = false;
        if ( pCommandLine != nullptr && pCommandLine->getArgument( argument, bCommandLineValue ) )
            return bCommandLineValue;
        return bConfigValue;
    }

    expected<RHIDiagnosticSwitches> resolveRHIDiagnosticSwitches( const RHIConfig& config, const CommandLine* pCommandLine, RHIDiagnosticBuild build )
    {
        const expected<bool> validation  = resolveRHIDiagnosticSwitch( config._validationLayer, CommandLineArgument::RHI_VALIDATION, pCommandLine, build );
        const expected<bool> dred        = resolveRHIDiagnosticSwitch( config._dred, CommandLineArgument::RHI_DRED, pCommandLine, build );
        const expected<bool> breadcrumbs = resolveRHIDiagnosticSwitch( config._gpuBreadcrumbs, CommandLineArgument::RHI_BREADCRUMBS, pCommandLine, build );
        for ( const expected<bool>* pResult : { &validation, &dred, &breadcrumbs } )
        {
            if ( pResult->has_value() == false )
                return unexpected( pResult->error() );
        }
        RHIDiagnosticSwitches switches{};
        switches._bValidationLayer = build != RHIDiagnosticBuild::Shipping && *validation;
        switches._bDRED            = *dred;
        switches._bGPUBreadcrumbs  = *breadcrumbs;
        return switches;
    }
} // namespace sw
