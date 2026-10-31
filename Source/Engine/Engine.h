/**
 * @file Engine.h
 * @brief 엔진 코어의 메인 루프와 서브시스템 소유권을 관리합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/EngineFrameHookList.h"
#include "Engine/EngineStartupExtension.h"
#include "Engine/FileSystem/AssetID.h"
#include "Engine/Foundation/QuitRequest.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/HeadlessSubsystem.h"
#include "Engine/Profiling/FrameProfileSession.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"
#include "Engine/Renderer/Frame/View/RenderView.h"

namespace sw
{
    struct EngineConfig;
    struct EngineDefaultAssets;
    struct EngineFrameContext;

    class AssetManager;
    class AssetStreamingQueue;
    class CameraComponent;
    class CommandLineManager;
    class CommandLineSubsystem;
    class CompressionCodecRegistry;
    class ConfigManager;
    class DebugDrawQueue;
    class DebugKeyActions;
    class EventDispatcher;
    class FontService;
    class FrameProfiler;
    class FrameRenderer;
    class GlobalVariableManager;
    class IAudioService;
    class InputManager;
    class InputMap;
    class IRHIDevice;
    class MemoryBudgetSubsystem;
    class RenderTargetRegistry;
    class RenderThread;
    class RHI;
    class RHIBackendRegistry;
    class RHISubsystem;
    class SceneManager;
    class TaskManager;
    class TypeRegistry;
    class UISystem;

    // `ViewCameraProviderDelegate`(씬 뷰 카메라 조회)는 그것을 부르는 패킷 시점 헤더(`RenderPacketViewSetup.h`, 패킷 빌더 헤더가 끌어온다)에 있습니다.

    /** @brief 엔진을 띄우는 호스트의 역할입니다 — 기동 표의 대상(`SubsystemHostMask`) · 오디오 장치 · 허용하는 헤드리스 작업을 가릅니다. */
    enum class EngineHostRole : uint8
    {
        Client,          ///< App — 창 · 렌더러 · 플레이어 설정(에디터 포함)
        DedicatedServer, ///< Server — 창 · GPU · 오디오 장치 · 플레이어 설정 없음
    };

    /**
     * @class Engine
     * @brief 코어 매니저들을 소유하고 메인 루프(tick)를 돌립니다.
     */
    class SW_API Engine
    {
    public:
        Engine();
        ~Engine();

        Engine( const Engine& )            = delete;
        Engine& operator=( const Engine& ) = delete;

        /**
         * @brief 서브시스템(창, RHI 포함)을 초기화합니다.
         * @details @p role 이 `DedicatedServer` 면 기동 표의 `Client` 단계(창 · RHI · 렌더러 · 플레이어 설정)를 돌리지 않고 장치 없는 오디오를 씁니다.
         *          이 빌드에 없는 역할이면(Client 빌드의 전용 서버, Server 빌드의 클라이언트) 실패합니다.
         * @param pHostScope 호스트 범위입니다 — 코어 범위의 부모이고, 엔진의 `ModuleLoadStage` · `ModuleUnloadStage` 단계가 여기서 `IModuleLoader` 를 찾습니다.
         *                   범위 객체는 엔진보다 오래 살아야 합니다. 그 안의 서브시스템은 `shutdown` 이 내립니다. nullptr 이면 호스트가 내준 것이 없습니다(테스트 하네스).
         *                   이 범위가 **프로세스 범위**(범위 사슬의 뿌리)라, 모듈 서브시스템(`SW_MODULE_SUBSYSTEM( Process, … )`)이 여기에 모듈 이름의 자식 범위로 선다 —
         *                   그래서 쓰기 가능한 범위로 받는다. 없으면 코어 범위가 뿌리다.
         */
        [[nodiscard]] bool initialize( int32 argc, utf8* pArgv[], EngineHostRole role = EngineHostRole::Client, SubsystemScope* pHostScope = nullptr );
        /**
         * @brief 매니저들을 종료하고 정리합니다.
         * @param hostScope `initialize` 에 넘긴 호스트 범위입니다. 엔진 범위(기동 · 서비스)를 내린 뒤, 코어 범위보다 **먼저** 비웁니다(`destroyAll`).
         * @details 코어의 진단 서브시스템이 소멸하며 종료 누수 창을 닫습니다. 호스트 범위는 기동 뒤에 칸 · 부품 · 종료 순서를 키우므로(`bind`,
         *          `shutdownDependentsOf`), 창보다 늦게 비우면 그 저장소가 "창 안에서 태어나 살아 있는 블록" 으로 보고됩니다. 호스트의 서브시스템도
         *          로거 · 이름 풀(코어)이 아직 있을 때 소멸합니다.
         */
        void shutdown( SubsystemScope& hostScope );

        /**
         * @brief 입력 등을 시작하는 프레임의 첫 단계입니다.
         * @param deltaSeconds 지난 프레임의 실제 시간(초). 입력 장치의 타이머(진동 길이 · 게임패드 재연결 주기)가 이 값으로 흐릅니다.
         *                     고정값으로 흘리면 0.3 초 진동이 144 fps 에서 0.13 초로 끝납니다.
         * @param unscaledDeltaSeconds 이번 프레임의 실제 경과(배율 · 정지와 무관). `GameTimeScale::setUnscaledDeltaTime` 으로 적어
         *                             정지 메뉴 · 페이드 · 에디터 도구가 읽습니다(`GameTimeScale::getUnscaledDeltaTime`).
         */
        void beginFrame( float32 deltaSeconds, float32 unscaledDeltaSeconds );
        /**
         * @brief 씬 업데이트, RHI 제출 등을 수행합니다.
         * @param deltaTime 델타 타임
         * @param views 호스트가 요청한 오프스크린 출력(에디터 게임 뷰 · 씬 뷰). 둘 다 비면 백버퍼에 게임 카메라로 그립니다.
         * @param sceneViewCameraProvider 씬 뷰를 그리는 호스트 카메라(에디터 카메라)를 돌려주는 델리게이트. 씬 뷰가 없으면 부르지 않습니다.
         * @param bTickScene false이면 씬 GameObject tick을 건너뜁니다 (에디터 Pause).
         */
        void update( float32 deltaTime, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider, bool bTickScene );
        /** @brief 입력 종료 등 프레임의 마지막 단계입니다. */
        void endFrame();

        /** @brief 대기 중인 RHI 핫스왑을 수행합니다. 렌더러 몫(이것 · 창 크기 · 렌더 패킷)의 구현은 `EngineRendering.cpp`, 전용 서버는 `EngineRenderingServer.cpp` 입니다. */
        [[nodiscard]] bool applyPendingBackendChange();

        // ----------------------------------------------------------------------
        // 도우미
        // ----------------------------------------------------------------------
        /**
         * @brief 프레임 훅 목록입니다 — 붙이는 쪽(App · 서버 · 개발 도구)이 `add` 로 훅 묶음을 붙인다. 루프는 훅이 무엇인지 모른다(자동화 시나리오 · 텔레메트리 · 개발 도구).
         * @details 기동이 어디서 멈췄든(크래시 보고 프로세스 포함) 엔진과 수명이 같다. `shutdown` 은 남은 구독을 모두 끊는다.
         */
        EngineFrameHookList& getFrameHooks() { return _frameHooks; }
        /** @brief 기동 확장을 겁니다(`initialize` 전에 — 기동 단계 범위를 채운 뒤 · 초기화 전에 한 번 부른다). 대상의 수명은 건 쪽이 엔진 종료까지 쥔다. */
        void setStartupExtension( const EngineStartupExtensionDelegate& extension ) { _startupExtension = extension; }

        /**
         * @brief 엔진이 스스로 종료를 원하면 true 입니다(`-gv_profileFrames=N` 을 다 채웠거나 `requestQuit` 이 불렸다).
         * @details 창 수명은 App 이 쥐고 있으므로 여기서는 의사만 알립니다.
         */
        bool isQuitRequested() const { return _bQuitRequested || _profileSession.isQuitRequested() || ( _pQuitRequest != nullptr && _pQuitRequest->isRequested() ); }
        /** @brief `-gv_profileFrames` · `-gv_profileSeconds` 계측이 시작됐으면 true 입니다(앱 루프의 프레임 페이싱이 읽는다). */
        bool isProfileRunActive() const { return _profileSession.isActive(); }
        /** @brief 이번 프레임이 끝나면 루프를 끝내라고 요청합니다(호스트). 모듈은 서비스 `QuitRequest` 에 직접 건다 — 이것도 그리로 간다. 여럿이면 처음 것의 코드를 씁니다. */
        void requestQuit( int32 exitCode );
        /** @brief App 이 돌려줄 종료 코드입니다(요청이 없었으면 0). */
        int32 getExitCode() const { return _bQuitRequested == false && _pQuitRequest != nullptr && _pQuitRequest->isRequested() ? _pQuitRequest->getExitCode() : _exitCode; }
        /**
         * @brief 창이 닫혀 루프가 끝났다고 알립니다(App 이 루프 뒤에 부른다). 프레임 관찰자가 종료 코드를 내면 그 코드로 정합니다
         *        (관찰자의 결과가 프로파일 세션의 종료보다 이긴다).
         */
        void onWindowClosed();
        // 셸 디버그 키 맵은 서비스 범위의 서브시스템이다(`engine::getSubsystem<DebugKeyActions>()`) — 호스트 루프가 돌리고 리로드 단축키 · 개발 도구가 읽는다.

        // ----------------------------------------------------------------------
        // Getter (App 이 GameModuleManager 등과 연동하는 데 필요)
        // ----------------------------------------------------------------------
        ConfigManager* getConfigManager() const { return _pConfigManager; }
        /** @brief 코어 범위의 명령줄입니다. 기동 전 · 종료 뒤에는 nullptr 입니다. */
        CommandLineManager* getCommandLineManager() const;
        RHI*                getRHI() const { return _pRHI; }
        RenderThread*       getRenderThread() const { return _pRenderThread; }
        /**
         * @brief 서비스 · 기동 범위의 서브시스템 초기화 시간을 오래 걸린 것부터 "이름=ms" 로 이은 글입니다(1 ms 미만은 뺀다). 시동 요약 로그의 재료입니다.
         * @details `SubsystemCollection::getInitTimings` 를 모은 것이라 `initialize` 가 끝난 뒤에만 뜻이 있습니다.
         */
        [[nodiscard]] string describeSubsystemInitTimings() const;
        bool                 isHeadless() const { return _headlessState.isHeadless(); }
        /** @brief 이 엔진을 띄운 호스트의 역할입니다(`initialize` 의 인자). */
        EngineHostRole getHostRole() const { return _hostRole; }
        /** @brief 헤드리스 작업(셰이더 쿠킹 · 씬 쿠킹)이 실패했는지 반환합니다. 부르는 쪽은 이것을 종료 코드로 내보냅니다. */
        bool didHeadlessTaskFail() const { return _headlessState._bFailed; }
        /** @brief 실패한 헤드리스 작업이 정한 종료 코드입니다(정하지 않았으면 -1). */
        int32 getHeadlessFailureExitCode() const { return _headlessState._failureExitCode; }
        /** @brief RHI 기동 단계의 결과입니다. 그 단계가 돌지 않았으면(헤드리스 · 앞 단계 실패) `NotStarted` 입니다. App 이 기동 실패의 종료 코드를 고를 때 읽습니다. */
        RHIInitResult getRHIInitResult() const { return _rhiInitResult; }

    private:
        /**
         * @brief 디바이스 재생성 뒤 내렸던 단계(렌더러 · 렌더 스레드 · 라이브 셰이더 · 씬의 디바이스)를 다시 세웁니다. 모두 섰으면 true 입니다.
         * @param pOutTiming 주면 자원 다시 올리기 · 단계 다시 세우기에 든 시간을 한 줄로 받습니다(교체 로그).
         */
        [[nodiscard]] bool rebindSceneAfterDeviceRecreate( string* pOutTiming = nullptr );
        /** @brief 설정 파일을 다시 읽었습니다(`ConfigManager::onConfigReloaded`). 게임 설정 활성본 · 선호 수직 동기화를 다시 맞춥니다. */
        void onConfigReloaded( const hashed_string& configTypeName );
        /**
         * @brief 주 창 크기가 바뀌었습니다 — 렌더 스레드를 쉬게 한 뒤 스왑체인 크기를 바꿉니다(창 스레드).
         * @details 창 · RHI · 렌더 스레드를 모두 가진 엔진의 일이다(언리얼 `FSceneViewport::ResizeFrame` · Godot RenderingServer 와 같은 자리). 호스트는 모른다.
         */
        void onMainWindowResize( uint32 width, uint32 height );
        /** @brief 렌더 스레드가 받은 패킷을 모두 끝낼 때까지 기다립니다(렌더 스레드가 없으면 할 일이 없다). */
        void drainRenderThreadPackets();
        /** @brief 기동 단계를 구성한 직후 렌더러 몫의 서브시스템(RHI 단계 · 프레임 렌더러 · 패킷 빌더)을 잡아 둡니다. 전용 서버는 할 일이 없습니다. */
        void bindRendererParts( const engine::StartupParts& parts );
        /** @brief 기동 단계가 돈 뒤 RHI · 렌더 스레드 · 주 창 크기 구독을 잡습니다(서지 않은 것은 nullptr). 전용 서버는 할 일이 없습니다. */
        void bindRendererAfterStartup( const engine::StartupParts& parts );
        /** @brief `-gv_screenshotQuit` 의 사진을 모두 찍었으면 그 결과로 종료를 요청합니다(프레임 끝). 전용 서버는 찍을 사진이 없습니다. */
        void requestQuitAfterCaptures();
        /** @brief 프레임 관찰자에게 넘길 이번 프레임의 문맥(입력 · 렌더 스레드가 쓴 스크린샷 수 · 프레임 시간 · 활성 씬)을 만듭니다. */
        EngineFrameContext makeFrameContext() const;
        /** @brief 프레임 앞 서비스를 돌립니다 — 셰이더 핫 리로드, 파일 대화 상자 결과, 에셋 스트리밍 큐, 씬 전환, 이벤트, 오디오(`update` 의 첫 구간). */
        void updateFrameServices( float32 deltaTime );
        /**
         * @brief 활성 씬을 틱합니다. 게임 카메라와 입력이 쓰는 게임 화면 크기를 틱 전에 적고(@p vpWidth · @p vpHeight), 미리보기 씬은 활성 씬의 정지 · 재생과 무관하게 돌립니다.
         * @details 미리보기 씬은 @p views 가 이번 프레임에 그려 달라고 한 씬만 틱합니다(`ScenePreviewTick::WhenDrawn`).
         */
        void updateScenes( float32 deltaTime, const HostViewTargets& views, uint32 vpWidth, uint32 vpHeight, bool bTickScene );
        /** @brief 런타임 UI 를 이번 프레임의 게임 상태로 돌립니다(게임 틱 뒤 · 렌더 패킷 앞). 디버그 HUD 와 음성 자막을 먼저 넘기고, 뷰포트는 게임이 그려지는 화면으로 정합니다. */
        void updateRuntimeUI( const HostViewTarget& mainOutput, uint32 vpWidth, uint32 vpHeight );
        /** @brief 머티리얼을 올리고 렌더 프레임 패킷을 채워 렌더 스레드에 냅니다. Server 타깃에는 렌더러가 없어 아무 일도 하지 않습니다. */
        void submitRenderPacket( float32 deltaTime, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider );

    private:
        /**
         * @brief 코어 범위입니다 — 진단 도구 · 이름 풀 · 로거 · 명령줄(전역 변수) · 크래시 핸들러 · 정적 저장소. 가장 먼저 서고 가장 나중에 사라진다.
         * @details 멤버 중 맨 앞이라 소멸도 맨 마지막이다(기동이 중간에 멈춰 `shutdown` 없이 소멸해도 위 범위가 먼저 사라진다).
         */
        SubsystemScope _coreScope;
        /** @brief 서비스 범위입니다 — 엔진 범위의 어느 것보다 오래 사는 공용 서비스(이벤트 · 지역화 · 디버그 도구 · 프로파일러 · 백엔드 레지스트리 …). 부모는 `_coreScope`. */
        SubsystemScope _services;
        /** @brief 코어 범위의 명령줄 서브시스템입니다(`_coreScope` 소유 — 빌려 쓴다). */
        CommandLineSubsystem* _pCommandLineSubsystem;
        /** @brief 서비스 범위의 이벤트 디스패처입니다(`_services` 소유 — 빌려 쓴다). 프레임마다 큐를 비운다. */
        EventDispatcher* _pEventDispatcher;
        /** @brief 서브시스템 `Config` 의 설정 표입니다(`_startup` 소유 — 빌려 쓴다). App · 서버가 `getConfigManager` 로 읽는다. */
        ConfigManager* _pConfigManager;
        /** @brief 설정 다시 읽기 알림(`ConfigManager::onConfigReloaded`)의 구독입니다. 설정 표가 먼저 사라지면 무효가 된다. */
        Subscription _configReloadedSubscription;
        /** @brief 주 창 크기 칸의 구독입니다(RHI 가 선 클라이언트만). `shutdown` 이 창보다 먼저 놓는다. */
        Subscription _mainWindowResizeSubscription;
        /** @brief 엔진이 붙인 압축 코덱(LZ4 · Zstd · Zlib)의 등록 구독입니다(`EngineCompressionCodecUtil::registerAll`). */
        vector<Subscription> _listCodecSubscription;
        /** @brief 서비스 범위의 디버그 키 맵입니다(`_services` 소유 — 빌려 쓴다). `shutdown` 이 단계 종료 뒤 · 해제 전에(입력 매니저가 소멸하기 전에) 맵을 놓는다. */
        DebugKeyActions* _pDebugKeyActions;
        /**
         * @brief 프레임 관찰자 목록입니다(대상은 소유하지 않는다). 부르는 시점은 루프가 정한다(`beginFrame` · `update` · `endFrame` · `onWindowClosed`) —
         *        종료 코드를 내면 `requestQuit` 입니다. 텔레메트리(기동 단계 `Telemetry` 가 붙인다)와 App 의 진단 · 자동화도 여기로 붙습니다.
         */
        EngineFrameHookList            _frameHooks;
        EngineStartupExtensionDelegate _startupExtension; ///< 호스트가 건 기동 확장(`setStartupExtension`, 없으면 비었다)
        /** @brief 기동 단계 `Telemetry` 가 붙인 텔레메트리 관찰자의 구독입니다. 목록보다 뒤에 선언해 먼저 사라진다. */
        Subscription _telemetryFrameSubscription;

        /** @brief 이 엔진을 띄운 호스트의 역할입니다. 서비스를 만들기 전에 정한다(오디오 장치 · 기동 표 대상). */
        EngineHostRole _hostRole;
        /** @brief 헤드리스 작업의 결과입니다(서브시스템 `Headless` 가 적는다 · 크래시 보고 프로세스도 헤드리스다). */
        HeadlessTaskState _headlessState;
        bool              _bQuitRequested; ///< 창 닫힘 관찰자 · 종료 뒤로 옮겨 둔 요청(`shutdown`)이 정한 끝이다. 그 밖의 요청은 `_pQuitRequest`
        /** @brief RHI 기동 단계의 결과입니다(getRHIInitResult 참고). */
        RHIInitResult _rhiInitResult;
        /** @brief 이번 프레임 씬이 흘린 시간(초)입니다. 씬을 틱하지 않은 프레임은 0 — 디버그 드로우의 지속 시간이 이 값으로 흐릅니다. */
        float32 _sceneDeltaSeconds;
        /** @brief 이번 프레임 시간(초)입니다(`beginFrame` · `tick` 이 받은 값). 프레임 관찰자 문맥의 `_deltaSeconds` 입니다. */
        float32 _frameDeltaSeconds;
        /** @brief `requestQuit` 이 정한 종료 코드입니다. */
        int32 _exitCode;
        /** @brief 서비스 범위의 종료 요청입니다(`_services` 소유 — 빌려 쓴다). 서비스가 내려가기 전에 `shutdown` 이 코드를 위 칸으로 옮기고 비운다. */
        QuitRequest* _pQuitRequest;

        /** @brief `-gv_profileFrames` 계측 한 회분입니다. 판정은 모두 이 안에 있고 루프는 두 줄만 부릅니다. */
        FrameProfileSession _profileSession;
        /** @brief 코어 범위의 메모리 태그 예산 감시입니다(`_coreScope` 소유 — 프레임 끝마다 부른다. 배포본에서는 아무 일도 하지 않는다). */
        MemoryBudgetSubsystem* _pMemoryBudget;
        /** @brief 엔진 범위(기동 단계)입니다 — 선언된 의존으로 위상 정렬해 초기화하고 역순으로 종료 · 소멸합니다. 부모는 `_services`. 서브시스템 객체를 소유합니다. */
        SubsystemScope _startup;
        /** @brief 서브시스템 `RHI` 입니다(`_startup` 소유). 백엔드 교체가 이것에 기대는 단계만 내렸다가 다시 세운다. */
        RHISubsystem* _pRHISubsystem;
        /** @brief 서브시스템 `RHI` 의 RHI 입니다(`_startup` 소유 — 그 단계가 선 뒤부터 빌려 쓴다). 서지 않았으면(전용 서버 · 헤드리스) nullptr 입니다. */
        RHI* _pRHI;
        /** @brief 서브시스템 `Fonts` 입니다(`_startup` 소유 — 빌려 쓴다). 렌더 패킷의 캔버스가 글리프 캐시를 싣는다. */
        FontService* _pFontService;
        /** @brief 서브시스템 `UI` 입니다(`_startup` 소유 — 빌려 쓴다). 프레임마다 입력 · 갱신 · 캔버스가 돈다. */
        UISystem* _pUISystem;
        /** @brief 서브시스템 `Input` 입니다(`_startup` 소유 — 빌려 쓴다). 프레임마다 입력 프레임 · 디버그 키 맵 · 관찰자 문맥이 쓴다. */
        InputManager* _pInputManager;
        /** @brief 서브시스템 `Audio` 의 오디오 서비스입니다(`_startup` 소유 — 빌려 쓴다). 프레임마다 갱신하고 플레이어 설정이 볼륨을 넣는다. */
        IAudioService* _pAudioService;
        /** @brief 서브시스템 `Scene` 의 씬 매니저입니다(`_startup` 소유 — 빌려 쓴다). 프레임마다 전환 · 틱 · 디버그 그리기 · 패킷이 쓴다. */
        SceneManager* _pSceneManager;
        /** @brief 서브시스템 `FrameRenderer` 의 렌더러입니다(`_startup` 소유 — 빌려 쓴다). */
        FrameRenderer* _pFrameRenderer;
        /**
         * @brief 프레임 패킷을 채우는 GT 쪽 상태(씬 스냅샷 빌더 · 패킷 · 업로드 큐 · 추가 뷰 스케줄러)입니다(서브시스템 `FrameRenderer` 소유 — 빌려 쓴다).
         *        그 단계가 세우고 내린다(헤드리스 작업에서는 서지 않는다).
         */
        RenderPacketBuilder* _pPacketBuilder;
        /** @brief 서브시스템 `RenderThread` 의 렌더 스레드입니다(소유는 그 단계 — 처음 설 때부터 해제까지 빌려 쓴다). 전용 서버에는 없다. */
        RenderThread* _pRenderThread;
    };
} // namespace sw
