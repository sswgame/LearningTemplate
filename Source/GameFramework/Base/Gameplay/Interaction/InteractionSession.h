/**
 * @file InteractionSession.h
 * @brief 상호작용 한 번의 진행입니다. 단계마다 입력 방식(누름, 누르고 있기, 연타)대로 차오르고, 마지막 단계를 끝내면 완료되며, 놓거나 떠나면 취소됩니다.
 * @details 누르고 있기는 기반인 `InteractionProgress`(끊기면 처음부터)를 그대로 쓴다. 시간은 `update` 로만 흐르며 결정적이다.
 */
#pragma once
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionProgress.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 진행 중에 일어난 일입니다. */
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
     * @brief 정의 하나를 참조해서 진행합니다. 정의는 소유하지 않습니다. 다시 쓰려면 `begin` 을 다시 부릅니다.
     */
    class SW_GF_API InteractionSession
    {
    public:
        InteractionSession();

        /**
         * @brief 시작합니다. 누름 단계이면 즉시 끝납니다. 시작을 일으킨 누름이 그 단계의 누름입니다.
         * @return 시작했으면 true 입니다. 정의가 없거나 단계가 없으면 false 입니다.
         */
        bool begin( const InteractionDef* pDef, uint32 actorID );
        /**
         * @brief 시간을 진행합니다. @p bHeld 는 지금 누르고 있는지, @p bPressed 는 이번 프레임에 새로 눌렀는지 나타냅니다.
         * @details 누르고 있기 단계에서 떼면 취소되고 진행은 처음부터 다시 시작한다. 연타 단계는 누를 때마다 1/presses 만큼 오르고, 쉬면 decay 만큼 줄어든다.
         */
        void update( float32 deltaTime, bool bHeld, bool bPressed );
        /** @brief 취소합니다. 범위를 벗어나거나, 대상이 사라지거나, 맞았을 때 씁니다. 진행 중이 아니면 아무것도 하지 않습니다. */
        void cancel();

        InteractionSessionState getState() const { return _state; }
        bool                    isActive() const { return _state == InteractionSessionState::Active; }
        int32                   getStepIndex() const { return _stepIndex; }
        int32                   getStepCount() const { return _pDef != nullptr ? static_cast<int32>( _pDef->_listStep.size() ) : 0; }
        /** @brief 지금 단계의 진행량(0..1)입니다. */
        float32 getStepProgress() const;
        /** @brief 지금 단계의 정의입니다. 진행 중이 아니면 nullptr 입니다. */
        const InteractionStepDef* getStep() const;
        const InteractionDef*     getDefinition() const { return _pDef; }
        /** @brief 쌓인 이벤트를 @p outListEvent 뒤에 추가하고 비웁니다. */
        void drainEvents( vector<InteractionSessionEvent>& outListEvent );

    private:
        void beginStep();
        void completeStep();

        const InteractionDef*                _pDef;
        InteractionProgress                  _holdProgress;
        EventBuffer<InteractionSessionEvent> _eventBuffer;
        vector<InteractionEvent>             _listHoldEvent;
        uint32                               _actorID;
        int32                                _stepIndex;
        float32                              _mashProgress;
        InteractionSessionState              _state;
    };
} // namespace sw
