#include "pch.h"

#include "Engine/Animation/AnimPlayback.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct AnimNotifyTrackInternal
        {
            /** @brief 울린 알림의 순서 — 시각이 이르면 먼저, 같으면 끝이 먼저입니다. */
            static bool isFiredBefore( const AnimFiredNotify& lhs, const AnimFiredNotify& rhs )
            {
                if ( lhs._time != rhs._time )
                    return lhs._time < rhs._time;
                const bool bLhsEnd = lhs._phase == AnimNotifyPhase::End;
                const bool bRhsEnd = rhs._phase == AnimNotifyPhase::End;
                return bLhsEnd && bRhsEnd == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void AnimClipCursor::reset( float32 time )
    {
        _time   = MathUtil::max( time, 0.0f );
        _bFresh = SW_TRUE;
    }

    AnimTimeStep AnimClipCursor::advance( float32 deltaSeconds, float32 playLength, bool bLoop )
    {
        AnimTimeStep step{};
        step._previousTime   = _time;
        step._bIncludesStart = _bFresh;
        _bFresh              = SW_FALSE;
        if ( playLength <= 0.0f )
        {
            step._currentTime = _time;
            return step;
        }

        float32 time = _time + MathUtil::max( deltaSeconds, 0.0f );
        if ( bLoop )
        {
            // 한 바퀴 안으로 감는다 — 끝없이 키우면 10^6 초 근처에서 한 프레임을 더해도 값이 움직이지 않는다.
            if ( time >= playLength )
            {
                const float32 wrapCount = MathUtil::floor( time / playLength );
                time -= wrapCount * playLength;
                step._wrapCount = static_cast<uint32>( wrapCount );
                if ( time >= playLength || time < 0.0f )
                    time = 0.0f;
            }
        }
        else if ( time >= playLength )
        {
            time              = playLength;
            step._bReachedEnd = SW_TRUE;
        }
        _time             = time;
        step._currentTime = time;
        return step;
    }

    float32 AnimClipCursor::computeNormalizedTime( float32 playLength ) const
    {
        return playLength > 0.0f ? MathUtil::clamp( _time / playLength, 0.0f, 1.0f ) : 0.0f;
    }

    void AnimClipCursor::setNormalizedTime( float32 normalizedTime, float32 playLength )
    {
        _time = playLength > 0.0f ? MathUtil::clamp( normalizedTime, 0.0f, 1.0f ) * playLength : 0.0f;
    }

    void AnimNotifyTrack::addEvent( const AnimNotifyEvent& event )
    {
        size_t insertIndex = _listEvent.size();
        while ( insertIndex > 0 && _listEvent[insertIndex - 1]._time > event._time )
            --insertIndex;
        _listEvent.insert( _listEvent.begin() + static_cast<ptrdiff_t>( insertIndex ), event );
    }

    void AnimNotifyTrack::collectFired( const AnimTimeStep& step, float32 playLength, float32 weight, const IAnimPlayable* pSource,
                                        vector<AnimFiredNotify>& outListFired ) const
    {
        if ( _listEvent.empty() )
            return;
        const bool bIncludeStart = step._bIncludesStart == SW_TRUE;
        if ( step._wrapCount == 0 )
        {
            collectRange( step._previousTime, step._currentTime, bIncludeStart, playLength, weight, pSource, outListFired );
            return;
        }
        collectRange( step._previousTime, playLength, bIncludeStart, playLength, weight, pSource, outListFired );
        for ( uint32 loopIndex = 1; loopIndex < step._wrapCount; ++loopIndex )
            collectRange( 0.0f, playLength, true, playLength, weight, pSource, outListFired );
        collectRange( 0.0f, step._currentTime, true, playLength, weight, pSource, outListFired );
    }

    float32 AnimNotifyTrack::computeEndTime( const AnimNotifyEvent& event, float32 playLength )
    {
        const float32 endTime = event._time + event._duration;
        return playLength > 0.0f ? MathUtil::min( endTime, playLength ) : endTime;
    }

    void AnimNotifyTrack::collectRange( float32 fromTime, float32 toTime, bool bIncludeFrom, float32 playLength, float32 weight, const IAnimPlayable* pSource,
                                        vector<AnimFiredNotify>& outListFired ) const
    {
        const size_t firstAdded = outListFired.size();
        for ( size_t eventIndex = 0; eventIndex < _listEvent.size(); ++eventIndex )
        {
            const AnimNotifyEvent& event         = _listEvent[eventIndex];
            const bool             bState        = event._duration > 0.0f;
            const float32          endTime       = bState ? computeEndTime( event, playLength ) : 0.0f;
            const bool             bStartCrossed = ( bIncludeFrom ? event._time >= fromTime : event._time > fromTime ) && event._time <= toTime;
            // 끝은 시작과 같은 구간 규칙이다 — 다만 시작 시각을 포함하는 첫 걸음에서도 끝 시각 자체가 from 이면 이미 닫힌 구간이라 뺀다.
            const bool bEndCrossed = bState && endTime > fromTime && endTime <= toTime;
            if ( bStartCrossed )
            {
                AnimFiredNotify fired{};
                fired._name       = event._name;
                fired._pSource    = pSource;
                fired._time       = event._time;
                fired._weight     = weight;
                fired._duration   = event._duration;
                fired._eventIndex = static_cast<uint32>( eventIndex );
                fired._phase      = bState ? AnimNotifyPhase::Begin : AnimNotifyPhase::Instant;
                outListFired.push_back( fired );
            }
            if ( bEndCrossed )
            {
                AnimFiredNotify fired{};
                fired._name       = event._name;
                fired._pSource    = pSource;
                fired._time       = endTime;
                fired._weight     = weight;
                fired._duration   = event._duration;
                fired._eventIndex = static_cast<uint32>( eventIndex );
                fired._phase      = AnimNotifyPhase::End;
                outListFired.push_back( fired );
            }
        }
        // 시각 순서로 — 같은 시각이면 끝이 먼저다(앞 구간이 닫힌 뒤 다음 구간이 열린다).
        std::stable_sort( outListFired.begin() + static_cast<ptrdiff_t>( firstAdded ), outListFired.end(),
                          AnimNotifyTrackInternal::isFiredBefore );
    }
} // namespace sw
