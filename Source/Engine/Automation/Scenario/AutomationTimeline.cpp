#include "pch.h"

#include "Engine/Automation/Scenario/AutomationTimeline.h"

#include "Core/String/StringBuilder.h"

#include <algorithm>

namespace sw
{
    AutomationTimeline::AutomationTimeline()
        : _arrSlice{}
        , _listSlowestWait{}
        , _listStepKindCost{}
        , _pendingWaitFrameCount{ 0 }
        , _pendingWaitSeconds{ 0.0 }
    {
    }

    void AutomationTimeline::recordFrame( AutomationFrameKind kind, float64 seconds )
    {
        AutomationTimelineSlice& slice = _arrSlice[static_cast<uint32>( kind )];
        ++slice._frameCount;
        slice._seconds += seconds;
        const bool bWaiting = kind == AutomationFrameKind::ScheduledWait || kind == AutomationFrameKind::ConditionHold;
        if ( bWaiting )
        {
            ++_pendingWaitFrameCount;
            _pendingWaitSeconds += seconds;
        }
    }

    void AutomationTimeline::closeWait( const string& stepKind, uint32 stepNumber, uint32 frameIndex )
    {
        if ( _pendingWaitFrameCount == 0 )
            return;
        const uint32  waitedFrameCount = _pendingWaitFrameCount;
        const float64 waitedSeconds    = _pendingWaitSeconds;
        _pendingWaitFrameCount         = 0;
        _pendingWaitSeconds            = 0.0;
        // 목록은 긴 순으로 유지한다 — 가득 찼을 때 맨 끝(가장 짧은 것)보다 길지 않으면 이 대기는 버린다(단계 이름 복사도 아낀다).
        const bool bFull = _listSlowestWait.size() >= kSlowestWaitKeep;
        if ( bFull && waitedSeconds <= _listSlowestWait.back()._seconds )
            return;
        AutomationSlowWait wait;
        wait._stepKind         = stepKind;
        wait._stepNumber       = stepNumber;
        wait._frameIndex       = frameIndex;
        wait._waitedFrameCount = waitedFrameCount;
        wait._seconds          = waitedSeconds;
        size_t insertIndex     = _listSlowestWait.size();
        while ( insertIndex > 0 && _listSlowestWait[insertIndex - 1]._seconds < waitedSeconds )
        {
            --insertIndex;
        }
        _listSlowestWait.insert( _listSlowestWait.begin() + static_cast<ptrdiff_t>( insertIndex ), std::move( wait ) );
        if ( _listSlowestWait.size() > kSlowestWaitKeep )
            _listSlowestWait.pop_back();
    }

    void AutomationTimeline::recordStepFrame( const string& stepKind, float64 seconds )
    {
        for ( AutomationStepKindCost& cost : _listStepKindCost )
        {
            if ( cost._stepKind == stepKind )
            {
                ++cost._frameCount;
                cost._seconds += seconds;
                return;
            }
        }
        _listStepKindCost.push_back( AutomationStepKindCost{ stepKind, 1, seconds } );
    }

    string AutomationTimeline::describe() const
    {
        static constexpr size_t kStepKindKeep  = 4;
        const auto              toMilliseconds = []( float64 seconds )
        { return static_cast<uint32>( seconds * 1000.0 + 0.5 ); };
        string line;
        for ( uint32 kindIndex = 0; kindIndex < kFrameKindCount; ++kindIndex )
        {
            const AutomationTimelineSlice& slice = _arrSlice[kindIndex];
            line += formatToString( "%#%# %#ms/%#f", line.empty() ? "" : ", ", getKindKey( static_cast<AutomationFrameKind>( kindIndex ) ), toMilliseconds( slice._seconds ),
                                    slice._frameCount );
        }
        vector<AutomationStepKindCost> listCost = _listStepKindCost;
        std::sort( listCost.begin(), listCost.end(), []( const AutomationStepKindCost& left, const AutomationStepKindCost& right )
        { return left._seconds > right._seconds; } );
        line += "; steps";
        for ( size_t index = 0; index < listCost.size() && index < kStepKindKeep; ++index )
            line += formatToString( " %# %#ms/%#f", listCost[index]._stepKind, toMilliseconds( listCost[index]._seconds ), listCost[index]._frameCount );
        if ( _listSlowestWait.empty() == false )
        {
            const AutomationSlowWait& wait = _listSlowestWait.front();
            line += formatToString( "; longest wait %#ms/%#f before %# #%#", toMilliseconds( wait._seconds ), wait._waitedFrameCount, wait._stepKind, wait._stepNumber );
        }
        return line;
    }

    const utf8* AutomationTimeline::getKindKey( AutomationFrameKind kind )
    {
        switch ( kind )
        {
            case AutomationFrameKind::Start:
                return "start";
            case AutomationFrameKind::BackgroundHold:
                return "backgroundHold";
            case AutomationFrameKind::ConditionHold:
                return "held";
            case AutomationFrameKind::StepRan:
                return "step";
            case AutomationFrameKind::ScheduledWait:
                return "scheduledWait";
        }
        return "";
    }
} // namespace sw
