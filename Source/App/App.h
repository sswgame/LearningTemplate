/**
 * @file App.h
 * @brief 런타임 클라이언트 앱(얇은 런처)입니다.
 *
 * 소유권: App 은 최상위 창 콜백 · EditorModuleManager · Engine 만 들고, 나머지 엔진 핵심 로직은 모두 Engine 에 맡깁니다.
 *
 * @details App 이 직접 아는 것은 네 가지로 제한합니다. 부팅 순서, 창, 프레임 순서, 그리고 호스트 ↔ 모듈 콜백 연결입니다.
 *          시간 정책은 FixedTimestep, 백엔드 교체는 RHIBackendSwitcher, 모듈 수명은 EditorModuleManager(공통 GameModuleManager 위의 App 전용 층),
 *          핫 리로드는 ModuleReloadDriver(Server 와 같이 쓰는 `Source/GameModuleManager`), 개발 콘솔과 표준 입력 명령은 개발 도구(`DevToolsHost`), 헤드리스 작업은 AppHeadlessTasks 가 각자 맡습니다.
 *          자동화가 찾아야 하는 호스트 객체(교체기 · 리로드 구동기)는 정적 포인터가 아니라 호스트 범위(`_host`)에 걸고 `engine::findSubsystem<T>()` 로 찾게 합니다.
 *          런타임 UI 에 끼우는 것(일시정지 메뉴의 "끝내기", 운영체제 클립보드, 스크린 리더)은 협력 객체 `RuntimeUIHost` 가 맡습니다.
 */
#pragma once
#include "App/AppAutomationHost.h"
#include "App/AppDevTools.h"
#include "App/RHIBackendSwitcher.h"
#include "App/RuntimeUIHost.h"
#include "App/UserSettingsHost.h"

#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Time/FixedTimestep.h"

#include "Engine/Engine.h"
#include "Engine/HostServices.h"
#include "Engine/Module/ModuleCatalog.h"

#include "ModuleManager/ModuleReloadDriver.h"
#include "ModuleManager/ModuleStateStore.h"

namespace sw
{
    struct EngineConfig;
    struct NativeWindowEvent;
    struct RenderFramePacket;

    class CommandLineManager;
    class EditorModuleManager;
    class IRHIDevice;
    class IWindow;
    class ModuleLoaderSubsystem;
} // namespace sw

namespace sw
{
    /** @brief 창의 OS 메시지를 엔진에 전달하고 메인 루프를 돌리는 얇은 래퍼입니다. */
    class App
    {
    public:
        App();
        ~App();

        /** @brief 창을 만들고 모듈과 엔진을 초기화합니다. */
        [[nodiscard]] bool initialize( int32 argc, utf8* pArgv[] );
        /** @brief 모든 자원을 해제합니다. */
        void shutdown();
        /** @brief 메인 루프입니다(할 일은 최소한만 합니다). */
        void run();
        /** @brief `initialize` 가 false 를 돌려준 뒤의 프로세스 종료 코드입니다. 이 기계 · 빌드가 그 RHI 백엔드를 못 돌리면 `kRHIUnusableHereExitCode`, 그 밖은 -1 입니다. */
        int32 getInitFailureExitCode() const;
        /** @brief 루프가 끝난 뒤 돌려줄 종료 코드입니다(`Engine::requestQuit`. 요청이 없었으면 0). */
        int32 getExitCode() const;

    private:
        // 부팅 단계. initialize() 가 차례로 부른다.
        /** @brief 호스트 범위를 세웁니다 — 모듈 라이브러리 창구와 자동화 구동기를 걸고 초기화합니다. 엔진을 세우기 전에 부릅니다. */
        [[nodiscard]] bool initializeHostScope();
        /** @brief 플랫폼 창을 확보합니다. Engine 가 이미 만들어 두었으면 그 소유권을 넘겨받습니다. */
        bool acquireMainWindow( const EngineConfig& engineConfig, const CommandLineManager& commandLineManager );
        /**
         * @brief 기동 단계 `ModuleLoadStage` 의 호스트 로더입니다. 타입 공급자 모듈(GameFramework, 키트, SWGame)의 이미지를 로드해 그 타입을 등록합니다.
         * @details 헤드리스 작업(씬 쿠킹)도 이 단계를 지나므로 쿠킹이 게임과 키트 컴포넌트를 제 타입으로 쿠킹합니다. 인스턴스는 `startModules` 가 만듭니다.
         */
        [[nodiscard]] bool loadModuleLibrarys();
        /**
         * @brief 호스트 서비스 하나를 세웁니다(`kArrClientHostService` — 순서는 `HostService` 의 의존 선언이 정한다).
         * @details `Game` 은 EditorModuleManager 로 게임과 에디터 인스턴스를 만든다(모듈 라이브러리는 `loadModuleLibrarys` 가 이미 로드했다).
         */
        [[nodiscard]] bool startHostService( HostServiceID id );
        /** @brief `startHostService` 가 세운 서비스 하나를 내립니다. 호스트 서비스 집합이 역순으로 부릅니다. */
        void stopHostService( HostServiceID id );
        /**
         * @brief 모듈이 모두 로드된 뒤에도 가져간 곳이 없는 `-gv_*` 인자를 한 번 경고합니다.
         * @details 파서는 모르는 `gv_` 키를 버리지 않고 보류한다(모듈이 선언하는 변수는 파싱 시점에 아직 없다). 그래서 오타가
         *          바로 드러나지 않으므로 여기서 대신 알린다.
         */
        void warnUnknownGlobalOverrides() const;
        /** @brief 창 콜백 · Present 훅을 연결합니다(호스트 서비스이 선 뒤). */
        void bindHostCallbacks();

        /** @brief 설정 파일을 다시 읽었습니다. EngineConfig 면 프레임 시간 정책(최대 델타 · 고정 스텝)을 다시 정합니다. */
        void onConfigReloaded( const hashed_string& configTypeName );
        /** @brief 네이티브 창 이벤트를 전달합니다. */
        bool onWindowMessage( const NativeWindowEvent& event );
        /** @brief 에디터 렌더 훅 콜백입니다(렌더 스레드). EditorModuleManager 로 넘깁니다. */
        void onEditorRender( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );
        /** @brief 에디터 멀티 뷰포트 · 플랫폼 창 훅 콜백입니다(Present 뒤에 실행됩니다). */
        void onEditorPostPresent( IRHIDevice& renderDevice, const RenderFramePacket& framePacket );

    private:
        /**
         * @brief 호스트 범위입니다 — 엔진 범위의 부모이고, 엔진보다 먼저 서서 나중에 사라집니다(그래서 `_engine` 보다 앞에 선언한다).
         * @details 모듈 라이브러리 창구(`ModuleLoaderSubsystem` — 엔진의 `ModuleLoadStage` · `ModuleUnloadStage` 단계가 찾아 부른다)와 자동화 구동기(`AutomationSession`)를 듭니다.
         *          구동기는 엔진 서비스를 빌려 쓰므로 `shutdown` 이 엔진 종료 앞에서 먼저 내립니다. 개발 도구는 이 범위에 호스트가 직접 넣습니다(호스트 종류 비트를 두지 않는다).
         */
        SubsystemScope _host;
        /** @brief 모듈이 리로드 사이에 맡기는 칸입니다. 호스트 범위에 `IModuleStateStore` 로 건다(`initializeHostScope`). 범위보다 먼저 선언해 나중에 사라진다. */
        ModuleStateStore _moduleStateStore;
        /** @brief 호스트 범위의 모듈 라이브러리 창구입니다(`_host` 소유 — 빌려 쓴다). */
        ModuleLoaderSubsystem* _pModuleImageHost;
        /** @brief 개발 도구 연결(호스트 범위의 `DevToolsHost` · 매니페스트 · 리로드 단축키 · 콘솔)입니다. 구현 파일은 CMake 가 빌드마다 고른다(`AppDevTools.h`). */
        AppDevTools _devTools;
        Engine      _engine;
        /** @brief 자동화 시나리오 연결(호스트 범위의 구동기 · 엔진 프레임 관찰자)입니다. 엔진보다 뒤에 선언해 먼저 사라진다(`shutdown` 이 엔진 종료 전에 뗀다). */
        AppAutomationHost _automation;
        /** @brief 모듈 핫 리로드입니다(Engine 가 아니라 런처의 일 — Server 와 같은 구동기). 자신을 `_host` 에 겁니다. Shipping 에서는 아무것도 하지 않습니다. */
        ModuleReloadDriver _moduleReload;
        /** @brief 게임 창의 개발 콘솔(`~`)과 표준 입력 명령 채널입니다. Shipping 에서는 아무것도 하지 않습니다. */
        unique_ptr<EditorModuleManager> _gameModuleManager;
        /** @brief 모듈 매니페스트(`Bin/Modules`)와 그 해석입니다. 무엇을 어떤 순서로 로드할지 정합니다(Dev). */
        ModuleCatalog    _moduleCatalog;
        ModuleResolution _moduleResolution;
        /** @brief 주 창입니다(관찰 포인터). 소유자는 엔진 범위의 `MainWindowSubsystem` 이라 엔진 종료 때 창이 파괴된다. `acquireMainWindow` 가 채운다. */
        IWindow* _pWindow;

        FixedTimestep _fixedTimestep;
        /** @brief 백엔드 교체기입니다. 호스트 서비스 `BackendSwitcher` 가 서며 `_host` 에 걸고 내려가며 풉니다(시나리오 단계 `SwapBackend` · 감시 값 `RHI.*` 가 찾는다). */
        RHIBackendSwitcher _backendSwap;
        UserSettingsHost   _userSettingsHost;
        /** @brief 런타임 UI 의 호스트 훅(클립보드 · 스크린 리더 · 끝내기)입니다. 호스트 서비스 `RuntimeUIHooks` 가 세우고 내린다. */
        RuntimeUIHost _runtimeUIHost;

        // 프레임마다 다시 만들 이유가 없는 델리게이트다. bindHostCallbacks 에서 한 번 묶는다.
        /** @brief 에디터 모드에서만 연결됩니다. 비어 있으면 Engine 가 씬 카메라를 씁니다. */
        ViewCameraProviderDelegate _sceneViewCameraProvider;
        /** @brief 설정 다시 읽기 알림(`ConfigManager::onConfigReloaded`)의 구독입니다. 엔진이 설정 표를 내리면 무효가 된다. */
        Subscription _configReloadedSubscription;
        /** @brief 주 창의 메시지 처리기 칸 구독입니다(`bindHostCallbacks`). 창이 엔진 종료 때 파괴되면 무효가 된다. 크기 칸은 엔진이 쥔다(스왑체인). */
        Subscription _windowMessageSubscription;
        /// @brief `initialize` 가 시작된 시각(마이크로초, `MonotonicClock`)입니다. 메인 루프에 들어갈 때 로그에 시작 시간을 찍습니다.
        int64 _initializeStartMicro;
        /**
         * @brief 엔진 뒤에 서고 엔진 앞에 내려가는 호스트 서비스(게임 · 교체기 · 사용자 설정 · 런타임 UI 훅 · 개발 콘솔)입니다. 순서는 `HostService` 의 의존 선언이 정한다.
         * @details 서비스의 몸통이 위 멤버를 쓰므로 맨 뒤에 둔다(소멸이 먼저다). `shutdown` 이 엔진 종료 앞에서 내린다.
         */
        SubsystemCollection _hostServices;

        uint8                  _bEnableEditor        : 1;
        uint8                  _bQuitAfterInitialize : 1; ///< 한 번 하고 끝나는 작업(`--render-portraits`)을 마쳤다. 루프에 들어가지 않는다.
        [[maybe_unused]] uint8 _reserved             : 5;
    };
} // namespace sw
