/**
 * @file AppAutomationHost.h
 * @brief App 의 자동화 시나리오 연결 — 호스트 범위의 자동화 구동기(`AutomationSession`)를 더하고, `-scenario` · `-scenario-record` 로 시작하고, 엔진 프레임 관찰자로 붙이고, 엔진보다 먼저 뗍니다.
 * @details App 이 소유하는 협력 객체다(언리얼 Automation Controller 가 실행 파일 루프 밖에서 세션을 쥐는 자리). 순서는 App 이 정한다 —
 *          `addToHostScope`(호스트 범위를 세우기 전) → `start`(엔진 기동 뒤 · 모듈 앞) → `detach`(호스트 서비스를 내린 뒤 · 엔진 종료 앞) → `release`(엔진 종료 뒤).
 *          자동화 런타임을 뺀 구성(`SW_ENABLE_AUTOMATION=OFF`)은 구동기가 없고, 시나리오 인자를 조용히 무시하지 않고 오류로 남긴다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class AutomationSession;
    class CommandLineManager;
    class Engine;
    class SubsystemScope;

    /**
     * @class AppAutomationHost
     * @brief App 의 자동화 시나리오 연결입니다. 파일 머리말 참고.
     */
    class AppAutomationHost
    {
    public:
        AppAutomationHost();

        /** @brief 자동화 구동기를 호스트 범위에 더합니다(범위를 세우기 전). 구동기는 범위가 소유한다. */
        void addToHostScope( SubsystemScope& host );
        /**
         * @brief `-scenario` 가 있으면 자동화 세션을 시작하고 렌더 스레드의 시나리오 캡처를 켭니다. 재생기는 늘 엔진 프레임 관찰자로 붙이고 호스트 범위에 `IScenarioPlayer` 로 겁니다.
         * @details 시나리오는 늘 고정 프레임 시간이다(세션이 단계마다 `gv_fixedFrameDelta` 를 그 시나리오 값으로 맞춘다). 여럿이면 한 프로세스에서 차례로 돈다.
         *          게임 · 콘솔의 재생(`ScenarioPlayback::play`)은 기동 뒤 아무 때나 시작하므로 시나리오가 없어도 붙인다 — 쉬는 세션의 프레임 훅은 아무것도 하지 않는다.
         */
        void start( Engine& engine, SubsystemScope& host, const CommandLineManager& commandLineManager );
        /** @brief 세션이 무인 시나리오를 돌리는 중인가(재생 · 구동기 없는 구성은 false). 프레임 페이싱이 읽는다. */
        bool isBusy() const;
        /** @brief 재생기를 호스트 범위에서 풀고 엔진에서 뗍니다. 구동기는 로그 리스너 · 입력을 빌려 쓰므로 엔진 서비스보다 먼저 부릅니다. */
        void detach( SubsystemScope& host );
        /** @brief 엔진 종료(호스트 범위 비우기) 뒤 구동기 포인터를 놓습니다. */
        void release() { _pSession = nullptr; }

    private:
        AutomationSession* _pSession;          ///< 호스트 범위의 자동화 구동기(`host` 소유 — 빌려 쓴다)
        Subscription       _frameSubscription; ///< 구동기를 엔진 프레임 관찰자로 붙인 구독(`detach` 가 엔진 종료 전에 놓는다)
    };
} // namespace sw
