#include "pch.h"

#include "Engine/CoreServices/ContentRootSubsystem.h"

#include "Engine/Config/GameSelection.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/CoreServices/CommandLineSubsystem.h"
#include "Engine/CoreServices/CrashHandlerSubsystem.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    ContentRootSubsystem::ContentRootSubsystem( const CommandLineSubsystem& commandLine, CrashHandlerSubsystem& crashHandler )
        : _commandLine{ commandLine }
        , _crashHandler{ crashHandler }
        , _assetPaths{}
        , _resourceComposition{}
        , _resourceTags{ _resourceComposition }
    {
    }

    SubsystemInitResult ContentRootSubsystem::initializeSubsystem()
    {
        if ( _crashHandler.isCrashReporterRun() )
            return SubsystemInitResult::Succeeded;
        if ( _assetPaths.initialize() == false )
        {
            SW_LOG_ERROR_ID( "ContentRootSubsystem.ContentRootNotFound", "Content root not found - run from a location that contains Content/." );
            return SubsystemInitResult::Failed;
        }
        // 돌릴 게임을 고른다(Dev `-game=`) — 모듈 카탈로그 · 게임 프리셋을 읽기 전이어야 한다. 이 빌드에 없는 게임이면 서지 않는다.
        if ( GameSelection::select( *_commandLine.findCommandLine() ) == false )
            return SubsystemInitResult::Failed;
        _crashHandler.getHandler().setContextValue( "Game", GameSelection::getName().c_str() );
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> ContentRootSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { CrashHandlerSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw

namespace sw
{
    // 이 서브시스템이 든 리소스 경로 상태의 서비스 칸(경로 표 · 검색 우선순위 · 붙인 디스크 도메인 · 해석 캐시). 워커(디코드 · 쿠킹)도 경로를 풀므로 칸으로 읽는다.
    SW_ENGINE_SERVICE( AssetPaths, GameVisible );
    // 리소스 조건 태그(게임 코드가 `flag` · `feature` 축을 켠다)와 합성 기록 · 덮어쓰기 층(모드 세션이 층을 얹는다). 로드 워커도 읽으므로 칸으로 읽는다.
    SW_ENGINE_SERVICE( ResourceTags, GameVisible );
    SW_ENGINE_SERVICE( ResourceComposition, GameVisible );
} // namespace sw
