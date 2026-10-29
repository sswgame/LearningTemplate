#include "pch.h"

#include "DevTools/DevToolsHost.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "DevTools/AssetReload/ConfigReloadSubsystem.h"
#include "DevTools/AssetReload/ContentFolderWatchService.h"
#include "DevTools/Console/DevConsoleHost.h"
#include "DevTools/DebugHUD/DebugHUD.h"
#include "DevTools/Net/NetLineEmulation.h"
#include "DevTools/Profiling/FrameStall.h"
#include "DevTools/RenderDoc/RenderDocCapture.h"
#include "DevTools/ServerPose/ServerPoseOverlay.h"
#include "DevTools/Shader/LiveShaderSubsystem.h"
#include "DevTools/Shader/ShaderRecompiler.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Engine.h"
#include "Engine/EngineSubsystemSet.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    SW_LOG_CALLER( "DevTools" );

    DevToolsHost::DevToolsHost()
        : _pConsole{}
        , _pContentFolderWatch{ make_unique<ContentFolderWatchService>() }
    {
    }

    DevToolsHost::~DevToolsHost() = default;

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
        // 시작 명령(`-gv_devConsoleExec`)은 시작 씬이 열린 뒤 한 번 — 이 프레임의 입력 재생 앞이다.
        if ( _pConsole != nullptr )
            _pConsole->runPendingExec();
    }

    void DevToolsHost::addStartupSubsystems( SubsystemScope& startup, const engine::StartupParts& parts )
    {
        // RenderDoc — 그래픽 API 를 만들 때 끼어들므로 RHI 디바이스보다 먼저 올린다(기동 단계는 아직 초기화 전이다). `-renderdoc` 이면 설치 경로에서 로드한다.
        if ( parts._pRHI != nullptr )
        {
            const CommandLineManager* const pCommandLine = locateSubsystem<CommandLineManager>( &startup );
            RenderDocCapture::initialize( pCommandLine != nullptr && pCommandLine->isArgumentProvided( CommandLineArgument::RENDERDOC ) );
        }
        // 셰이더 라이브 리로드는 렌더러가 있는 호스트만(전용 서버 · 렌더러를 끈 기동은 렌더 쪽이 nullptr).
        if ( parts._pFrameRendererSubsystem != nullptr && parts._pRenderThread != nullptr )
            (void)startup.addSubsystem( sw::make_unique<LiveShaderSubsystem>( *parts._pFrameRendererSubsystem, *parts._pRenderThread ) );
        // 설정 파일 리로드는 서버에도 선다.
        if ( parts._pConfigManager != nullptr )
        {
            SW_MEMORY_SCOPE( EngineMisc );
            (void)startup.addSubsystem( sw::make_unique<ConfigReloadSubsystem>( *parts._pConfigManager ) );
        }
        // 회선 흉내 — 세션이 열기 전에 샤드 전송 데코레이터를 건다(실행 중 조건은 `net.emulation`).
        if ( parts._pNetSession != nullptr )
            (void)startup.addSubsystem( sw::make_unique<NetLineEmulationSubsystem>( *parts._pNetSession ) );
        // 서버 포즈 고스트 — 서버가 가진 위치를 클라이언트 씬에 겹친다(`-gv_serverPoseOverlayPort` 가 0 이면 아무 일도 없다, 서버 디버깅 S6).
        (void)startup.addSubsystem( sw::make_unique<ServerPoseOverlay>() );
        // 디버그 HUD 설정 — 사용자 파일보다 먼저 읽어야 저장된 HUD 값이 버려지지 않는다(설정 매니저는 아직 초기화 전이다).
        if ( parts._pUserSettings != nullptr )
            parts._pUserSettings->addStartupSchema( &EngineDefaultAssets::_debugHUDSettingsSchema );
        // 디버그 HUD 는 런타임 UI 위에 그린다 — UI 가 있는 실행만.
        if ( parts._pUISystem != nullptr )
            (void)startup.addSubsystem( sw::make_unique<DebugHUD>( *parts._pUISystem, parts._pUserSettings ) );
    }

    void DevToolsHost::onAfterSceneTick( [[maybe_unused]] const EngineFrameContext& context )
    {
        // 디버그 HUD — 씬 틱 뒤 · 런타임 UI 앞(HUD 화면이 UI 갱신의 틱에서 섹션을 부른다). 꺼져 있으면 디버그 키 맵 액션 하나를 읽고 끝난다.
        // 개발 명령 `profile.stall` 이 건 지연 — 엔진의 `GT.Frame` 구간 안이다(스파이크를 프레임 비용에 기대지 않고 만든다).
        FrameStall::consume();
        // 서버 포즈 고스트 — 씬 틱 · 디버그 그리기 뒤(이번 프레임에 넣은 선이 프레임 끝에 확정된다).
        if ( ServerPoseOverlay* const pServerPose = engine::findSubsystem<ServerPoseOverlay>() )
            if ( SceneManager* const pSceneManager = engine::findSubsystem<SceneManager>() )
                pServerPose->update( pSceneManager->getActiveScene(), engine::getSubsystem<DebugDrawQueue>() );
        InputMap* const pShellMap = engine::getSubsystem<DebugKeyActions>().getInputMap(); // 호스트 루프가 처음 돌리기 전에는 nullptr
        if ( DebugHUD* const pDebugHUD = engine::findSubsystem<DebugHUD>() )
            pDebugHUD->update( pShellMap );
        // 게임 창 콘솔 — 호스트가 이번 프레임의 디버그 키 액션을 갱신한 뒤다(App 루프가 `beginFrame` 뒤에 갱신한다).
        if ( _pConsole != nullptr )
            _pConsole->update( pShellMap );
    }

    int32 DevToolsHost::onFrameEnd( [[maybe_unused]] const EngineFrameContext& context )
    {
#if defined( SW_DEBUG )
        // 셰이더 재컴파일 · 강제 리로드 단축키(`ReloadShaders`)는 Debug 클라이언트만 — 단축키의 뜻은 재컴파일기를 가진 이쪽이 읽는다(디버그 키 맵은 입력 사실만 낸다).
        LiveShaderSubsystem* const pLiveShader = engine::findSubsystem<LiveShaderSubsystem>();
        if ( ShaderRecompiler* const pRecompiler = pLiveShader != nullptr ? pLiveShader->findShaderRecompiler() : nullptr )
        {
            pRecompiler->update();
            if ( engine::getSubsystem<DebugKeyActions>().wasActionTriggered( hashed_string( InputMapDefaults::kReloadShadersAction ) ) )
            {
                pRecompiler->triggerReloadAll();
                SW_LOG_INFO( "%#: force shader reload", InputMapDefaults::kReloadShadersAction );
            }
        }
#endif
        if ( ConfigReloadSubsystem* const pConfigReload = engine::findSubsystem<ConfigReloadSubsystem>() )
            pConfigReload->update();
        _pContentFolderWatch->update(); // 기능 · 게임이 건 데이터 폴더 감시(외형 데이터 등)
        // 부모 프로세스(에디터의 멀티 프로세스 재생)가 표준 입력으로 보낸 종료 낱말 — 정상 종료다.
        if ( _pConsole != nullptr && _pConsole->pollStandardInputCommands() )
            return 0;
        return kEngineFrameRunning;
    }
} // namespace sw
