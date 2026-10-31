/**
 * @file AppDevTools.h
 * @brief App 에만 있는 개발 도구 — 핫 리로드 단축키(게임 · 에디터)와 백엔드 시험 훅을 App 의 프레임 · 시동 열에 끼웁니다.
 * @details 배포에서는 두 함수 모두 아무것도 하지 않는다(`build::kShipping`) — 쓰는 기능(디버그 키 · 백엔드 교체기)이 배포에도 지어져 구현 파일을 가르지 않는다.
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
