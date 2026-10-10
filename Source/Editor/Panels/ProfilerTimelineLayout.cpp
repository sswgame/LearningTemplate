#include "pch.h"

#include "Editor/Panels/ProfilerTimelineLayout.h"

#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    namespace
    {
        struct ProfilerTimelineLayoutInternal
        {
            /** @brief 범위를 한도 안으로 옮깁니다. 범위가 한도보다 넓으면 한도 전체입니다. */
            static void clampRange( uint64& inoutBegin, uint64& inoutEnd, uint64 limitBegin, uint64 limitEnd )
            {
                const uint64 limitSpan = limitEnd > limitBegin ? limitEnd - limitBegin : 0;
                const uint64 span      = inoutEnd > inoutBegin ? inoutEnd - inoutBegin : 0;
                if ( span >= limitSpan )
                {
                    inoutBegin = limitBegin;
                    inoutEnd   = limitEnd;
                    return;
                }
                if ( inoutBegin < limitBegin )
                {
                    inoutBegin = limitBegin;
                    inoutEnd   = limitBegin + span;
                }
                if ( inoutEnd > limitEnd )
                {
                    inoutEnd   = limitEnd;
                    inoutBegin = limitEnd - span;
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    float32 ProfilerTimelineLayout::computeX( uint64 nanos, uint64 visibleBegin, uint64 visibleEnd, float32 width )
    {
        if ( visibleEnd <= visibleBegin )
            return 0.0f;
        const float64 offset = static_cast<float64>( nanos ) - static_cast<float64>( visibleBegin );
        const float64 span   = static_cast<float64>( visibleEnd - visibleBegin );
        return static_cast<float32>( offset / span * static_cast<float64>( width ) );
    }

    void ProfilerTimelineLayout::layoutRects( const vector<ProfilerTimelineThread>& listThread, uint64 visibleBegin, uint64 visibleEnd, float32 width,
                                              vector<ProfilerTimelineRect>& outListRect, vector<uint16>& outListLaneCount )
    {
        outListRect.clear();
        outListLaneCount.assign( listThread.size(), 1 );
        if ( visibleEnd <= visibleBegin || width <= 0.0f )
            return;

        for ( uint32 threadIndex = 0; threadIndex < static_cast<uint32>( listThread.size() ); ++threadIndex )
        {
            const vector<ProfilerTimelineEvent>& listEvent = listThread[threadIndex]._listEvent;
            for ( uint32 eventIndex = 0; eventIndex < static_cast<uint32>( listEvent.size() ); ++eventIndex )
            {
                const ProfilerTimelineEvent& event = listEvent[eventIndex];
                outListLaneCount[threadIndex]      = MathUtil::max( outListLaneCount[threadIndex], static_cast<uint16>( event._depth + 1 ) );
                const bool bOutside                = event._endNanos <= visibleBegin || event._beginNanos >= visibleEnd;
                if ( bOutside )
                    continue;
                ProfilerTimelineRect rect;
                rect._x0          = MathUtil::max( 0.0f, computeX( event._beginNanos, visibleBegin, visibleEnd, width ) );
                rect._x1          = MathUtil::min( width, computeX( event._endNanos, visibleBegin, visibleEnd, width ) );
                rect._x1          = MathUtil::max( rect._x1, MathUtil::min( width, rect._x0 + kMinRectWidthPixels ) );
                rect._threadIndex = threadIndex;
                rect._eventIndex  = eventIndex;
                rect._depth       = event._depth;
                rect._bShowsLabel = ( rect._x1 - rect._x0 ) >= kMinLabelWidthPixels ? SW_TRUE : SW_FALSE;
                outListRect.push_back( rect );
            }
        }
    }

    void ProfilerTimelineLayout::zoom( uint64& inoutBegin, uint64& inoutEnd, float32 pivotX, float32 width, float32 factor, uint64 limitBegin, uint64 limitEnd )
    {
        if ( inoutEnd <= inoutBegin || width <= 0.0f || factor <= 0.0f )
            return;
        const float64 span      = static_cast<float64>( inoutEnd - inoutBegin );
        const float64 pivotT    = MathUtil::clamp( static_cast<float64>( pivotX ) / static_cast<float64>( width ), 0.0, 1.0 );
        const float64 pivotTime = static_cast<float64>( inoutBegin ) + span * pivotT;
        const float64 newSpan   = MathUtil::max( span * static_cast<float64>( factor ), static_cast<float64>( kMinSpanNanos ) );
        const float64 newBegin  = MathUtil::max( 0.0, pivotTime - newSpan * pivotT );
        inoutBegin              = static_cast<uint64>( newBegin );
        inoutEnd                = static_cast<uint64>( newBegin + newSpan );
        ProfilerTimelineLayoutInternal::clampRange( inoutBegin, inoutEnd, limitBegin, limitEnd );
    }

    void ProfilerTimelineLayout::pan( uint64& inoutBegin, uint64& inoutEnd, float32 deltaPixels, float32 width, uint64 limitBegin, uint64 limitEnd )
    {
        if ( inoutEnd <= inoutBegin || width <= 0.0f )
            return;
        const float64 span       = static_cast<float64>( inoutEnd - inoutBegin );
        const float64 deltaNanos = -static_cast<float64>( deltaPixels ) / static_cast<float64>( width ) * span;
        const float64 newBegin   = MathUtil::max( 0.0, static_cast<float64>( inoutBegin ) + deltaNanos );
        inoutBegin               = static_cast<uint64>( newBegin );
        inoutEnd                 = static_cast<uint64>( newBegin + span );
        ProfilerTimelineLayoutInternal::clampRange( inoutBegin, inoutEnd, limitBegin, limitEnd );
    }
} // namespace sw::editor
