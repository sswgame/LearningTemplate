#include "pch.h"

#include "Engine/Animation/AnimPlayer.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    AnimPlayer::AnimPlayer()
        : _arrSlot{}
        , _fadeDuration{ 0.0f }
        , _fadeElapsed{ 0.0f }
        , _speed{ 1.0f }
        , _interruptCount{ 0 }
    {
    }

    void AnimPlayer::play( const IAnimPlayable* pPlayable, bool bLoop )
    {
        _arrSlot[0]            = Slot{};
        _arrSlot[0]._pPlayable = pPlayable;
        _arrSlot[0]._bLoop     = bLoop ? SW_TRUE : SW_FALSE;
        _arrSlot[1]            = Slot{};
        _fadeDuration          = 0.0f;
        _fadeElapsed           = 0.0f;
    }

    void AnimPlayer::crossfade( const IAnimPlayable* pPlayable, float32 fadeSeconds, bool bLoop )
    {
        if ( pPlayable == nullptr || _arrSlot[0]._pPlayable == nullptr || fadeSeconds <= 0.0f )
        {
            play( pPlayable, bLoop );
            return;
        }
        // 페이드 중에 또 넘어가면 지금 섞이던 다음 칸이 새 출발점이다(가중치가 더 큰 쪽을 남긴다). 버린 칸만큼 포즈가 튀므로 끊긴 수를 센다 —
        // 포즈를 내는 쪽(스켈레탈 애니메이터)이 직전 포즈에서 이어 섞는다.
        if ( _arrSlot[1]._pPlayable != nullptr )
            ++_interruptCount;
        if ( _arrSlot[1]._pPlayable != nullptr && getBlendAlpha() >= 0.5f )
            _arrSlot[0] = _arrSlot[1];
        _arrSlot[1]            = Slot{};
        _arrSlot[1]._pPlayable = pPlayable;
        _arrSlot[1]._bLoop     = bLoop ? SW_TRUE : SW_FALSE;
        _fadeDuration          = fadeSeconds;
        _fadeElapsed           = 0.0f;
    }

    void AnimPlayer::update( float32 deltaSeconds, vector<AnimFiredNotify>* pOutListFired )
    {
        const float32 delta = MathUtil::max( deltaSeconds, 0.0f ) * _speed;
        const float32 alpha = getBlendAlpha();
        for ( uint32 slotIndex = 0; slotIndex < 2; ++slotIndex )
        {
            Slot& slot = _arrSlot[slotIndex];
            if ( slot._pPlayable == nullptr )
                continue;
            const float32 playLength      = slot._pPlayable->getPlayLength();
            slot._lastStep                = slot._cursor.advance( delta, playLength, slot._bLoop == SW_TRUE );
            const AnimNotifyTrack* pTrack = slot._pPlayable->findNotifyTrack();
            if ( pTrack != nullptr && pOutListFired != nullptr )
                pTrack->collectFired( slot._lastStep, playLength, slotIndex == 0 ? 1.0f - alpha : alpha, slot._pPlayable, *pOutListFired );
        }

        if ( _arrSlot[1]._pPlayable == nullptr )
            return;
        _fadeElapsed += delta;
        if ( _fadeElapsed < _fadeDuration )
            return;
        _arrSlot[0]   = _arrSlot[1];
        _arrSlot[1]   = Slot{};
        _fadeDuration = 0.0f;
        _fadeElapsed  = 0.0f;
    }

    bool AnimPlayer::hasFinished() const
    {
        const Slot& current = _arrSlot[0];
        if ( current._pPlayable == nullptr )
            return true;
        if ( current._bLoop == SW_TRUE || _arrSlot[1]._pPlayable != nullptr )
            return false;
        return current._cursor.getTime() >= current._pPlayable->getPlayLength();
    }

    float32 AnimPlayer::getBlendAlpha() const
    {
        if ( _arrSlot[1]._pPlayable == nullptr || _fadeDuration <= 0.0f )
            return 0.0f;
        return MathUtil::clamp( _fadeElapsed / _fadeDuration, 0.0f, 1.0f );
    }

    void AnimPlayer::setCurrentTime( float32 time )
    {
        const float32 playLength = _arrSlot[0]._pPlayable != nullptr ? _arrSlot[0]._pPlayable->getPlayLength() : 0.0f;
        _arrSlot[0]._cursor.reset( MathUtil::clamp( time, 0.0f, MathUtil::max( playLength, 0.0f ) ) );
    }

    float32 AnimPlayer::getCurrentNormalizedTime() const
    {
        const Slot& current = _arrSlot[0];
        return current._pPlayable != nullptr ? current._cursor.computeNormalizedTime( current._pPlayable->getPlayLength() ) : 0.0f;
    }

    void AnimPlayer::setNormalizedTime( float32 normalizedTime )
    {
        for ( Slot& slot : _arrSlot )
        {
            if ( slot._pPlayable != nullptr )
                slot._cursor.setNormalizedTime( normalizedTime, slot._pPlayable->getPlayLength() );
        }
    }

    int32 AnimSyncGroup::synchronize( AnimPlayer* const* ppPlayer, const float32* pWeight, uint32 count )
    {
        int32   leaderIndex  = -1;
        float32 leaderWeight = -1.0f;
        for ( uint32 index = 0; index < count; ++index )
        {
            if ( ppPlayer[index] == nullptr || ppPlayer[index]->getCurrentPlayable() == nullptr )
                continue;
            if ( pWeight[index] > leaderWeight )
            {
                leaderWeight = pWeight[index];
                leaderIndex  = static_cast<int32>( index );
            }
        }
        if ( leaderIndex < 0 )
            return leaderIndex;

        const float32 phase = ppPlayer[leaderIndex]->getCurrentNormalizedTime();
        for ( uint32 index = 0; index < count; ++index )
        {
            if ( static_cast<int32>( index ) != leaderIndex && ppPlayer[index] != nullptr )
                ppPlayer[index]->setNormalizedTime( phase );
        }
        return leaderIndex;
    }

    void AnimParameterSet::setFloat( const hashed_string& name, float32 value )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._name == name )
            {
                entry._value = value;
                return;
            }
        }
        Entry entry{};
        entry._name  = name;
        entry._value = value;
        _listEntry.push_back( entry );
    }

    float32 AnimParameterSet::getFloat( const hashed_string& name ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._name == name )
                return entry._value;
        }
        return 0.0f;
    }
} // namespace sw
