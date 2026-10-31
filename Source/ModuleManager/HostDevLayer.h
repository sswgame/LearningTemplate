/**
 * @file HostDevLayer.h
 * @brief 호스트(App · Server)의 개발 계층 창구 `IHostDevLayer` 입니다 — 개발 도구 연결(`DevToolsHost` · 프레임 훅 · 개발 콘솔)과 모듈 핫 리로드를 한 묶음으로 듭니다.
 * @details 구현(`ModuleManager/Dev/` — DevTools · LiveReload 를 쓴다)은 개발 구성에만 있고 배포는 폴더째 짓지 않는다. 호스트는 엔진을 세우기 전에
 *          `if constexpr ( build::kShipping == false )` 한 곳에서 `makeHostDevLayer` 로 만들고, 배포에서는 포인터가 비어 있다 — 버려지는 갈래라 배포 링크에
 *          `makeHostDevLayer` 의 정의가 없어도 된다. 빈 구현 파일을 두지 않는다(`docs/agents/BuildConfigSwitch.md`). 언리얼이 Developer 모듈을 배포 타깃에 링크하지 않는 것과 같은 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct ModuleResolution;
    class Engine;
    class LiveReloadManager;
    class ModuleCatalog;
    class ModuleLoaderSubsystem;
    class SubsystemScope;

    /** @brief 리로드 묶음을 시작하기 전에 부르는 델리게이트입니다(바꿀 모듈 이름들). false 면 이번 묶음을 미룬다. */
    using BeforeReloadBatchDelegate = Delegate<bool( const vector<string>& listModuleName )>;

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
        /**
         * @brief 모듈 매니페스트를 읽어 @p outCatalog · @p outResolution 을 채우고 핫 리로드 매니저를 세웁니다(호스트 범위에 겁니다).
         * @param targetMask 이 호스트가 올릴 모듈 대상(`ModuleTarget` 비트)입니다.
         * @return 매니페스트를 읽지 못했으면 false 입니다(기동 실패).
         */
        [[nodiscard]] virtual bool initializeModuleReload( uint8 targetMask, SubsystemScope& host, ModuleLoaderSubsystem& moduleLibraryHost, ModuleCatalog& outCatalog,
                                                           ModuleResolution& outResolution ) = 0;
        /** @brief 핫 리로드 매니저입니다(`initializeModuleReload` 전에는 nullptr). 모듈 관리자가 라이브러리를 올릴 때 넘긴다. */
        virtual LiveReloadManager* getLiveReloadManager() const = 0;
        /** @brief 리로드 묶음을 시작하기 전의 알림을 구독합니다(서버가 서비스를 멈춘다). */
        [[nodiscard]] virtual Subscription addOnBeforeReloadBatch( BeforeReloadBatchDelegate onBeforeBatch ) = 0;
        /** @brief 모듈 @p pModuleName 을 다음 `updateModuleReload` 에서 다시 올리게 합니다(단축키 · 개발 명령). */
        virtual void requestReload( const utf8* pModuleName ) = 0;
        /** @brief 바뀐 모듈 라이브러리를 보고 리로드를 돌립니다. 틱 직전에 부릅니다. */
        virtual void updateModuleReload() = 0;

        /** @brief 엔진이 선 뒤 개발 도구의 프레임 훅을 붙입니다. */
        virtual void attachFrameHooks( Engine& engine ) = 0;
        /** @brief 개발 콘솔을 세웁니다(호스트 서비스 `DevConsole`). */
        virtual void startConsole( bool bEnableEditor ) = 0;
        /** @brief 개발 콘솔을 지웁니다. */
        virtual void stopConsole() = 0;
        /** @brief 프레임 훅을 뗍니다. 개발 도구는 엔진 서비스를 빌려 쓰므로 엔진 종료 전에 부릅니다. */
        virtual void detach() = 0;
    };

    /** @brief 개발 계층을 만듭니다. 정의는 개발 구성에만 있다(`ModuleManager/Dev/HostDevLayer.cpp`) — `build::kShipping` 이 아닌 갈래에서만 부른다. */
    [[nodiscard]] unique_ptr<IHostDevLayer> makeHostDevLayer();
} // namespace sw
