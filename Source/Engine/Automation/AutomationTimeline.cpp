#include "pch.h"

#include "Engine/Automation/AutomationTimeline.h"

namespace sw
{
    AutomationTimeline::AutomationTimeline()
        : _arrSlice{}
        , _listSlowestWait{}
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
