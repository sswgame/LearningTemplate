#include "pch.h"

#include "Engine/EngineSubsystemSet.h"

#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/CoreServices/CommandLineSubsystem.h"
#include "Engine/CoreServices/ContentRootSubsystem.h"
#include "Engine/CoreServices/CrashHandlerSubsystem.h"
#include "Engine/CoreServices/DiagnosticsSubsystem.h"
#include "Engine/CoreServices/LoggerSubsystem.h"
#include "Engine/CoreServices/MemoryBudgetSubsystem.h"
#include "Engine/CoreServices/NamePoolSubsystem.h"
#include "Engine/CoreServices/StaticStorageSubsystem.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

// 서비스 · 기동 단계 서브시스템의 헤더는 이 파일이 include 하지 않는다 — 서브시스템마다 제 .cpp 에서 등록한다(EngineSubsystemRegistration.h).
// 코어 범위(명령줄 · 로거 · 크래시 핸들러 …)는 호스트 값을 서로 넘겨 만들고 수가 적어 여기 그대로 둔다.

namespace sw::engine
{
    CoreParts addCoreSubsystems( SubsystemScope& coreScope, const CoreSetup& setup )
    {
        CoreParts parts;
        parts._pDiagnostics = &coreScope.emplaceSubsystem<DiagnosticsSubsystem>( setup._bDiagnostics );
        coreScope.emplaceSubsystem<NamePoolSubsystem>();
        parts._pCommandLine = &coreScope.emplaceSubsystem<CommandLineSubsystem>( setup._argc, setup._ppArgv );
        coreScope.emplaceSubsystem<LoggerSubsystem>( *parts._pCommandLine );
        parts._pCrashHandler = &coreScope.emplaceSubsystem<CrashHandlerSubsystem>( parts._pCommandLine );
        parts._pContentRoot  = &coreScope.emplaceSubsystem<ContentRootSubsystem>( *parts._pCommandLine, *parts._pCrashHandler );
        coreScope.emplaceSubsystem<StaticStorageSubsystem>();
        if ( setup._bMemoryBudget )
            parts._pMemoryBudget = &coreScope.emplaceSubsystem<MemoryBudgetSubsystem>( *parts._pCrashHandler );
        // 서브시스템 안의 객체도 서비스 표의 행이다 — 그 타입으로 건다(새 범위라 겹치지 않는다).
        (void)coreScope.bind( parts._pCommandLine->findCommandLine() );
        (void)coreScope.bind( parts._pCommandLine->findGlobalVariables() ); // 새 범위라 같은 타입이 걸려 있지 않다(위 설명)
        (void)coreScope.bind( &parts._pContentRoot->getAssetPaths() );      // 리소스 경로 상태 — 주인은 리소스 루트 서브시스템 하나다
        (void)coreScope.bind( &parts._pCrashHandler->getHandler() );        // 크래시 처리기 — 주인은 크래시 서브시스템 하나다
        return parts;
    }

    bool initializeCoreSubsystems( SubsystemScope& coreScope, const CoreParts& parts )
    {
        if ( coreScope.getCollection().initializeAll( SubsystemHostMask::All ) == false )
            return false;
        (void)coreScope.bind( parts._pDiagnostics->findMemoryProfiler() ); // 진단 도구를 껐으면(배포본) 없어(nullptr) 걸지 않는다 — 선택 행이다
        return true;
    }

    ServiceParts addServiceSubsystems( SubsystemScope& services )
    {
        // 초기화 · 종료할 것이 없고 수명만 있는 공용 서비스다 — 엔진 범위의 어느 것보다 먼저 서고 나중에 사라진다(누가 소멸자에서 부르든 살아 있다).
        // 무엇을 어떤 순서로 만드는지는 자리 표(`SW_ENGINE_SUBSYSTEM_SLOTS`)이고, 만드는 법은 서브시스템마다 제 .cpp 의 등록이다.
        ServiceParts         parts;
        EngineSubsystemBuild build;
        build._pServiceParts = &parts;
        buildEngineSubsystems( EngineSubsystemStage::Services, services, build );
        return parts;
    }

    StartupParts addStartupSubsystems( SubsystemScope& startup, StartupSetup&& setup )
    {
        // 단계 객체 · 본문 델리게이트의 할당은 엔진 기타 줄로 센다(만드는 객체는 제 용도 태그를 다시 건다).
        StartupParts         parts;
        EngineSubsystemBuild build;
        build._pStartupSetup = &setup;
        build._pStartupParts = &parts;
        buildEngineSubsystems( EngineSubsystemStage::Startup, startup, build );
        return parts;
    }
} // namespace sw::engine
