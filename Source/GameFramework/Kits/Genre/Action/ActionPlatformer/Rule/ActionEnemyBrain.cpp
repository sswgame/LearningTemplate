#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Rule/ActionEnemyBrain.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"

namespace sw
{
    ActionEnemyBrain::ActionEnemyBrain()
        : _pPattern{ nullptr }
        , _pCatalog{ nullptr }
        , _stateIndex{}
        , _stateFrame{ 0 }
        , _facing{ 1 }
        , _bEntered{ SW_FALSE }
    {
    }

    bool ActionEnemyBrain::initialize( const ActionPatternDef* pPattern, int32 facing )
    {
        setFacing( facing );
        const optional<uint32> startIndex = pPattern != nullptr ? pPattern->findStateIndex( pPattern->_start ) : nullopt;
        if ( startIndex.has_value() == false )
        {
            // 첫 상태로 대신 시작하지 않는다. 실패하면 앞 패턴의 진행도 남기지 않는다.
            _pPattern   = nullptr;
            _stateIndex = nullopt;
            _stateFrame = 0;
            _bEntered   = SW_FALSE;
            return false;
        }
        _pPattern = pPattern;
        enterState( *startIndex );
        return true;
    }

    ActionEnemyAction ActionEnemyBrain::advanceFrame( float32 playerDistance )
    {
        ActionEnemyAction            action;
        const ActionPatternStateDef* pState = getState();
        if ( pState == nullptr )
            return action;
        // 들어선 프레임에는 다음 상태로 넘어가지 않는다. 상태마다 적어도 한 프레임은 보여야 하기 때문이다.
        if ( _bEntered == SW_FALSE )
        {
            ++_stateFrame;
            optional<uint32> targetIndex;
            if ( pState->_onNear.empty() == false && playerDistance <= pState->_nearRange )
                targetIndex = _pPattern->findStateIndex( pState->_onNear );
            const bool bShouldAdvanceToNextState = targetIndex.has_value() == false && pState->_next.empty() == false && _stateFrame >= pState->_frames;
            if ( bShouldAdvanceToNextState )
                targetIndex = _pPattern->findStateIndex( pState->_next );
            if ( targetIndex.has_value() && targetIndex != _stateIndex )
                enterState( *targetIndex );
            else if ( targetIndex.has_value() )
                _stateFrame = 0; // 같은 상태로 머물고 시간만 다시 센다
            pState = getState();
        }
        action._moveX         = pState->_moveX * static_cast<float32>( _facing );
        action._bAttack       = pState->_bAttack ? SW_TRUE : SW_FALSE;
        action._bStateChanged = _bEntered;
        if ( _bEntered == SW_TRUE && pState->_bFire )
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
        const optional<uint32> targetIndex = _pPattern->findStateIndex( pState->_onHit );
        if ( targetIndex.has_value() == false )
            return false;
        enterState( *targetIndex );
        return true;
    }

    const hashed_string& ActionEnemyBrain::getStateID() const
    {
        const ActionPatternStateDef* pState = getState();
        if ( pState != nullptr )
            return pState->_id;
        static const hashed_string s_empty{};
        return s_empty;
    }

    void ActionEnemyBrain::enterState( uint32 stateIndex )
    {
        _stateIndex = stateIndex;
        _stateFrame = 0;
        _bEntered   = SW_TRUE;
    }

    const ActionPatternStateDef* ActionEnemyBrain::getState() const
    {
        const bool bStateIndexInvalid = _pPattern == nullptr || _stateIndex.has_value() == false || *_stateIndex >= _pPattern->_listState.size();
        if ( bStateIndexInvalid )
            return nullptr;
        return &_pPattern->_listState[*_stateIndex];
    }

    void ActionEnemyBrain::serializeState( StateIO& io )
    {
        io.reference( _pPattern, [this]( const hashed_string& id )
        { return _pCatalog != nullptr ? _pCatalog->findPattern( id ) : nullptr; } );
        hashed_string stateID = io.isWriting() ? getStateID() : hashed_string{};
        io.value( stateID );
        io.value( _stateFrame );
        io.value( _facing );
        io.flag( _bEntered );
        io.check( 0 <= _stateFrame && ( _facing == -1 || _facing == 1 ) );
        if ( io.isReading() )
        {
            _stateIndex = _pPattern != nullptr && stateID.empty() == false ? _pPattern->findStateIndex( stateID ) : nullopt;
            io.check( stateID.empty() || _stateIndex.has_value() ); // 이름이 있는데 찾지 못하면 다른 데이터의 바이트다
        }
    }

} // namespace sw
