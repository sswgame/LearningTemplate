/**
 * @file AppDevTools.h
 * @brief App 의 개발 도구 연결 — 개발 도구 호스트(`DevToolsHost`) · 모듈 매니페스트 · 핫 리로드 단축키 · 개발 콘솔 · 백엔드 시험 훅을 App 의 시동 열에 끼웁니다.
 * @details 빌드마다 다른 것은 이 선언들의 구현 파일이다 — Dev 는 `AppDevTools.cpp`, Shipping 은 `AppDevToolsShipping.cpp`(빈 구현)를 CMake 가 고른다
 *          (`Source/App/CMakeLists.txt`, 언리얼의 Developer 모듈을 배포 타깃이 링크하지 않는 자리). 인터페이스 · 가상 함수를 두지 않는다 — 한 빌드에 구현은 하나다.
 *          순서는 App 이 정한다 — `addToHostScope`(호스트 범위를 세울 때) → `attachFrameHooks`(엔진 기동 뒤) →
 *          `startConsole` / `stopConsole`(호스트 서비스) → `detach`(엔진 종료 앞) → `release`(엔진 종료 뒤).
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class DevToolsHost;
    class Engine;
    class ModuleReloadDriver;
    class RenderThread;
    class RHIBackendSwitcher;
    class SubsystemScope;

    /**
     * @class AppDevTools
     * @brief App 의 개발 도구 연결입니다. 파일 머리말 참고.
     */
    class AppDevTools
    {
    public:
        AppDevTools() = default;

        /** @brief 개발 도구 호스트를 호스트 범위에 더하고 엔진 기동 확장 · 콘텐츠 폴더 감시(`IContentFolderWatch`)를 겁니다. 엔진을 세우기 전에 부릅니다. */
        void addToHostScope( SubsystemScope& host, Engine& engine );
        /** @brief 엔진이 선 뒤 개발 도구의 프레임 훅(씬 틱 뒤 HUD · 콘솔, 프레임 끝에 셰이더 재컴파일 · 단축키 · 설정 리로드)을 붙입니다. */
        void attachFrameHooks( Engine& engine );
        /** @brief 개발 콘솔을 세웁니다(호스트 서비스 `DevConsole`). */
        void startConsole( bool bEnableEditor );
        /** @brief 개발 콘솔을 내립니다(호스트 서비스 `DevConsole`). */
        void stopConsole();
        /** @brief 렌더 스레드에 개발 전용 훅(프레임 시작의 디바이스 제거 시험 — 개발 명령 `rhi.removeDevice <ms>`)을 겁니다. */
        void bindRenderThreadHooks( RenderThread& renderThread, RHIBackendSwitcher& backendSwitcher );
        /**
         * @brief 셸 액션(리로드 단축키 · 개발 콘솔 키)을 이번 프레임 것으로 갱신하고, 리로드 단축키가 눌렸으면 리로드를 요청합니다(Ctrl+F7 게임, 에디터 모드의 Ctrl+F6 에디터).
         * @details 실제 교체는 `ModuleReloadDriver::update` 가 틱 직전에 한다. 배포에는 리로드할 모듈이 없어 셸 InputMap 도 로드하지 않는다 — 아무것도 하지 않는다.
         */
        void pollShellActions( float32 deltaTime, ModuleReloadDriver& moduleReload, bool bEnableEditor );
        /** @brief 개발 도구의 프레임 훅을 뗍니다. 개발 도구는 엔진 서비스를 빌려 쓰므로 엔진 종료 앞에 부릅니다. */
        void detach();
        /** @brief 엔진 종료(호스트 범위 비우기) 뒤 개발 도구 호스트 포인터를 놓습니다. */
        void release() { _pDevTools = nullptr; }

    private:
        DevToolsHost* _pDevTools = nullptr; ///< 호스트 범위의 개발 도구(`host` 소유 — 빌려 쓴다. 배포 빌드는 늘 nullptr)
        Subscription  _frameSubscription;   ///< 개발 도구의 프레임 관찰 구독
    };
} // namespace sw
