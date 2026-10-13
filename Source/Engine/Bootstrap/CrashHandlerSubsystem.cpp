#include "pch.h"

#include "Engine/Bootstrap/CrashHandlerSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/BuildInfo.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Module/ModuleBuildID.h"

#include "Engine/Bootstrap/CommandLineSubsystem.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw
{
    SW_LOG_CALLER( "CrashHandlerSubsystem" );

    CrashHandlerSubsystem::CrashHandlerSubsystem( const CommandLineSubsystem& commandLine )
        : _commandLine{ commandLine }
        , _bInstalled{ false }
        , _bCrashReporterRun{ false }
    {
    }

    CrashHandlerSubsystem::~CrashHandlerSubsystem()
    {
        if ( _bInstalled )
            CrashHandler::shutdown();
    }

    SubsystemInitResult CrashHandlerSubsystem::initializeSubsystem()
    {
        const CommandLineManager& commandLine = *_commandLine.findCommandLine();
        string                    reporterFolder;
        _bCrashReporterRun = commandLine.getArgument( CommandLineArgument::CRASH_REPORTER, reporterFolder ) && reporterFolder.empty() == false;
        if ( _bCrashReporterRun )
            return SubsystemInitResult::Succeeded;

        CrashHandler::initialize();
        _bInstalled = true;

        if ( ResourcePaths::initialize() == false )
        {
            SW_LOG_ERROR( "리소스 루트를 찾지 못했습니다 — Resource/ 가 있는 위치에서 실행하십시오." );
            return SubsystemInitResult::Failed;
        }
        // 돌릴 게임을 고른다(Dev `-game=`) — 모듈 카탈로그 · 게임 프리셋을 읽기 전이어야 한다. 이 빌드에 없는 게임이면 서지 않는다.
        if ( GameSelection::select( commandLine ) == false )
            return SubsystemInitResult::Failed;

        // 크래시 리포트에 함께 나갈 값들이다. 덤프만으로는 알 수 없는 것들이다. 백엔드는 RHI 단계가 덮어쓴다.
        CrashHandler::setContextValue( "Game", GameSelection::getName().c_str() );
        CrashHandler::setContextValue( "Build", build::kConfigName );
        CrashHandler::setContextValue( "Platform", build::kPlatformName );
        // 심볼과 짝짓는 열쇠 — 실행 파일과(다르면) 엔진 모듈의 빌드 id. 덤프의 모듈 목록에도 같은 값이 있다.
        const ModuleBuildID executableID = ModuleBuildID::find( nullptr );
        const ModuleBuildID engineID     = ModuleBuildID::find( reinterpret_cast<const void*>( &CrashHandler::setContextValue ) );
        CrashHandler::setContextValue( "BuildId", executableID._id );
        if ( engineID._id != executableID._id )
            CrashHandler::setContextValue( "EngineBuildId", engineID._id );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> CrashHandlerSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { CommandLineSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
