#include "pch.h"

#include "Engine/CoreServices/CrashHandlerSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/BuildInfo.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Module/ModuleBuildID.h"

#include "Engine/CoreServices/CommandLineSubsystem.h"
#include "Engine/CoreServices/LoggerSubsystem.h"

namespace sw
{
    CrashHandlerSubsystem::CrashHandlerSubsystem( const CommandLineSubsystem* pCommandLine )
        : _pCommandLine{ pCommandLine }
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
        if ( _pCommandLine != nullptr )
        {
            string reporterFolder;
            _bCrashReporterRun = _pCommandLine->findCommandLine()->getArgument( CommandLineArgument::CRASH_REPORTER, reporterFolder ) && reporterFolder.empty() == false;
            if ( _bCrashReporterRun )
                return SubsystemInitResult::Succeeded;
        }

        CrashHandler::initialize();
        _bInstalled = true;

        // 크래시 리포트에 함께 나갈 값들이다. 덤프만으로는 알 수 없는 것들이다. 게임은 리소스 루트 단계가, 백엔드는 RHI 단계가 덧붙인다.
        CrashHandler::setContextValue( "Build", build::kConfigName );
        CrashHandler::setContextValue( "Platform", build::kPlatformName );
        // 심볼과 짝짓는 열쇠 — 실행 파일과(다르면) 엔진 모듈의 빌드 id. 덤프의 모듈 목록에도 같은 값이 있다.
        const ModuleBuildID executableID = ModuleBuildID::find( nullptr );
        const ModuleBuildID engineID     = ModuleBuildID::find( reinterpret_cast<const void*>( &CrashHandler::setContextValue ) );
        CrashHandler::setContextValue( "BuildID", executableID._id );
        if ( engineID._id != executableID._id )
            CrashHandler::setContextValue( "EngineBuildID", engineID._id );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> CrashHandlerSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrAfterCommandLine[] = { CommandLineSubsystem::kSubsystemName };
        static constexpr const utf8* kArrAfterLogger[]      = { LoggerSubsystem::kSubsystemName };
        if ( _pCommandLine != nullptr )
            return kArrAfterCommandLine;
        return kArrAfterLogger;
    }
} // namespace sw
