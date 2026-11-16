#include "pch.h"

#include "DevTools/DevToolsHost.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "DevTools/AssetReload/ConfigReloadSubsystem.h"
#include "DevTools/AssetReload/ContentFolderWatchService.h"
#include "DevTools/AssetReload/ReloadTracker.h"
#include "DevTools/Console/DevConsoleHost.h"
#include "DevTools/DevToolsClientPart.h"
#include "DevTools/Diagnostics/ReflectionDump.h"
#include "DevTools/Diagnostics/StateHashRecorder.h"
#include "DevTools/File/FileReadDiagnostics.h"
#include "DevTools/Maintenance/SavedRetentionSubsystem.h"
#include "DevTools/Net/NetLineEmulation.h"
#include "DevTools/Net/NetListenLaunch.h"
#include "DevTools/Profiling/FrameProfileSession.h"
#include "DevTools/Profiling/LeakBaselineDetailSwitch.h"
#include "DevTools/Profiling/ProfilerLaunchSubsystem.h"
#include "DevTools/Profiling/ProfilerTraceExport.h"
#include "DevTools/RenderDoc/RenderDocCapture.h"
#include "DevTools/ServerPose/ServerPoseOverlay.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Engine.h"
#include "Engine/EngineSubsystemSet.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    /**
     * @brief `-gv_userSettingsFile=<경로>`: 사용자 설정 파일 자리입니다(비면 사용자 폴더의 usersettings.json, 무인 실행은 실행마다 새 자리).
     * @details 실행 짝(`Scripts/net/lockstep_pair_run.py`) · 순서 시나리오(`Test/Scenario/RunStateDirPair.cmake`)가 한 파일을 이어 쓰거나 사람의 파일을 피할 때 쓴다.
     *          배포본에는 없다 — 배포본의 자리를 가를 때는 환경 변수 `SW_USER_DATA_ROOT`(`Core/File/UserDataPath.h`)다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_userSettingsFile, "", "사용자 설정 파일 경로 (비면 사용자 폴더의 usersettings.json)" );

    DevToolsHost::DevToolsHost()
        : _pConsole{}
        , _pContentFolderWatch{ make_unique<ContentFolderWatchService>() }
        , _pFileReadDiagnostics{ make_unique<FileReadDiagnostics>() }
        , _frameStall{}
    {
        LeakBaselineDetailSwitch::install(); // 기동 기준선(엔진 초기화 끝 · 헤드리스 진입)보다 먼저다 — 호스트 범위가 엔진보다 먼저 선다
    }

    DevToolsHost::~DevToolsHost()
    {
        LeakBaselineDetailSwitch::uninstall();
    }

    EngineStartupExtensionDelegate DevToolsHost::makeStartupExtension()
    {
        return SW_DELEGATE_METHOD( EngineStartupExtensionDelegate, &DevToolsHost::addStartupSubsystems, this );
    }

    EngineFrameHooks DevToolsHost::makeFrameHooks()
    {
        EngineFrameHooks hooks{};
        hooks._onFrameBegin     = SW_DELEGATE_METHOD( EngineFrameDelegate, &DevToolsHost::onFrameBegin, this );
        hooks._onAfterSceneTick = SW_DELEGATE_METHOD( EngineFrameDelegate, &DevToolsHost::onAfterSceneTick, this );
        hooks._onFrameEnd       = SW_DELEGATE_METHOD( EngineFrameExitDelegate, &DevToolsHost::onFrameEnd, this );
        return hooks;
    }

    void DevToolsHost::startConsole( bool bEnableEditor )
    {
        stopConsole();
        _pConsole = make_unique<DevConsoleHost>();
        _pConsole->initialize( bEnableEditor );
    }

    void DevToolsHost::stopConsole()
    {
        if ( _pConsole == nullptr )
            return;
        _pConsole->shutdown();
        _pConsole.reset();
    }

    void DevToolsHost::onFrameBegin( [[maybe_unused]] const EngineFrameContext& context )
    {
        if ( ProfilerLaunchSubsystem* const pProfilerSwitches = engine::findSubsystem<ProfilerLaunchSubsystem>() )
            pProfilerSwitches->onFrameBegin(); // 느린 프레임 판정의 시작 시각
        // 시작 명령(`-gv_devConsoleExec`)은 시작 씬이 열린 뒤 한 번 — 이 프레임의 입력 재생 앞이다.
        if ( _pConsole != nullptr )
            _pConsole->runPendingExec();
        ReflectionDump::runPending(); // `-gv_dumpReflection` — 첫 프레임이라 게임 · 에디터 모듈의 타입까지 등록된 뒤다
    }

    void DevToolsHost::addStartupSubsystems( SubsystemScope& startup, const engine::StartupParts& parts )
    {
        // RenderDoc — 그래픽 API 를 만들 때 끼어들므로 RHI 디바이스보다 먼저 올린다(기동 단계는 아직 초기화 전이다). `-renderdoc` 이면 설치 경로에서 로드한다.
        if ( parts._pRHI != nullptr )
        {
            const CommandLineManager* const pCommandLine = locateSubsystem<CommandLineManager>( &startup );
            RenderDocCapture::initialize( pCommandLine != nullptr && pCommandLine->isArgumentProvided( CommandLineArgument::RENDERDOC ) );
        }
        // 오래된 실행 산출물(`Saved` — 자동화 실행 · 로그 · 스크린샷 · 트레이스 …)을 자기 스레드에서 지운다.
        (void)startup.emplaceSubsystem<SavedRetentionSubsystem>();
        // 리로드 결과 집계 — 설정 · 셰이더 · 에디터 에셋 리로드가 결과를 적는다(`reload.status`). 리로드 주인보다 먼저 선다(셰이더 라이브 리로드는 클라이언트 부분 `IDevToolsClientPart` 가 세운다).
        (void)startup.emplaceSubsystem<ReloadTracker>();
        // 설정 파일 리로드는 서버에도 선다.
        if ( parts._pConfigManager != nullptr )
        {
            SW_MEMORY_SCOPE( EngineMisc );
            (void)startup.emplaceSubsystem<ConfigReloadSubsystem>( *parts._pConfigManager );
        }
        // 회선 흉내 — 세션이 열기 전에 샤드 전송 데코레이터를 건다(실행 중 조건은 `net.emulation`).
        // 리슨 포트 덮어쓰기(`-gv_netListenPort`) — 세션이 기동 역할을 정하기 전에 건다.
        if ( parts._pNetSession != nullptr )
        {
            (void)startup.emplaceSubsystem<NetLineEmulationSubsystem>( *parts._pNetSession );
            (void)startup.emplaceSubsystem<NetListenLaunchSubsystem>( *parts._pNetSession );
        }
        // 프로파일러 출력의 실행 인자(Tracy · 종료 트레이스 · 느린 프레임 캡처) — 서비스 범위의 프레임 프로파일러가 이미 섰다.
        (void)startup.emplaceSubsystem<ProfilerLaunchSubsystem>();
        // 프레임 프로파일 실행(`-gv_profileFrames` · `-gv_profileSeconds`, 부하 시나리오의 측정 창) — 프레임 끝 훅이 돌린다. GPU 메모리 표는 렌더러 창구로 묻는다(전용 서버는 없다).
        (void)startup.emplaceSubsystem<FrameProfileSession>( parts._pRenderDriver );
        // 틱별 상태 해시 이력(`-gv_stateHashHistory`) — 이력 서비스는 서비스 범위라 이미 섰다. 초기화 때 용량을 건다(서버 디버깅 S7).
        (void)startup.emplaceSubsystem<StateHashRecorder>();
        // 서버 포즈 고스트 — 서버가 가진 위치를 클라이언트 씬에 겹친다(`-gv_serverPoseOverlayPort` 가 0 이면 아무 일도 없다, 서버 디버깅 S6).
        (void)startup.emplaceSubsystem<ServerPoseOverlay>();
        // 디버그 HUD 설정 — 사용자 파일보다 먼저 읽어야 저장된 HUD 값이 버려지지 않는다(설정 매니저는 아직 초기화 전이다).
        // 사용자 설정 파일 자리(`-gv_userSettingsFile`)도 초기화 전에 정한다 — 초기화가 그 자리를 읽는다.
        if ( parts._pUserSettings != nullptr )
        {
            parts._pUserSettings->addStartupSchema( &EngineDefaultAssets::_debugHUDSettingsSchema );
            if ( gv_userSettingsFile.empty() == false )
                parts._pUserSettings->setUserFilePath( gv_userSettingsFile );
        }
        // 클라이언트 전용 부분(셰이더 라이브 리로드 · 디버그 HUD) — 전용 서버는 그 소스를 짓지 않아 창구가 없다.
        if ( IDevToolsClientPart* const pClientPart = IDevToolsClientPart::find() )
            pClientPart->addStartupSubsystems( startup, parts );
    }

    void DevToolsHost::onAfterSceneTick( [[maybe_unused]] const EngineFrameContext& context )
    {
        // 디버그 HUD — 씬 틱 뒤 · 런타임 UI 앞(HUD 화면이 UI 갱신의 틱에서 섹션을 부른다). 꺼져 있으면 디버그 키 맵 액션 하나를 읽고 끝난다.
        // 개발 명령 `profile.stall` 이 건 지연 — 엔진의 `GT.Frame` 구간 안이다(스파이크를 프레임 비용에 기대지 않고 만든다).
        _frameStall.consume();
        // 서버 포즈 고스트 — 씬 틱 · 디버그 그리기 뒤(이번 프레임에 넣은 선이 프레임 끝에 확정된다).
        if ( ServerPoseOverlay* const pServerPose = engine::findSubsystem<ServerPoseOverlay>() )
            if ( SceneManager* const pSceneManager = engine::findSubsystem<SceneManager>() )
                pServerPose->update( pSceneManager->getActiveScene(), engine::getSubsystem<DebugDrawQueue>() );
        InputMap* const pShellMap = engine::getSubsystem<DebugKeyActions>().getInputMap(); // 호스트 루프가 처음 돌리기 전에는 nullptr
        if ( IDevToolsClientPart* const pClientPart = IDevToolsClientPart::find() )
            pClientPart->onAfterSceneTick( pShellMap ); // 디버그 HUD(클라이언트 전용)
        // 게임 창 콘솔 — 호스트가 이번 프레임의 디버그 키 액션을 갱신한 뒤다(App 루프가 `beginFrame` 뒤에 갱신한다).
        if ( _pConsole != nullptr )
            _pConsole->update( pShellMap );
    }

    EngineFrameExit DevToolsHost::onFrameEnd( const EngineFrameContext& context )
    {
        // 긴 캡처(`profile.trace record`)의 끝 시각 확인(진행 중이 아니면 atomic 하나) — 루프가 `FrameProfiler` 프레임을 마감한 뒤다. 내보내기는 개발 도구라
        // 엔진 루프가 이름으로 부르지 않고 이 훅이 부른다(언리얼 Insights 내보내기도 Developer 쪽).
        ProfilerTraceExport::updateRecording( engine::getSubsystem<FrameProfiler>() );
        // 프레임 프로파일 측정 — 루프가 `FrameProfiler` 프레임을 마감한 뒤다. 목표를 채우면 보고하고 통과 코드로 끝낸다(창 닫힘의 관찰자 코드가 이긴다).
        EngineFrameExit exitCode = kEngineFrameRunning;
        if ( FrameProfileSession* const pProfileSession = engine::findSubsystem<FrameProfileSession>() )
            exitCode = pProfileSession->onFrameEnd( context._activeSceneID );
        if ( ProfilerLaunchSubsystem* const pProfilerSwitches = engine::findSubsystem<ProfilerLaunchSubsystem>() )
            pProfilerSwitches->onFrameEnd(); // 느린 프레임 자동 캡처(`-gv_traceSlowTickMs`)
        // 셰이더 재컴파일 · 강제 리로드 단축키(Debug 클라이언트) · 시작 인자 사진 — 클라이언트 전용 부분이 한다. 먼저 끝난 쪽의 종료 코드를 돌려준다.
        if ( IDevToolsClientPart* const pClientPart = IDevToolsClientPart::find() )
        {
            const EngineFrameExit clientExit = pClientPart->onFrameEnd();
            if ( exitCode == kEngineFrameRunning )
                exitCode = clientExit;
        }
        if ( ConfigReloadSubsystem* const pConfigReload = engine::findSubsystem<ConfigReloadSubsystem>() )
            pConfigReload->update();
        _pContentFolderWatch->update(); // 기능 · 게임이 건 데이터 폴더 감시(외형 데이터 등)
        // 부모 프로세스(에디터의 멀티 프로세스 재생)가 표준 입력으로 보낸 종료 낱말 — 정상 종료다.
        if ( _pConsole != nullptr && _pConsole->pollStandardInputCommands() )
            return ProcessExitCode::Passed;
        return exitCode;
    }
} // namespace sw
