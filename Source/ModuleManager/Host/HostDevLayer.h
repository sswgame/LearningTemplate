/**
 * @file HostDevLayer.h
 * @brief 호스트(App · Server)의 개발 계층 창구 `IHostDevLayer` 입니다 — 개발 도구 연결(`DevToolsHost` · 프레임 훅 · 개발 콘솔), 모듈 핫 리로드, 그리고 App 의 개발 전용 몫
 *        (에디터 모듈 관리자 · 자동화 실행기 · 호스트 서비스 뒤 작업 · 기동 측정)을 한 묶음으로 듭니다.
 * @details 구현(`DevTools/Host/HostDevLayer.cpp`)은 DevTools.dll 에만 있고 배포는 DevTools 를 짓지 않는다 — 배포 App 은 에디터 · 자동화 · 개발 콘솔 · 셸 · 기동 측정 코드를 컴파일하지 않는다.
 *          ModuleManager 는 DevTools 를 보지 않으므로(의존은 DevTools → ModuleManager) 호스트(App · Server)가 `if constexpr ( build::kShipping == false )` 한 곳에서 `makeHostDevLayer` 로 만들어
 *          부팅 열(`HostLaunch::initializeHostScope`)에 넘긴다. 배포에서는 포인터가 비어 있다(`HostLaunch::getDevLayer` 가 배포에서 상수 nullptr) — 버려지는 갈래라 배포 링크에 `makeHostDevLayer` 의 정의가 없어도 된다.
 *          빈 구현 파일을 두지 않는다(`docs/agents/CppStyle.md "Build configuration switches"`). 언리얼이 Developer · Editor 모듈을 배포 타깃에 링크하지 않는 것과 같은 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/DynamicModules.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "ModuleManager/ModuleManagerAPI.h"

namespace sw
{
    struct ModuleResolution;
    class CommandLineManager;
    class Engine;
    class GameModuleManager;
    class HostLaunch;
    class IAutomationShutdownWatch;
    class ILiveReloadManager;
    class ModuleCatalog;
    class ModuleLoaderSubsystem;
    class SubsystemScope;

    /** @brief 리로드 묶음을 시작하기 전에 부르는 델리게이트입니다(바꿀 모듈 이름들). false 면 이번 묶음을 미룬다. */
    using BeforeReloadBatchDelegate = Delegate<bool( const vector<string>& listModuleName )>;

    /** @brief 호스트 서비스가 선 뒤 한 번 하고 끝나는 작업(`IHostDevLayer::runAfterHostSubsystems`)의 결과입니다. */
    enum class HostTaskResult : uint8
    {
        NotRequested, ///< 인자가 없어 실행하지 않았다(기동을 이어 간다).
        Failed,       ///< 실행했고 실패했다.
        Finished,     ///< 이 작업만 하고 끝낸다(성공).
    };

    /**
     * @class IHostDevLayer
     * @brief 호스트의 개발 계층입니다. 파일 머리말 참고. 게임 스레드에서만 쓴다.
     */
    class IHostDevLayer
    {
    public:
        virtual ~IHostDevLayer() = default;

        /** @brief 개발 도구를 호스트 범위에 더하고 엔진 기동 확장을 겁니다. 엔진을 세우기 전에 부릅니다. */
        virtual void addToHostScope( SubsystemScope& host, Engine& engine ) = 0;
        /** @brief 셸 액션(디버그 키 — 개발 콘솔 키, 동적 모듈 구성이면 리로드 단축키도)을 이번 프레임 것으로 갱신하고 리로드 단축키를 요청으로 바꿉니다(App 이 프레임마다 — Ctrl+F6 에디터는 @p bEnableEditor 일 때만). */
        virtual void pollShellActions( float32 deltaTime, bool bEnableEditor ) = 0;
#if SW_WITH_DYNAMIC_MODULES
        // ---- 핫 리로드 — 동적 모듈 구성에만 있다. 정적 구성(배포 · 웹 · Android 개발)에서 부르는 코드는 컴파일되지 않는다(`SW_WITH_DYNAMIC_MODULES`) ----
        /**
         * @brief 모듈 매니페스트를 읽어 @p outCatalog · @p outResolution 을 채우고 핫 리로드 매니저를 세웁니다(호스트 범위에 겁니다).
         * @param targetMask 이 호스트가 올릴 모듈 대상(`ModuleTarget` 비트)입니다.
         * @return 매니페스트를 읽지 못했으면 false 입니다(기동 실패).
         */
        [[nodiscard]] virtual bool initializeModuleReload( uint8 targetMask, SubsystemScope& host, ModuleLoaderSubsystem& moduleLibraryHost, ModuleCatalog& outCatalog,
                                                           ModuleResolution& outResolution ) = 0;
        /** @brief 핫 리로드 매니저입니다(`initializeModuleReload` 전에는 nullptr). 모듈 관리자가 라이브러리를 올릴 때 넘긴다. */
        virtual ILiveReloadManager* getLiveReloadManager() const = 0;
        /** @brief 리로드 묶음을 시작하기 전의 알림을 구독합니다(서버가 서비스를 멈춘다). */
        [[nodiscard]] virtual Subscription addOnBeforeReloadBatch( BeforeReloadBatchDelegate onBeforeBatch ) = 0;
        /** @brief 모듈 @p pModuleName 을 다음 `updateModuleReload` 에서 다시 올리게 합니다(단축키 · 개발 명령). */
        virtual void requestReload( const utf8* pModuleName ) = 0;
        /** @brief 바뀐 모듈 라이브러리를 보고 리로드를 돌립니다. 틱 직전에 부릅니다. */
        virtual void updateModuleReload() = 0;
#endif

        /**
         * @brief 헤드리스로 선 호스트가 명령줄이 청한 에디터 모듈 작업(현지화 도구 · 원본 임포트 · 대조 · 에셋 참조 검사)을 차례로 돌립니다. 엔진이 선 뒤에 부릅니다.
         * @return 실패한 작업이 없으면(청한 것이 없어도) true 입니다.
         */
        [[nodiscard]] virtual bool runEditorModuleHeadlessTasks( const CommandLineManager& commandLine ) = 0;
        /** @brief 엔진이 선 뒤 개발 도구의 프레임 훅을 붙입니다. */
        virtual void attachFrameHooks( Engine& engine ) = 0;
        /**
         * @brief 개발 콘솔(게임 창 콘솔과 표준 입력 채널)을 호스트 서비스 `DevConsole` 로 @p launch 에 더합니다. 콘솔 명령은 게임이 올린 개발 명령을 부르므로 @p pGameServiceName 뒤에 선다.
         * @param pGameServiceName 게임 호스트 서비스의 이름입니다(프로그램 내내 사는 글).
         */
        virtual void addConsoleHostService( HostLaunch& launch, const utf8* pGameServiceName, bool bEnableEditor ) = 0;

        // ---- 클라이언트(App)의 개발 전용 몫 ----
        /** @brief 에디터 인스턴스를 드는 모듈 관리자를 만듭니다(`-EnableEditor` — 구현 `DevTools/Host/EditorGameModuleManager`). 에디터 렌더 훅 · 창 최소 크기도 그 관리자가 건다. */
        [[nodiscard]] virtual unique_ptr<GameModuleManager> makeEditorModuleManager() = 0;
        /**
         * @brief 엔진이 선 뒤 자동화 실행기(`IAutomationRuntime`)를 찾아 시작합니다. 모듈을 올리기 전에 부릅니다.
         * @return 실행기가 없는 구성이 `-scenario` · `-scenario-record` 를 받았으면 false(기동 오류)입니다 — 시나리오 없이 돌면 결과를 낼 수 없다.
         */
        [[nodiscard]] virtual bool startAutomation( const CommandLineManager& commandLine ) = 0;
        /** @brief 무인 시나리오를 돌리는 중인가입니다(프레임 페이싱의 자동화 상태). */
        [[nodiscard]] virtual bool isAutomationBusy() const = 0;
        /** @brief 시나리오를 돌린 실행이면 프로세스 종료 감시를 돌려줍니다(호스트가 엔진을 다 내린 뒤 놓는다). 아니면 nullptr 입니다. */
        [[nodiscard]] virtual unique_ptr<IAutomationShutdownWatch> watchAutomationShutdown() = 0;
        /**
         * @brief 호스트 서비스(게임 · 에디터 인스턴스)가 선 뒤 한 번 하고 끝나는 작업(초상화 굽기 `--render-portraits`)을 실행합니다.
         * @return 실행한 작업이 없으면 `NotRequested` 입니다. 그 밖이면 호스트는 메인 루프에 들어가지 않고 끝낸다.
         */
        [[nodiscard]] virtual HostTaskResult runAfterHostSubsystems( const CommandLineManager& commandLine ) = 0;

        // ---- 기동 측정 — 단계표(`PhaseTimings`)는 개발 계층을 만든 때부터 잰다 ----
        /** @brief 앞 단계 끝부터 지금까지를 @p pPhaseName 단계로 적습니다(프로그램 내내 사는 글). */
        virtual void markStartupPhase( const utf8* pPhaseName ) = 0;
        /** @brief 시동 요약 한 줄(단계별 ms · 오래 걸린 서브시스템)을 남깁니다. */
        virtual void logStartupPhases() = 0;
        /** @brief 메인 루프에 들어가는 줄(시작 시간 · 파일 연 횟수)을 남기고 첫 프레임들을 잴 기준 시각을 잡습니다. */
        virtual void onMainLoopEntered() = 0;
        /** @brief 메인 루프의 한 프레임이 끝났습니다 — 첫 프레임 몇 개의 끝난 시각을 남깁니다. */
        virtual void onFrameEnded() = 0;
        /** @brief 첫 그림이 나와 주 창을 띄웠습니다 — 메인 루프에 든 뒤 걸린 시간을 남깁니다. */
        virtual void onMainWindowShown() = 0;
        /** @brief 프레임 훅을 뗍니다. 개발 도구는 엔진 서비스를 빌려 쓰므로 엔진 종료 전에 부릅니다. */
        virtual void detach() = 0;
    };

    /** @brief 개발 계층을 만듭니다. 정의는 DevTools.dll 에만 있다(`DevTools/Host/HostDevLayer.cpp`) — 호스트가 `build::kShipping` 이 아닌 갈래에서만 부른다. */
    [[nodiscard]] SW_HOST_DEV_API unique_ptr<IHostDevLayer> makeHostDevLayer();
} // namespace sw
