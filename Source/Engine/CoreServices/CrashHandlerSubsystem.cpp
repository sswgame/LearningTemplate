#include "pch.h"

#include "Engine/CoreServices/CrashHandlerSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleBuildID.h"

#include "Engine/CoreServices/CommandLineSubsystem.h"
#include "Engine/CoreServices/LoggerSubsystem.h"

namespace sw
{
    CrashHandlerSubsystem::CrashHandlerSubsystem( const CommandLineSubsystem* pCommandLine )
        : _pCommandLine{ pCommandLine }
        , _handler{}
        , _bCrashReporterRun{ SW_FALSE }
    {
    }

    CrashHandlerSubsystem::~CrashHandlerSubsystem() = default; // 처리기가 소멸자에서 스스로 뗀다

    SubsystemInitResult CrashHandlerSubsystem::initializeSubsystem()
    {
        if ( _pCommandLine != nullptr )
        {
            string reporterFolder;
            _bCrashReporterRun = _pCommandLine->findCommandLine()->getArgument( CommandLineArgument::CRASH_REPORTER, reporterFolder ) && reporterFolder.empty() == false;
            if ( _bCrashReporterRun )
                return SubsystemInitResult::Succeeded;
        }

        // 덤프 · 리포트는 로그 폴더에 둔다 — 사용자가 폴더 하나만 보내면 되도록. 로그가 고정 이름 파일(`-log-file`)이면 다음 실행의 보고 묶음이
        // 그 파일을 찾도록 문맥에 적는다(기본 로그는 세션 접미사로 찾는다 — `CrashContextStore::kLogFileKey`).
        _handler.setReportFolder( Logger::getGlobalSink()->getLogFolderPath() );
        string fixedLogFile;
        if ( _pCommandLine != nullptr && _pCommandLine->findCommandLine()->getArgument( CommandLineArgument::LOG_FILE, fixedLogFile ) && fixedLogFile.empty() == false )
            _handler.setContextValue( CrashContextStore::kLogFileKey, fixedLogFile );
        _handler.install();

        // 크래시 리포트에 함께 나갈 값들이다. 덤프만으로는 알 수 없는 것들이다. 게임은 리소스 루트 단계가, 백엔드는 RHI 단계가 덧붙인다.
        _handler.setContextValue( "Build", build::kConfigName );
        _handler.setContextValue( "Platform", build::kPlatformName );
        // 심볼과 짝짓는 열쇠 — 실행 파일과(다르면) 엔진 모듈의 빌드 id. 덤프의 모듈 목록에도 같은 값이 있다.
        const ModuleBuildID executableID = ModuleBuildID::find( nullptr );
        const ModuleBuildID engineID     = ModuleBuildID::find( reinterpret_cast<const void*>( &CrashHandler::crashForTest ) );
        _handler.setContextValue( "BuildID", executableID._id );
        if ( engineID._id != executableID._id )
            _handler.setContextValue( "EngineBuildID", engineID._id );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> CrashHandlerSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrAfterCommandLine[] = { CommandLineSubsystem::kSubsystemName, LoggerSubsystem::kSubsystemName };
        static constexpr const utf8* kArrAfterLogger[]      = { LoggerSubsystem::kSubsystemName };
        if ( _pCommandLine != nullptr )
            return kArrAfterCommandLine;
        return kArrAfterLogger;
    }
} // namespace sw
