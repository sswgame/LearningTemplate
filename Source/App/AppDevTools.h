/**
 * @file AppDevTools.h
 * @brief App 에만 있는 개발 도구 — 핫 리로드 단축키(게임 · 에디터)와 백엔드 시험 훅을 App 의 프레임 · 시동 열에 끼웁니다.
 * @details 빌드마다 다른 것은 이 선언들의 구현 파일이다 — Dev 는 `AppDevTools.cpp`, Shipping 은 `AppDevToolsShipping.cpp`(빈 구현)를 CMake 가 고른다
 *          (`Source/App/CMakeLists.txt`, 언리얼의 Developer 모듈을 배포 타깃이 링크하지 않는 자리). 인터페이스 · 가상 함수를 두지 않는다 — 한 빌드에 구현은 하나다.
 *          App · Server 가 같이 쓰는 개발 도구 연결(`DevToolsHost` · 프레임 훅 · 개발 콘솔)은 `ModuleManager/HostDevTools.h` 다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class ModuleReloadDriver;
    class RenderThread;
    class RHIBackendSwitcher;

    /**
     * @struct AppDevTools
     * @brief App 에만 있는 개발 도구입니다(상태 없음). 파일 머리말 참고.
     */
    struct AppDevTools
    {
        /** @brief 렌더 스레드에 개발 전용 훅(프레임 시작의 디바이스 제거 시험 — 개발 명령 `rhi.removeDevice <ms>`)을 겁니다. */
        static void bindRenderThreadHooks( RenderThread& renderThread, RHIBackendSwitcher& backendSwitcher );
        /**
         * @brief 셸 액션(리로드 단축키 · 개발 콘솔 키)을 이번 프레임 것으로 갱신하고, 리로드 단축키가 눌렸으면 리로드를 요청합니다(Ctrl+F7 게임, 에디터 모드의 Ctrl+F6 에디터).
         * @details 실제 교체는 `ModuleReloadDriver::update` 가 틱 직전에 한다. 배포에는 리로드할 모듈이 없어 셸 InputMap 도 로드하지 않는다 — 아무것도 하지 않는다.
         */
        static void pollShellActions( float32 deltaTime, ModuleReloadDriver& moduleReload, bool bEnableEditor );
    };
} // namespace sw
