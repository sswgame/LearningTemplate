/**
 * @file Engine/HeadlessSubsystem.h
 * @brief 기동 순서의 Headless 자리입니다 — 명령줄의 헤드리스 작업(셰이더 · 씬 쿠킹, 리플렉션 문서, 리타깃 베이크, 립싱크, 임포트 · 로컬라이제이션 준비)을 돌립니다.
 * @details 작업이 있었으면 `SkipDependents` 라 이것에 기대는 단계(창 · RHI · 렌더러 · 플레이어 설정 · 글꼴)가 서지 않는다. 결과는 호스트가 준 상태
 *          (`HeadlessTaskState`)에 적는다 — 엔진 루프가 `isHeadless` · `didHeadlessTaskFail` 로 내보낸다. 전용 서버는 씬 쿠킹만 하고 셰이더 쿠킹 · 임포트는 거절한다.
 *          모든 타입 공급자가 등록을 끝낸 뒤(ModuleLoadStage)라 쿠킹이 게임 · 킷의 컴포넌트 타입을 안다. 글꼴 · 사용자 설정 · RHI 는 낮은 층이라 이 이름을 모르므로
 *          역방향 의존으로 적는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /** @brief 헤드리스 작업의 결과입니다(호스트 소유). */
    struct HeadlessTaskState
    {
        /**
         * @brief 헤드리스 작업으로 들어갑니다 — 누수 기준선을 잡고 헤드리스로 표시합니다. 헤드리스로 표시하는 길은 이것뿐입니다.
         * @details 헤드리스 작업은 보통 실행이 기준선을 잡는 자리(`Engine::initialize` 끝)에 닿지 않는다. 기준선이 없으면 종료 때 살아 있는
         *          기동 블록을 모두 누수로 찍으므로, 표시와 기준선을 한 호출로 묶어 분기마다 따로 기억하지 않게 한다.
         */
        void enterHeadless()
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _bHeadless = true;
        }

        bool isHeadless() const { return _bHeadless; }

        bool _bFailed{ false }; ///< 그 작업이 실패했다 — 호스트가 종료 코드로 내보낸다

    private:
        bool _bHeadless{ false }; ///< 헤드리스 작업을 했다(뒤 단계를 세우지 않았다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class HeadlessSubsystem
     * @brief 명령줄의 헤드리스 작업을 돌리는 서브시스템입니다.
     */
    class SW_API HeadlessSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Headless";

        /** @brief @p outState 에 결과를 적습니다(호스트 소유 — 이것보다 오래 산다). @p bDedicatedServer 면 클라이언트 작업을 거절합니다. */
        HeadlessSubsystem( bool bDedicatedServer, HeadlessTaskState& outState );

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        span<const utf8* const> getSubsystemDependents() const override;

    private:
        HeadlessTaskState& _state;
        bool               _bDedicatedServer;
    };
} // namespace sw
