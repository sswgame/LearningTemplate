/**
 * @file InteractionSession.h
 * @brief 상호작용 한 번의 진행 — 단계마다 입력 방식(누름 · 누르고 있기 · 연타)대로 차오르고, 마지막 단계를 끝내면 완료, 놓거나 떠나면 취소입니다.
 * @details 누르고 있기는 기반 `InteractionProgress`(끊기면 처음부터)를 그대로 씁니다. 시간은 `update` 로만 흐릅니다(결정적).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Interaction/InteractionCatalog.h"
#include "GameFramework/Interaction/InteractionProgress.h"

namespace sw
{
    /** @brief 진행 중 생긴 일입니다. */
    enum class InteractionSessionEvent : uint8
    {
        Started = 0,
        StepCompleted, ///< 마지막이 아닌 단계를 끝냈다
        Completed,
        Cancelled
    };

    /** @brief 진행 상태입니다. */
    enum class InteractionSessionState : uint8
    {
        Idle = 0,
        Active,
        Completed,
        Cancelled
    };
} // namespace sw

namespace sw
{
    /**
     * @class InteractionSession
     * @brief 정의 하나를 빌려 들고 진행합니다. 다시 쓰려면 `begin` 을 다시 부릅니다.
     */
    class SW_GF_API InteractionSession
    {
    public:
        InteractionSession();

        /**
         * @brief 시작합니다. 누름 단계면 그 자리에서 끝납니다(시작을 일으킨 누름이 그 단계의 누름).
         * @return 시작했으면 true(정의가 없거나 단계가 없으면 false).
         */
        bool begin( const InteractionDef* pDef, uint32 actorId );
        /**
         * @brief 시간을 흘립니다. @p bHeld = 지금 누르고 있는가, @p bPressed = 이번 프레임에 새로 눌렀는가.
         * @details 누르고 있기 단계에서 떼면 취소(진행은 처음부터), 연타 단계는 누를 때마다 1/presses 오르고 쉬면 decay 만큼 줄어듭니다.
         */
        void update( float32 deltaTime, bool bHeld, bool bPressed );
        /** @brief 취소합니다(범위 밖 · 대상 사라짐 · 맞음). 진행 중이 아니면 아무것도 하지 않습니다. */
        void cancel();

        InteractionSessionState getState() const { return _state; }
        bool                    isActive() const { return _state == InteractionSessionState::Active; }
        int32                   getStepIndex() const { return _stepIndex; }
        int32                   getStepCount() const { return _pDef != nullptr ? static_cast<int32>( _pDef->_listStep.size() ) : 0; }
        /** @brief 지금 단계의 진행(0..1)입니다. */
        float32 getStepProgress() const;
        /** @brief 지금 단계의 정의입니다. 진행 중이 아니면 nullptr 입니다. */
        const InteractionStepDef* getStep() const;
        const InteractionDef*     getDefinition() const { return _pDef; }
        /** @brief 쌓인 일을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<InteractionSessionEvent>& outListEvent );

    private:
        void beginStep();
        void completeStep();

        const InteractionDef*           _pDef;
        InteractionProgress             _holdProgress;
        vector<InteractionSessionEvent> _listEvent;
        vector<InteractionEvent>        _listHoldEvent;
        uint32                          _actorId;
        int32                           _stepIndex;
        float32                         _mashProgress;
        InteractionSessionState         _state;
    };
} // namespace sw
