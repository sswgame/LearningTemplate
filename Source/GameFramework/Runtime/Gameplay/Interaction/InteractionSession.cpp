#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractionSession.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Runtime/Foundation/Utility/DepletingValue.h"

namespace sw
{
    InteractionSession::InteractionSession()
        : _pDef{}
        , _holdProgress{}
        , _eventBuffer{}
        , _listHoldEvent{}
        , _actorID{ 0 }
        , _stepIndex{ 0 }
        , _mashProgress{ 0.0f }
        , _state{ InteractionSessionState::Idle }
    {
    }

    bool InteractionSession::begin( shared_ptr<const InteractionDef> pDef, uint32 actorID )
    {
        if ( pDef == nullptr || pDef->_listStep.empty() )
            return false;
        _pDef      = std::move( pDef );
        _actorID   = actorID;
        _stepIndex = 0;
        _state     = InteractionSessionState::Active;
        _eventBuffer.push( InteractionSessionEvent::Started );
        beginStep();
        return true;
    }

    void InteractionSession::beginStep()
    {
        const InteractionStepDef& step = _pDef->_listStep[static_cast<size_t>( _stepIndex )];
        _mashProgress                  = 0.0f;
        switch ( step._mode )
        {
            case InteractionInputMode::Press:
            {
                // 누름 단계는 그 단계를 시작한 누름으로 끝난다. 첫 단계는 시작한 누름으로, 다음 단계는 다음 누름으로 끝나며 이는 `update` 에서 처리한다.
                if ( _stepIndex == 0 )
                    completeStep();
                break;
            }
            case InteractionInputMode::Hold:
            {
                InteractionConfig config;
                config._duration          = step._duration;
                config._maxParticipants   = step._maxParticipants;
                config._bResetOnInterrupt = SW_TRUE;
                if ( _holdProgress.initialize( config, nullptr, 1u ) == false )
                {
                    cancel(); // 카탈로그가 단계의 시간과 인원을 범위 안으로만 읽으므로 닿지 않는다. 닿으면 이 상호작용을 취소한다(이유는 initialize 가 남겼다)
                    break;
                }
                (void)_holdProgress.join( _actorID );
                break;
            }
            case InteractionInputMode::Mash:
            {
                break;
            }
        }
    }

    void InteractionSession::completeStep()
    {
        if ( _stepIndex + 1 >= getStepCount() )
        {
            _state = InteractionSessionState::Completed;
            _eventBuffer.push( InteractionSessionEvent::Completed );
            return;
        }
        ++_stepIndex;
        _eventBuffer.push( InteractionSessionEvent::StepCompleted );
        beginStep();
    }

    void InteractionSession::update( float32 deltaTime, bool bHeld, bool bPressed )
    {
        if ( _state != InteractionSessionState::Active )
            return;
        const InteractionStepDef& step = _pDef->_listStep[static_cast<size_t>( _stepIndex )];
        switch ( step._mode )
        {
            case InteractionInputMode::Press:
            {
                if ( bPressed )
                    completeStep();
                break;
            }
            case InteractionInputMode::Hold:
            {
                if ( bHeld == false )
                {
                    // 떼면 진행은 처음부터 다시 시작한다(InteractionProgress 의 끊김 규칙). 이 상호작용은 끝난다.
                    (void)_holdProgress.leave( _actorID );
                    cancel();
                    break;
                }
                _holdProgress.update( deltaTime );
                _listHoldEvent.clear();
                _holdProgress.pollEvents( _listHoldEvent );
                if ( _holdProgress.isCompleted() )
                    completeStep();
                break;
            }
            case InteractionInputMode::Mash:
            {
                if ( bPressed )
                    _mashProgress += 1.0f / static_cast<float32>( MathUtil::max( 1, step._presses ) );
                else
                    (void)DepletingValue::take( _mashProgress, step._decay * deltaTime ); // 쉬면 줄고 0 에서 멈춘다(음수 감쇠는 깎지 않는다) — 깎은 양은 쓰지 않는다
                if ( _mashProgress >= 1.0f - 1.0e-5f )
                    completeStep();
                break;
            }
        }
    }

    void InteractionSession::cancel()
    {
        if ( _state != InteractionSessionState::Active )
            return;
        _state = InteractionSessionState::Cancelled;
        _eventBuffer.push( InteractionSessionEvent::Cancelled );
    }

    float32 InteractionSession::getStepProgress() const
    {
        const InteractionStepDef* pStep = getStep();
        if ( pStep == nullptr )
            return _state == InteractionSessionState::Completed ? 1.0f : 0.0f;
        switch ( pStep->_mode )
        {
            case InteractionInputMode::Press:
                return 0.0f;
            case InteractionInputMode::Hold:
                return _holdProgress.getProgress();
            case InteractionInputMode::Mash:
                return MathUtil::saturate( _mashProgress );
        }
        return 0.0f;
    }

    const InteractionStepDef* InteractionSession::getStep() const
    {
        if ( _state != InteractionSessionState::Active || _pDef == nullptr )
            return nullptr;
        return &_pDef->_listStep[static_cast<size_t>( _stepIndex )];
    }

    void InteractionSession::pollEvents( vector<InteractionSessionEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }
} // namespace sw
