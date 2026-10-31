/**
 * @file ModuleManager/Dev/HostDevTools.h
 * @brief 호스트(App · Server)의 개발 도구 연결 — 개발 도구 호스트(`DevToolsHost`)를 호스트 범위에 더하고, 엔진 기동 확장 · 프레임 훅 · 개발 콘솔을 겁니다.
 * @details 개발 구성에만 있다(`ModuleManager/Dev/` — 배포는 폴더째 짓지 않는다). 호스트는 개발 계층(`IHostDevLayer`)으로 닿고, 같은 순서로 부른다 — `addToHostScope`(호스트 범위를 세울 때) →
 *          `attachFrameHooks`(엔진 기동 뒤) → `startConsole` / `stopConsole`(호스트 서비스, App 만) → `detach`(엔진 종료 앞).
 *          호스트에만 있는 개발 도구(App 의 리로드 단축키 · 백엔드 시험 훅)는 그 호스트의 파일이 든다(`App/AppDevTools.h`).
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class DevToolsHost;
    class Engine;
    class SubsystemScope;

    /**
     * @class HostDevTools
     * @brief 호스트의 개발 도구 연결입니다. 파일 머리말 참고.
     */
    class HostDevTools
    {
    public:
        HostDevTools() = default;

        /** @brief 개발 도구 호스트를 호스트 범위에 더하고 엔진 기동 확장 · 콘텐츠 폴더 감시(`IContentFolderWatch`)를 겁니다. 엔진을 세우기 전에 부릅니다. */
        void addToHostScope( SubsystemScope& host, Engine& engine );
        /** @brief 엔진이 선 뒤 개발 도구의 프레임 훅(씬 틱 뒤 HUD · 콘솔, 프레임 끝에 셰이더 재컴파일 · 단축키 · 설정 리로드)을 붙입니다. */
        void attachFrameHooks( Engine& engine );
        /** @brief 개발 콘솔을 세웁니다(App 의 호스트 서비스 `DevConsole`). */
        void startConsole( bool bEnableEditor );
        /** @brief 개발 콘솔을 내립니다(App 의 호스트 서비스 `DevConsole`). */
        void stopConsole();
        /** @brief 개발 도구의 프레임 훅을 뗍니다. 개발 도구는 엔진 서비스를 빌려 쓰므로 엔진 종료 앞에 부릅니다. */
        void detach();

    private:
        DevToolsHost* _pDevTools = nullptr; ///< 호스트 범위의 개발 도구(`host` 소유 — 빌려 쓴다)
        Subscription  _frameSubscription;   ///< 개발 도구의 프레임 관찰 구독
    };
} // namespace sw
