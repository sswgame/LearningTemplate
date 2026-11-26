#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractionSession.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    InteractionSession::InteractionSession()
        : _pDef{}
        , _holdProgress{}
        , _eventBuffer{}
        , _listHoldEvent{}
        , _actorID{ 0 }
        , _beginCount{ 0 }
        , _stepIndex{ 0 }
        , _mashProgress{ 0.0f }
        , _reactionTime{ 0.0f }
        , _reactionOpenTime{ 0.0f }
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
        ++_beginCount;
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
            case InteractionInputMode::Reaction:
            {
                GameRandom random( HashUtil::hashCoord( static_cast<int32>( _beginCount ), _stepIndex, _actorID ) );
                _reactionTime     = 0.0f;
                _reactionOpenTime = random.nextRange( step._delayMin, step._delayMax );
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

    void InteractionSession::update( GameSeconds frameTime, bool bHeld, bool bPressed )
    {
        const float32 deltaTime = frameTime.count();
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
                _holdProgress.update( frameTime );
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
            case InteractionInputMode::Reaction:
            {
                _reactionTime += deltaTime;
                if ( bPressed && isReactionOpen() )
                    completeStep();
                else if ( bPressed || _reactionTime > _reactionOpenTime + step._window )
                    cancel(); // 먼저 눌렀거나 놓쳤다
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
            case InteractionInputMode::Reaction:
                return isReactionOpen() ? 1.0f - MathUtil::saturate( ( _reactionTime - _reactionOpenTime ) / pStep->_window ) : 0.0f;
        }
        return 0.0f;
    }

    bool InteractionSession::isReactionOpen() const
    {
        const InteractionStepDef* pStep = getStep();
        return pStep != nullptr && pStep->_mode == InteractionInputMode::Reaction && _reactionOpenTime <= _reactionTime && _reactionTime <= _reactionOpenTime + pStep->_window;
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
