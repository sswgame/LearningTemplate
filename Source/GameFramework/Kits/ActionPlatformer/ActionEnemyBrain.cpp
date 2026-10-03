#include "pch.h"

#include "GameFramework/Kits/ActionPlatformer/ActionEnemyBrain.h"

#include "GameFramework/Kits/ActionPlatformer/ActionPlatformerCatalog.h"

namespace sw
{
    ActionEnemyBrain::ActionEnemyBrain()
        : _pPattern{ nullptr }
        , _stateIndex{ -1 }
        , _stateFrame{ 0 }
        , _facing{ 1 }
        , _bEntered{ SW_FALSE }
    {
    }

    bool ActionEnemyBrain::initialize( const ActionPatternDef* pPattern, int32 facing )
    {
        _pPattern   = pPattern;
        _stateIndex = -1;
        setFacing( facing );
        if ( pPattern == nullptr || pPattern->_listState.empty() )
            return false;
        const int32 startIndex = pPattern->findStateIndex( pPattern->_start );
        enterState( startIndex >= 0 ? startIndex : 0 );
        return true;
    }

    ActionEnemyAction ActionEnemyBrain::advanceFrame( float32 playerDistance )
    {
        ActionEnemyAction            action;
        const ActionPatternStateDef* pState = getState();
        if ( pState == nullptr )
            return action;
        // 들어선 프레임에는 넘어가지 않는다 — 상태마다 적어도 한 프레임은 보인다.
        if ( _bEntered == SW_FALSE )
        {
            ++_stateFrame;
            int32 targetIndex = -1;
            if ( pState->_onNear.empty() == false && playerDistance <= pState->_nearRange )
                targetIndex = _pPattern->findStateIndex( pState->_onNear );
            if ( targetIndex < 0 && pState->_next.empty() == false && _stateFrame >= pState->_frames )
                targetIndex = _pPattern->findStateIndex( pState->_next );
            if ( targetIndex >= 0 && targetIndex != _stateIndex )
                enterState( targetIndex );
            else if ( targetIndex == _stateIndex )
                _stateFrame = 0; // 같은 상태로 — 시간만 다시 센다
            pState = getState();
        }
        action._moveX         = pState->_moveX * static_cast<float32>( _facing );
        action._bAttack       = pState->_bAttack;
        action._bStateChanged = _bEntered;
        if ( _bEntered == SW_TRUE && pState->_bFire == SW_TRUE )
        {
            action._bFire     = SW_TRUE;
            action._fireSpeed = pState->_fireSpeed;
        }
        _bEntered = SW_FALSE;
        return action;
    }

    bool ActionEnemyBrain::notifyHit()
    {
        const ActionPatternStateDef* pState = getState();
        if ( pState == nullptr || pState->_onHit.empty() )
            return false;
        const int32 targetIndex = _pPattern->findStateIndex( pState->_onHit );
        if ( targetIndex < 0 )
            return false;
        enterState( targetIndex );
        return true;
    }

    const hashed_string& ActionEnemyBrain::getStateId() const
    {
        const ActionPatternStateDef* pState = getState();
        if ( pState != nullptr )
            return pState->_id;
        static const hashed_string s_empty{};
        return s_empty;
    }

    void ActionEnemyBrain::enterState( int32 stateIndex )
    {
        _stateIndex = stateIndex;
        _stateFrame = 0;
        _bEntered   = SW_TRUE;
    }

    const ActionPatternStateDef* ActionEnemyBrain::getState() const
    {
        if ( _pPattern == nullptr || _stateIndex < 0 || _stateIndex >= static_cast<int32>( _pPattern->_listState.size() ) )
            return nullptr;
        return &_pPattern->_listState[static_cast<size_t>( _stateIndex )];
    }
} // namespace sw
