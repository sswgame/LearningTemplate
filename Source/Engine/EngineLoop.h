/**
 * @file EngineLoop.h
 * @brief 엔진 코어의 메인 루프와 서브시스템 소유권을 관리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/SubsystemCollection.h"

#include "Engine/EngineBootstrap.h"
#include "Engine/EngineServiceCollection.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/HeadlessSubsystem.h"
#include "Engine/Input/ShellInputActions.h"
#include "Engine/Module/ModuleTypesSubsystem.h"
#include "Engine/Profiling/FrameProfileSession.h"
#include "Engine/Profiling/MemoryBudgetMonitor.h"
#include "Engine/Renderer/Frame/PresentHookDelegate.h"
#include "Engine/Renderer/Frame/RenderPacketBuilder.h"
#include "Engine/Renderer/Frame/RenderView.h"

namespace sw
{
    struct EngineConfig;
    struct EngineDefaultAssets;
    struct EngineFrameContext;

    class AssetManager;
    class AssetStreamingQueue;
    class CameraComponent;
    class CommandLineManager;
    class CommandStack;
    class CompressionCodecRegistry;
    class ConfigManager;
    class DebugDrawQueue;
    class EventDispatcher;
    class FontService;
    class FrameProfiler;
    class FrameRenderer;
    class GlobalVariableManager;
    class IAudioService;
    class IEngineFrameObserver;
    class InputManager;
    class InputMap;
    class IRHIDevice;
    class RenderTargetRegistry;
    class RenderThread;
    class RHI;
    class RHIBackendRegistry;
    class RHISubsystem;
    class SceneManager;
    class ShaderRecompiler;
    class TaskManager;
    class TypeRegistry;
    class UISystem;

    // `ViewCameraProviderDelegate`(씬 뷰 카메라 조회)는 그것을 부르는 패킷 빌더 헤더(`RenderPacketBuilder.h`)에 있습니다.

    /** @brief 엔진을 띄우는 호스트의 역할입니다 — 기동 표의 대상(`SubsystemHostMask`) · 오디오 장치 · 허용하는 헤드리스 작업을 가릅니다. */
    enum class EngineHostRole : uint8
    {
        Client,          ///< App — 창 · 렌더러 · 플레이어 설정(에디터 포함)
        DedicatedServer, ///< Server — 창 · GPU · 오디오 장치 · 플레이어 설정 없음
    };

    /**
     * @class EngineLoop
     * @brief 코어 매니저들을 소유하고 메인 루프(tick)를 돌립니다.
     */
    class SW_API EngineLoop
    {
    public:
        EngineLoop();
        ~EngineLoop();

        EngineLoop( const EngineLoop& )            = delete;
        EngineLoop& operator=( const EngineLoop& ) = delete;

        /**
         * @brief 서브시스템(창, RHI 포함)을 초기화합니다.
         * @details @p role 이 `DedicatedServer` 면 기동 표의 `Client` 단계(창 · RHI · 렌더러 · 플레이어 설정)를 돌리지 않고 장치 없는 오디오를 씁니다.
         *          이 빌드에 없는 역할이면(Client 빌드의 전용 서버, Server 빌드의 클라이언트) 실패합니다.
         */
        bool initialize( int32 argc, utf8* pArgv[], EngineHostRole role = EngineHostRole::Client );
        /** @brief 매니저들을 종료하고 정리합니다. */
        void shutdown();

        /**
         * @brief 입력 등을 시작하는 프레임의 첫 단계입니다.
         * @param deltaSeconds 지난 프레임의 실제 시간(초). 입력 장치의 타이머(진동 길이 · 게임패드 재연결 주기)가 이 값으로 흐릅니다.
         *                     고정값으로 흘리면 0.3 초 진동이 144 fps 에서 0.13 초로 끝납니다.
         */
        void beginFrame( float32 deltaSeconds );
        /**
         * @brief 씬 업데이트, RHI 제출 등을 수행합니다.
         * @param deltaTime 델타 타임
         * @param views 호스트가 요청한 오프스크린 출력(에디터 게임 뷰 · 씬 뷰). 둘 다 비면 백버퍼에 게임 카메라로 그립니다.
         * @param sceneViewCameraProvider 씬 뷰를 그리는 호스트 카메라(에디터 카메라)를 돌려주는 델리게이트. 씬 뷰가 없으면 부르지 않습니다.
         * @param bTickScene false이면 씬 GameObject tick을 건너뜁니다 (에디터 Pause).
         */
        void tick( float32 deltaTime, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider, bool bTickScene );
        /** @brief 입력 종료 등 프레임의 마지막 단계입니다. */
        void endFrame();

        /** @brief 대기 중인 RHI 핫스왑을 수행합니다. */
        [[nodiscard]] bool applyPendingBackendChange();

        // ----------------------------------------------------------------------
        // 도우미
        // ----------------------------------------------------------------------
        /**
         * @brief 씬이 모두 정리됐고 엔진 서비스는 **아직 살아 있는** 지점에 불릴 훅을 겁니다.
         * @details 종료 시퀀스에는 "씬은 사라졌지만 서비스(SceneManager · TaskManager · 로거)는 아직 있다" 는 좁은
         *          구간이 있습니다. 모듈 DLL 을 내리는 일은 정확히 거기서 일어나야 합니다. 더 일찍이면 씬이 든 컴포넌트
         *          팩토리 델리게이트가 사라진 코드를 가리키고, 더 늦으면 언로드가 쓰는 서비스와 로거가 이미 없습니다.
         *          Engine 은 그것이 무엇인지 모릅니다. 자리만 내주고, 무엇을 할지는 훅을 건 쪽이 정합니다.
         */
        void setOnScenesReleased( Delegate<void()> onScenesReleased );
        /**
         * @brief 기동 단계 `ModuleTypes` 가 부를 타입 공급자 로더를 겁니다. `initialize` **전에** 겁니다.
         * @details 걸지 않으면 호스트에 따로 올릴 타입 공급자가 없다는 뜻입니다(정적 링크 — 리플렉션 단계가 이미 다 모았다). 어느 쪽이든 단계가 끝나면
         *          모든 타입이 등록된 것으로 적고, 씬을 읽는 일(헤드리스 쿠킹 · 게임 · 에디터의 씬 로드)은 그 뒤에만 됩니다.
         */
        void setModuleTypeLoader( ModuleTypeLoaderDelegate moduleTypeLoader );
        /**
         * @brief 프레임 관찰자(`IEngineFrameObserver`)를 붙입니다. 붙인 순서대로 부르고, 같은 관찰자를 두 번 붙이지 않습니다.
         * @details 루프는 관찰자가 무엇인지 모릅니다(자동화 시나리오 · 시퀀서 · AI 구동기). 수명은 붙인 쪽이 쥐고, 없애기 전에 `removeFrameObserver` 로 뗍니다.
         */
        void addFrameObserver( IEngineFrameObserver* pObserver );
        /** @brief 붙인 프레임 관찰자를 뗍니다. 붙어 있지 않으면 아무것도 하지 않습니다. */
        void removeFrameObserver( IEngineFrameObserver* pObserver );

        void setPresentHook( sw::PresentHookDelegate presentHook );
        void setPostPresentHook( sw::PresentHookDelegate postPresentHook );
        /** @brief 셸 디버그 액션 맵을 한 프레임 돌립니다(`ShellInputActions::update`). 입력 매니저가 없으면 아무것도 하지 않습니다. */
        void updateShellActions( float32 deltaTime );
        /** @brief 셸 디버그 액션 맵을 InputMap 리소스(`EngineDefaultAssets::_shellInputMap`)에서 만듭니다(`ShellInputActions::createInputMap`). */
        static unique_ptr<InputMap> createShellInputMap( string_view inputMapPath );

        /**
         * @brief 엔진이 스스로 종료를 원하면 true 입니다(`-gv_profileFrames=N` 을 다 채웠거나 `requestQuit` 이 불렸다).
         * @details 창 수명은 App 이 쥐고 있으므로 여기서는 의사만 알립니다.
         */
        bool isQuitRequested() const { return _bQuitRequested || _profileSession.isQuitRequested(); }
        /** @brief 이번 프레임이 끝나면 루프를 끝내라고 요청합니다(프레임 관찰자 · 도구). 여럿이면 처음 것의 코드를 씁니다. */
        void requestQuit( int32 exitCode );
        /** @brief App 이 돌려줄 종료 코드입니다(요청이 없었으면 0). */
        int32 getExitCode() const { return _exitCode; }
        /**
         * @brief 창이 닫혀 루프가 끝났다고 알립니다(App 이 루프 뒤에 부른다). 프레임 관찰자가 종료 코드를 내면 그 코드로 정합니다
         *        (관찰자의 결과가 프로파일 세션의 종료보다 이긴다).
         */
        void onWindowClosed();

        /**
         * @brief 셸 디버그 InputMap 에서 해당 액션이 이번 프레임에 발동했는지 반환합니다.
         * @details Engine 이 내주는 것은 **입력 사실**뿐입니다. 그 액션이 무엇을 뜻하는지(모듈을 다시 올린다,
         *          에디터를 다시 올린다)는 그 장치를 가진 쪽이 정합니다 — Engine 이 리로드 콜백을 들면 Shipping 헤더에도
         *          리로드 델리게이트가 남습니다.
         */
        bool wasDebugActionTriggered( string_view actionName ) const;
        /**
         * @brief 셸 InputMap 입니다(`updateShellActions` 가 처음 부를 때 만든다 — 그 전과 Shipping 에서는 nullptr).
         * @details 개발 콘솔(`DevConsoleController`)이 자기 액션을 읽고 자기 레이어를 켜고 끕니다. 이 맵은 키보드 포커스와 상관없이 키를 읽습니다.
         */
        InputMap* getShellInputMap() const { return _shellActions.getInputMap(); }

        // ----------------------------------------------------------------------
        // Getter (App 이 ModuleHost 등과 연동하는 데 필요)
        // ----------------------------------------------------------------------
        ConfigManager*      getConfigManager() const { return _pConfigManager; }
        CommandLineManager* getCommandLineManager() const { return _owned._pCommandLineManager.get(); }
        RHI*                getRHI() const { return _pRHI; }
        RenderThread*       getRenderThread() const { return _pRenderThread; }
        bool                isHeadless() const { return _headlessState._bHeadless; }
        /** @brief 이 엔진을 띄운 호스트의 역할입니다(`initialize` 의 인자). */
        EngineHostRole getHostRole() const { return _hostRole; }
        /**
         * @brief 프리팹마다 초상화를 그려 @p outputDirectory 에 `<이름>.portrait.dds` · `.png` 로 씁니다(`--render-portraits`).
         * @details 렌더 스레드가 쉬는 동안 이 스레드가 그래픽스 컨텍스트를 잡고 `PortraitRenderer`(따로 만든 스튜디오 씬 · 렌더러)로 그린다. 모듈(게임
         *          컴포넌트 타입)이 올라온 뒤에 부른다. 하나라도 실패하면 false 입니다.
         */
        [[nodiscard]] bool renderPortraits( string_view prefabList, string_view outputDirectory, uint32 size );
        /** @brief 헤드리스 작업(셰이더 쿠킹 · 씬 쿠킹)이 실패했는지 반환합니다. 부르는 쪽은 이것을 종료 코드로 내보냅니다. */
        bool didHeadlessTaskFail() const { return _headlessState._bFailed; }
        /** @brief RHI 기동 단계의 결과입니다. 그 단계가 돌지 않았으면(헤드리스 · 앞 단계 실패) `NotStarted` 입니다. App 이 기동 실패의 종료 코드를 고를 때 읽습니다. */
        RHIInitResult getRHIInitResult() const { return _rhiInitResult; }

    private:
        /** @brief 디바이스 재생성 뒤 내렸던 단계(렌더러 · 렌더 스레드 · 라이브 셰이더 · 씬의 디바이스)를 다시 세웁니다. 모두 섰으면 true 입니다. */
        [[nodiscard]] bool rebindSceneAfterDeviceRecreate();
        /** @brief 셰이더 라이브 리로드 매니저입니다. Shipping 에서는 늘 nullptr 입니다. */
        ShaderRecompiler* getShaderRecompiler() const;
        /** @brief 셰이더 강제 리로드 핫키를 처리합니다. Engine 자신의 개발 도구이므로 여기서 끝냅니다. */
        void pollShaderReloadHotkey();
        /** @brief 설정 파일을 다시 읽었습니다(`ConfigManager::onConfigReloaded`). 게임 설정 활성본 · 선호 수직 동기화를 다시 맞춥니다. */
        void onConfigReloaded( const hashed_string& configTypeName );
        /** @brief 프레임 관찰자에게 넘길 이번 프레임의 문맥(입력 · 렌더 스레드가 쓴 스크린샷 수 · 프레임 시간 · 활성 씬)을 만듭니다. */
        EngineFrameContext makeFrameContext() const;

    private:
        /**
         * @brief 목록(`EngineServiceList.xxx`)의 `EngineCreated` 서비스 저장소입니다. 생성 · 바인딩이 여기서 나옵니다.
         * @details 목록에 줄을 더하면 이 저장소가 같이 자랍니다. 만드는 방법이 특별한 셋
         *          (팩토리 · 구성별 조건부)만 아래에 손으로 남아 있습니다.
         */
        EngineServiceCollection _owned;

        /** @brief 기동 표 밖의 부트스트랩(로거 · 크래시 핸들러 · 진단 도구 · 명령줄 · 전역 변수)입니다. 테스트 하네스와 같은 것을 쓴다. `_owned` 보다 먼저 사라진다. */
        EngineBootstrap _bootstrap;
        /** @brief 서브시스템 `Config` 의 설정 표입니다(`_startup` 소유 — 빌려 쓴다). App · 서버가 `getConfigManager` 로 읽는다. */
        ConfigManager* _pConfigManager;
        /** @brief 셸 디버그 액션 맵입니다(`updateShellActions` 가 처음 부를 때 만든다). `shutdown` 이 단계 종료 뒤 · 해제 전에(입력 매니저가 소멸하기 전에) 놓는다. */
        ShellInputActions _shellActions;
        /** @brief 만든 오디오 서비스입니다(역할별 팩토리). 서비스 표에 묶은 뒤 소유를 서브시스템 `Audio` 로 넘긴다 — 이 뒤로는 비어 있다. */
        unique_ptr<IAudioService> _audioService;
        /** @brief 만든 렌더러입니다(서비스 표에 실린다). 묶은 뒤 소유를 서브시스템 `FrameRenderer` 로 넘긴다 — 이 뒤로는 비어 있다. */
        unique_ptr<FrameRenderer> _frameRenderer;
        Delegate<void()>          _onScenesReleased;
        /** @brief 기동 단계 `ModuleTypes` 가 부르는 호스트의 타입 공급자 로더입니다(`setModuleTypeLoader`). */
        ModuleTypeLoaderDelegate _moduleTypeLoader;
        /** @brief 에디터 Undo/Redo 전용이라 배포본에는 만들지 않습니다(목록의 HostCreated). 씬이 바뀌면 씬 매니저가 비운다. 모든 서브시스템이 소멸한 뒤 놓는다. */
        unique_ptr<CommandStack> _commandStack;
        /**
         * @brief 프레임 관찰자 목록입니다(소유하지 않는다). 프레임 앞(입력 전) · 씬 틱 뒤(UI 앞) · 패킷 채우기 · 프레임 뒤(렌더 제출 뒤) · 창 닫힘에서 붙인 순서대로 부르고,
         *        종료 코드를 내면 `requestQuit` 입니다. 텔레메트리(기동 단계 `Telemetry` 가 붙인다)와 App 의 진단 · 자동화도 여기로 붙습니다.
         */
        vector<IEngineFrameObserver*> _listFrameObserver;

        /** @brief 이 엔진을 띄운 호스트의 역할입니다. 서비스를 만들기 전에 정한다(오디오 장치 · 기동 표 대상). */
        EngineHostRole _hostRole;
        /** @brief 헤드리스 작업의 결과입니다(서브시스템 `Headless` 가 적는다 · 크래시 보고 프로세스도 헤드리스다). */
        HeadlessTaskState _headlessState;
        bool              _bQuitRequested;
        /** @brief RHI 기동 단계의 결과입니다(getRHIInitResult 참고). */
        RHIInitResult _rhiInitResult;
        /** @brief 이번 프레임 씬이 흘린 시간(초)입니다. 씬을 틱하지 않은 프레임은 0 — 디버그 드로우의 지속 시간이 이 값으로 흐릅니다. */
        float32 _sceneDeltaSeconds;
        /** @brief 이번 프레임 시간(초)입니다(`beginFrame` · `tick` 이 받은 값). 프레임 관찰자 문맥의 `_deltaSeconds` 입니다. */
        float32 _frameDeltaSeconds;
        /** @brief `requestQuit` 이 정한 종료 코드입니다. */
        int32 _exitCode;

        /** @brief `-gv_profileFrames` 계측 한 회분입니다. 판정은 모두 이 안에 있고 루프는 두 줄만 부릅니다. */
        FrameProfileSession _profileSession;
        /** @brief 메모리 태그 예산 · 보고(배포본에서는 아무 일도 하지 않는다). */
        MemoryBudgetMonitor _memoryBudgetMonitor;
        /** @brief 기동 단계(서브시스템)의 목록입니다 — 선언된 의존으로 위상 정렬해 초기화하고 역순으로 종료 · 소멸합니다. 서브시스템 객체를 소유합니다. */
        SubsystemCollection _startup;
        /** @brief 서브시스템 `RHI` 입니다(`_startup` 소유). 백엔드 교체가 이것에 기대는 단계만 내렸다가 다시 세운다. */
        RHISubsystem* _pRHISubsystem;
        /** @brief 서브시스템 `RHI` 의 RHI 입니다(`_startup` 소유 — 그 단계가 선 뒤부터 빌려 쓴다). 서지 않았으면(전용 서버 · 헤드리스) nullptr 입니다. */
        RHI* _pRHI;
        /** @brief 서브시스템 `Fonts` 입니다(`_startup` 소유 — 빌려 쓴다). 렌더 패킷의 캔버스가 글리프 캐시를 싣는다. */
        FontService* _pFontService;
        /** @brief 서브시스템 `UI` 입니다(`_startup` 소유 — 빌려 쓴다). 프레임마다 입력 · 갱신 · 캔버스가 돈다. */
        UISystem* _pUISystem;
        /** @brief 서브시스템 `Input` 입니다(`_startup` 소유 — 빌려 쓴다). 프레임마다 입력 프레임 · 셸 맵 · 관찰자 문맥이 쓴다. */
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
        /**
         * @brief 셰이더 라이브 리로드입니다(서브시스템 `LiveShader` 소유 — 빌려 쓴다). **Shipping 에는 없고** Debug 클라이언트에서만 실제로 만들어진다.
         * @details 셰이더 파일을 지켜보다 다시 컴파일하는 **개발 도구**입니다. RHI 는 디바이스 추상화이지 파일 감시자가 있을 곳이
         *          아니므로 돌리는 쪽(EngineLoop)이 갖습니다.
         */
#if !defined( SW_SHIPPING )
        ShaderRecompiler* _pShaderRecompiler;
#endif
    };
} // namespace sw
