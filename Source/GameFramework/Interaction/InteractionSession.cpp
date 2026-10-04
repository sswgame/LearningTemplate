#include "pch.h"

#include "GameFramework/Interaction/InteractionSession.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    InteractionSession::InteractionSession()
        : _pDef{ nullptr }
        , _holdProgress{}
        , _eventBuffer{}
        , _listHoldEvent{}
        , _actorId{ 0 }
        , _stepIndex{ 0 }
        , _mashProgress{ 0.0f }
        , _state{ InteractionSessionState::Idle }
    {
    }

    bool InteractionSession::begin( const InteractionDef* pDef, uint32 actorId )
    {
        if ( pDef == nullptr || pDef->_listStep.empty() )
            return false;
        _pDef      = pDef;
        _actorId   = actorId;
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
                // 누름 단계는 그 단계를 시작한 누름으로 끝난다(첫 단계는 시작한 누름, 다음 단계는 다음 누름 — `update`).
                if ( _stepIndex == 0 )
                    completeStep();
                break;
            }
            case InteractionInputMode::Hold:
            {
                InteractionConfig config;
                config._duration          = MathUtil::max( 1.0e-3f, step._duration );
                config._maxParticipants   = step._maxParticipants;
                config._bResetOnInterrupt = SW_TRUE;
                _holdProgress.initialize( config, nullptr, 1u );
                (void)_holdProgress.join( _actorId );
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
                    // 떼면 진행은 처음부터(InteractionProgress 의 끊김 규칙) — 상호작용이 끝난다.
                    (void)_holdProgress.leave( _actorId );
                    cancel();
                    break;
                }
                _holdProgress.update( deltaTime );
                _listHoldEvent.clear();
                _holdProgress.drainEvents( _listHoldEvent );
                if ( _holdProgress.isCompleted() )
                    completeStep();
                break;
            }
            case InteractionInputMode::Mash:
            {
                if ( bPressed )
                    _mashProgress += 1.0f / static_cast<float32>( MathUtil::max( 1, step._presses ) );
                else
                    _mashProgress = MathUtil::max( 0.0f, _mashProgress - step._decay * deltaTime );
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

    void InteractionSession::drainEvents( vector<InteractionSessionEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }
} // namespace sw
