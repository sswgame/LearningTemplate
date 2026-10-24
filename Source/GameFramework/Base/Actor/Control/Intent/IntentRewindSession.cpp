#include "pch.h"

#include "GameFramework/Base/Actor/Control/Intent/IntentRewindSession.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    SW_LOG_CALLER( "IntentRewindSession" );

    IntentRewindSession::IntentRewindSession()
        : _buffer{}
        , _pWorld{ nullptr }
        , _targetTick{ 0 }
        , _bSeeking{ SW_FALSE }
    {
    }

    bool IntentRewindSession::initialize( const IntentRewindConfig& config, IIntentRewindWorld* pWorld )
    {
        if ( pWorld == nullptr )
        {
            SW_LOG_ERROR( "IntentRewindSession: refused to start - there is no world to rewind" );
            return false;
        }
        if ( _buffer.initialize( config ) == false )
            return false; // 이유는 버퍼가 남겼다. 이전 상태 그대로다
        _pWorld     = pWorld;
        _targetTick = 0;
        _bSeeking   = SW_FALSE;
        return true;
    }

    bool IntentRewindSession::captureIfDue()
    {
        if ( _pWorld == nullptr )
            return true;
        const uint32 tick = _pWorld->getTick();
        if ( _buffer.isKeyTick( tick ) == false )
            return true;
        const IntentRewindBuffer::KeySnapshot* pExisting = _buffer.findAtOrBefore( tick );
        if ( pExisting != nullptr && pExisting->_tick == tick )
            return true; // 되감은 자리를 다시 돌다 만난 키 — 이미 있다
        vector<uint8> bytes;
        if ( _pWorld->captureKey( bytes ) == false )
        {
            SW_LOG_ERROR( "IntentRewindSession: the world could not capture the key at tick %#", tick );
            return false;
        }
        return _buffer.add( tick, std::move( bytes ) );
    }

    bool IntentRewindSession::beginSeek( uint32 targetTick )
    {
        if ( _pWorld == nullptr )
        {
            SW_LOG_ERROR( "IntentRewindSession: seek before initialize" );
            return false;
        }
        if ( targetTick < _pWorld->getTick() )
        {
            const IntentRewindBuffer::KeySnapshot* pKey = _buffer.findAtOrBefore( targetTick );
            if ( pKey == nullptr )
            {
                SW_LOG_ERROR( "IntentRewindSession: no key at or before tick %# - it is older than the oldest kept key (memory cap or recording start)", targetTick );
                return false;
            }
            const uint32        keyTick = pKey->_tick;
            const vector<uint8> bytes   = pKey->_bytes; // 되돌리는 동안 버퍼를 건드리지 않도록 복사한다
            if ( _pWorld->restoreKey( keyTick, bytes ) == false )
            {
                SW_LOG_ERROR( "IntentRewindSession: the world could not restore the key at tick %#", keyTick );
                return false;
            }
            // 되돌린 자리에서 다시 돌면 미래가 갈라진다 — 목표 뒤의 키는 버린다(목표 앞의 키는 같은 궤적이라 남긴다).
            _buffer.discardAfter( targetTick );
        }
        _targetTick = targetTick;
        _bSeeking   = _pWorld->getTick() < targetTick ? SW_TRUE : SW_FALSE;
        return true;
    }

    uint32 IntentRewindSession::stepSeek( uint32 maxTickCount )
    {
        if ( isSeeking() == false || _pWorld == nullptr )
            return 0;
        uint32 stepped = 0;
        while ( stepped < maxTickCount && _pWorld->getTick() < _targetTick )
        {
            if ( captureIfDue() == false )
                SW_LOG_WARNING( "IntentRewindSession: a key was not kept at tick %# while seeking - the seek goes on", _pWorld->getTick() );
            _pWorld->stepTick();
            ++stepped;
        }
        const uint32 remaining = getRemainingTickCount();
        if ( remaining == 0 )
            _bSeeking = SW_FALSE;
        return remaining;
    }

    void IntentRewindSession::cancelSeek()
    {
        _bSeeking = SW_FALSE;
    }

    uint32 IntentRewindSession::getRemainingTickCount() const
    {
        if ( _bSeeking == SW_FALSE || _pWorld == nullptr )
            return 0;
        const uint32 tick = _pWorld->getTick();
        return tick < _targetTick ? _targetTick - tick : 0;
    }

    bool IntentRewindSession::findEarliestSeekableTick( uint32& outTick ) const
    {
        const IntentRewindBuffer::KeySnapshot* pOldest = _buffer.findOldest();
        if ( pOldest == nullptr )
            return false;
        outTick = pOldest->_tick;
        return true;
    }
} // namespace sw
