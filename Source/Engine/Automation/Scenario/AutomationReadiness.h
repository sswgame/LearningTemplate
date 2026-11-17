/**
 * @file AutomationReadiness.h
 * @brief 시나리오를 붙잡는 조건(시작 · 시계 · 캡처 · 끝 · 렌더 정착)이 내는 세 값(`AutomationReadiness`)과 진척 없음을 세는 시계(`AutomationStallClock` · 벽시계판 `AutomationWallStallClock`)입니다.
 * @details 예전 조건은 `bool()` 이라 "이 구성에는 그 일이 없다"(TAA 패스가 없는 Forward 뷰)와 "아직" 이 같은 값이었고, 대기는 시한까지 기다린 뒤에야 졌다(DesignBacklog 실패 1 —
 *          Empty 체인 74 분). 세 값이면 해당 없음은 막지 않고, 진행 중은 진척 수를 함께 내 그 수가 정지 시한 동안 그대로면 바로 진다(`WaitStalled`).
 *          상용 엔진 비교: 언리얼 Gauntlet 의 하트비트(활동이 없으면 진다), 에디터 `<EditorWaitStable/>` 의 "할 일이 없는데 움직인다" 판정과 같은 자리다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class AutomationReadiness
     * @brief 조건 하나의 지금 상태입니다 — 준비됨 · 해당 없음 · 진행 중(진척 수와 무엇을 기다리는지 한 줄). 기본 생성자가 없어 셋 중 하나를 골라야 한다.
     */
    class AutomationReadiness
    {
    public:
        /** @brief 기다릴 것이 없습니다. */
        [[nodiscard]] static constexpr AutomationReadiness ready() { return AutomationReadiness{ State::Ready, 0, nullptr }; }
        /** @brief 이 구성에는 이 일이 없습니다(파이프라인에 TAA 패스가 없다 · 자동 노출을 쓰지 않는다). 막지 않는다. */
        [[nodiscard]] static constexpr AutomationReadiness notApplicable() { return AutomationReadiness{ State::NotApplicable, 0, nullptr }; }
        /**
         * @brief 아직입니다.
         * @param progress 일이 나아가면 바뀌는 수(쌓인 정지 프레임 · 남은 일 수). 진척을 낼 수 없는 일은 0 — 그 대기는 시한만 본다
         * @param pWhat 무엇을 기다리는지 한 줄(정적 글 — 등록한 이미지의 저장소에 있다)
         */
        [[nodiscard]] static constexpr AutomationReadiness waiting( uint64 progress, const utf8* pWhat ) { return AutomationReadiness{ State::Waiting, progress, pWhat }; }

        /** @brief 진행 중이면 true 입니다(준비됨 · 해당 없음은 막지 않는다). */
        [[nodiscard]] constexpr bool        isWaiting() const { return _state == State::Waiting; }
        [[nodiscard]] constexpr bool        isNotApplicable() const { return _state == State::NotApplicable; }
        [[nodiscard]] constexpr uint64      getProgress() const { return _progress; }
        [[nodiscard]] constexpr const utf8* getWhat() const { return _pWhat != nullptr ? _pWhat : ""; }

    private:
        enum class State : uint8
        {
            Ready,
            NotApplicable,
            Waiting,
        };

        constexpr AutomationReadiness( State state, uint64 progress, const utf8* pWhat )
            : _progress{ progress }
            , _pWhat{ pWhat }
            , _state{ state }
        {
        }

    private:
        uint64      _progress;
        const utf8* _pWhat;
        State       _state;
    };

    /** @brief 조건 하나의 지금 상태를 냅니다. 게임 스레드에서 프레임마다 불린다 — 비싼 훑기를 하지 않는다. */
    using AutomationReadinessFunction = AutomationReadiness ( * )();

    /**
     * @class AutomationStallClock
     * @brief 진행 중인 조건의 진척 수가 그대로인 이어진 횟수를 셉니다(정지 시한 `ScenarioRun.json` 의 `_stallFrames` 와 견준다). 기다리는 조건이 바뀌거나 진척이 바뀌면 0 부터다.
     * @details 조건 이름은 등록의 정적 글이라 포인터로 견준다(프레임마다 글을 복사하지 않는다).
     */
    class AutomationStallClock
    {
    public:
        /**
         * @brief 이번에 본 조건(@p pName — 기다리는 것이 없으면 nullptr)과 진척 @p progress 를 넣고, 진척 없이 이어진 횟수를 돌려줍니다.
         * @details 기다리는 것이 없으면 0 으로 되돌린다.
         */
        uint32 advance( const utf8* pName, uint64 progress )
        {
            if ( pName == nullptr )
            {
                reset();
                return 0;
            }
            const bool bSame = pName == _pName && progress == _progress;
            _pName           = pName;
            _progress        = progress;
            _unchangedCount  = bSame ? _unchangedCount + 1 : 0;
            return _unchangedCount;
        }

        void reset()
        {
            _pName          = nullptr;
            _progress       = 0;
            _unchangedCount = 0;
        }

        [[nodiscard]] uint32 getUnchangedCount() const { return _unchangedCount; }

    private:
        const utf8* _pName{ nullptr };
        uint64      _progress{ 0 };
        uint32      _unchangedCount{ 0 };
    };

    /**
     * @class AutomationWallStallClock
     * @brief `AutomationStallClock` 의 벽시계판입니다 — 벽시계 감시 값(`SW_AUTOMATION_WALL_WATCH`)의 활동 수가 그대로인 초를 셉니다(정지 시한 `ScenarioRun.json` 의 `_stallWallSeconds` 와 견준다).
     * @details 바깥 일은 프레임이 실시간보다 빨리 돌아 횟수로 셀 수 없다(1400 프레임이 1 초였다). 감시 값 이름은 등록의 정적 글이라 포인터로 견준다.
     */
    class AutomationWallStallClock
    {
    public:
        /**
         * @brief 이번에 본 감시 값(@p pName — 기다리는 것이 없으면 nullptr) · 활동 수 @p activity · 지금 초 @p nowSeconds 를 넣고, 활동 수가 그대로인 초를 돌려줍니다.
         * @details 감시 값이나 활동 수가 바뀌면 지금부터 다시 센다. 기다리는 것이 없으면 0 으로 되돌린다.
         */
        float64 advance( const utf8* pName, uint64 activity, float64 nowSeconds )
        {
            if ( pName == nullptr )
            {
                reset();
                return 0.0;
            }
            if ( pName != _pName || activity != _activity )
            {
                _pName          = pName;
                _activity       = activity;
                _changedSeconds = nowSeconds;
            }
            return nowSeconds - _changedSeconds;
        }

        void reset()
        {
            _pName          = nullptr;
            _activity       = 0;
            _changedSeconds = 0.0;
        }

    private:
        const utf8* _pName{ nullptr };
        uint64      _activity{ 0 };
        float64     _changedSeconds{ 0.0 };
    };
} // namespace sw
