#include "pch.h"

#include "Engine/CoreServices/ContentRootSubsystem.h"

#include "Core/Diagnostics/CrashHandler.h"

#include "Engine/Config/GameSelection.h"
#include "Engine/CoreServices/CommandLineSubsystem.h"
#include "Engine/CoreServices/CrashHandlerSubsystem.h"

namespace sw
{
    SW_LOG_CALLER( "ContentRootSubsystem" );

    ContentRootSubsystem::ContentRootSubsystem( const CommandLineSubsystem& commandLine, const CrashHandlerSubsystem& crashHandler )
        : _commandLine{ commandLine }
        , _crashHandler{ crashHandler }
        , _assetPaths{}
    {
    }

    SubsystemInitResult ContentRootSubsystem::initializeSubsystem()
    {
        if ( _crashHandler.isCrashReporterRun() )
            return SubsystemInitResult::Succeeded;
        if ( _assetPaths.initialize() == false )
        {
            SW_LOG_ERROR( "Content root not found - run from a location that contains Content/." );
            return SubsystemInitResult::Failed;
        }
        // 돌릴 게임을 고른다(Dev `-game=`) — 모듈 카탈로그 · 게임 프리셋을 읽기 전이어야 한다. 이 빌드에 없는 게임이면 서지 않는다.
        if ( GameSelection::select( *_commandLine.findCommandLine() ) == false )
            return SubsystemInitResult::Failed;
        CrashHandler::setContextValue( "Game", GameSelection::getName().c_str() );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> ContentRootSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { CrashHandlerSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
