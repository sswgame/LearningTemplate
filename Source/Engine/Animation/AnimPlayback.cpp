#include "pch.h"

#include "Engine/Animation/AnimPlayback.h"

#include "Core/Math/MathUtil.h"

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

    void AnimNotifyTrack::collectFired( const AnimTimeStep& step, float32 playLength, float32 weight, vector<AnimFiredNotify>& outListFired ) const
    {
        if ( _listEvent.empty() )
            return;
        const bool bIncludeStart = step._bIncludesStart == SW_TRUE;
        if ( step._wrapCount == 0 )
        {
            collectRange( step._previousTime, step._currentTime, bIncludeStart, weight, outListFired );
            return;
        }
        collectRange( step._previousTime, playLength, bIncludeStart, weight, outListFired );
        for ( uint32 loopIndex = 1; loopIndex < step._wrapCount; ++loopIndex )
            collectRange( 0.0f, playLength, true, weight, outListFired );
        collectRange( 0.0f, step._currentTime, true, weight, outListFired );
    }

    void AnimNotifyTrack::collectRange( float32 fromTime, float32 toTime, bool bIncludeFrom, float32 weight, vector<AnimFiredNotify>& outListFired ) const
    {
        for ( const AnimNotifyEvent& event : _listEvent )
        {
            const bool bAfterFrom = bIncludeFrom ? event._time >= fromTime : event._time > fromTime;
            if ( bAfterFrom == false )
                continue;
            if ( event._time > toTime )
                break;
            AnimFiredNotify fired{};
            fired._name   = event._name;
            fired._time   = event._time;
            fired._weight = weight;
            outListFired.push_back( fired );
        }
    }
} // namespace sw
