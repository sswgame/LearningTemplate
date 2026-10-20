/**
 * @file ScheduleSystemRuntime.cpp
 * @brief `ScheduleSystem` 의 평가(진행) 부분입니다.
 * @details 분 단위 진행 · 약속 판정 · NPC 상태 새로 고침 · 깨울 시각 · 사건 발행 · 조건 변화 반영 · 끼어들기 만료를 맡습니다.
 */
#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleActivity.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleHash.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

namespace sw
{
    SW_LOG_CALLER( "ScheduleSystemRuntime" );

    namespace
    {
        struct ScheduleSystemRuntimeInternal
        {
            static constexpr int32 kNoWake = 0x7fffffff;

            static int32 toDay( int32 absoluteMinute ) { return absoluteMinute / kScheduleMinutesPerDay; }
            static int32 toMinuteOfDay( int32 absoluteMinute ) { return absoluteMinute % kScheduleMinutesPerDay; }

            static int32 roundUpToStep( int32 minute, int32 step )
            {
                if ( step <= 1 )
                    return minute;
                return ( ( minute + step - 1 ) / step ) * step;
            }

            /** @brief 마지막으로 나선 칸 — 나선 시각이 @p minuteOfDay 이하인 마지막 칸입니다(출발 시각은 순서대로 늘어난다). */
            static int32 findActiveSegment( const vector<ScheduleSegment>& listSegment, float32 minuteOfDay )
            {
                int32 activeIndex = -1;
                int32 firstIndex  = -1;
                for ( int32 segmentIndex = 0; segmentIndex < static_cast<int32>( listSegment.size() ); ++segmentIndex )
                {
                    const ScheduleSegment& segment = listSegment[static_cast<size_t>( segmentIndex )];
                    if ( segment._bPast != SW_FALSE )
                        continue;
                    if ( firstIndex < 0 )
                        firstIndex = segmentIndex;
                    if ( static_cast<float32>( segment._departMinute ) <= minuteOfDay )
                        activeIndex = segmentIndex;
                }
                return activeIndex >= 0 ? activeIndex : firstIndex;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ScheduleLocation ScheduleSystem::computeLogicLocation( int32 npcIndex, int32 absoluteMinute ) const
    {
        const NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        if ( npc._listInterruption.empty() == false )
            return npc._held;
        const float32 minuteOfDay = static_cast<float32>( absoluteMinute - npc._planDay * kScheduleMinutesPerDay );
        const int32   active      = ScheduleSystemRuntimeInternal::findActiveSegment( npc._listSegment, minuteOfDay );
        if ( active < 0 )
            return npc._origin;
        float32                fraction = 1.0f;
        const ScheduleNpcPhase phase    = computePhase( npc, active, minuteOfDay, fraction );
        const ScheduleSegment& segment  = npc._listSegment[static_cast<size_t>( active )];
        if ( phase != ScheduleNpcPhase::Traveling )
            return segment._target;
        return computePlanningLocationOnRoute( npc, active, fraction );
    }

    ScheduleNpcPhase ScheduleSystem::computePhase( const NpcRuntime& npc, int32 segmentIndex, float32 minuteOfDay, float32& outFraction ) const
    {
        outFraction = 1.0f;
        if ( npc._listInterruption.empty() == false )
            return ScheduleNpcPhase::Interrupted;
        const ScheduleSegment& segment = npc._listSegment[static_cast<size_t>( segmentIndex )];
        const float32          arrive  = static_cast<float32>( segment.getArriveMinute() );
        if ( segment._travelMinutes <= 0 || minuteOfDay >= arrive )
            return ScheduleNpcPhase::Performing;
        outFraction = MathUtil::saturate( ( minuteOfDay - static_cast<float32>( segment._departMinute ) ) / static_cast<float32>( segment._travelMinutes ) );
        return ScheduleNpcPhase::Traveling;
    }

    ScheduleLocation ScheduleSystem::computePlanningLocationOnRoute( const NpcRuntime& npc, int32 segmentIndex, float32 fraction ) const
    {
        const ScheduleSegment&  segment = npc._listSegment[static_cast<size_t>( segmentIndex )];
        const ScheduleLocation& from    = getStartLocation( npc, segmentIndex );
        vector<float3>          listPoint;
        _pPlanning->makeRoute( from, segment._target, listPoint );
        ScheduleLocation location;
        location._position = SchedulePathingUtil::computePointAlong( listPoint, fraction );
        location._area     = _pPlanning->computeAreaAt( from, segment._target, fraction );
        return location;
    }

    const ScheduleLocation& ScheduleSystem::getStartLocation( const NpcRuntime& npc, int32 segmentIndex ) const
    {
        for ( int32 previousIndex = segmentIndex - 1; previousIndex >= 0; --previousIndex )
        {
            const ScheduleSegment& previous = npc._listSegment[static_cast<size_t>( previousIndex )];
            if ( previous._bPast == SW_FALSE )
                return previous._target;
        }
        return npc._origin;
    }

    bool ScheduleSystem::isAttendeePresent( int32 npcIndex, int32 appointmentIndex ) const
    {
        if ( isValidNpc( npcIndex ) == false )
            return false;
        const NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        if ( npc._listInterruption.empty() == false )
            return false;
        const float32 minuteOfDay = static_cast<float32>( _minute - npc._planDay * kScheduleMinutesPerDay );
        const int32   active      = ScheduleSystemRuntimeInternal::findActiveSegment( npc._listSegment, minuteOfDay );
        if ( active < 0 )
            return false;
        const ScheduleSegment& segment  = npc._listSegment[static_cast<size_t>( active )];
        float32                fraction = 1.0f;
        const bool             bThere   = segment._source == ScheduleSegmentSource::Appointment && segment._ownerIndex == appointmentIndex;
        return bThere && computePhase( npc, active, minuteOfDay, fraction ) == ScheduleNpcPhase::Performing;
    }

    void ScheduleSystem::processMinute( int32 minute )
    {
        if ( ScheduleSystemRuntimeInternal::toMinuteOfDay( minute ) == 0 )
        {
            const int32 day  = ScheduleSystemRuntimeInternal::toDay( minute );
            bool        bNew = false;
            for ( const NpcRuntime& npc : _listNpc )
            {
                bNew = bNew || npc._planDay != day;
            }
            if ( bNew )
                startDay( day );
        }
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            popExpiredInterruptions( npcIndex, minute );
        }
        for ( AppointmentCheck& check : _listAppointmentCheck )
        {
            if ( check._bDone == SW_FALSE && check._minute <= minute )
                processAppointmentCheck( check );
        }
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            if ( _listNpc[static_cast<size_t>( npcIndex )]._wakeMinute <= minute )
                refreshNpc( npcIndex );
        }
    }

    void ScheduleSystem::processAppointmentCheck( AppointmentCheck& inoutCheck )
    {
        inoutCheck._bDone                         = SW_TRUE;
        const ScheduleAppointmentDef& appointment = _pCatalog->getAppointments()[static_cast<size_t>( inoutCheck._appointmentIndex )];
        if ( isAppointmentMet( appointment._id ) )
            return;
        // 기다려도 모두 오지 않았다 — 그날 약속은 깨지고, 그 약속을 계획에 둔 참가자는 지금 자리에서 그 아래 칸으로 간다.
        _listBrokenAppointment.push_back( inoutCheck._appointmentIndex );
        for ( const hashed_string& attendee : appointment._listAttendee )
        {
            const int32 npcIndex = findNpcIndex( attendee );
            if ( npcIndex < 0 )
                continue;
            bool bPlanned = false;
            for ( const ScheduleSegment& segment : _listNpc[static_cast<size_t>( npcIndex )]._listSegment )
            {
                bPlanned = bPlanned || ( segment._source == ScheduleSegmentSource::Appointment && segment._ownerIndex == inoutCheck._appointmentIndex );
            }
            if ( bPlanned == false )
                continue;
            emitEvent( ScheduleEvent::Kind::AppointmentBroken, npcIndex, nullptr, appointment._id );
            replanNpc( npcIndex, _minute, false );
        }
    }

    void ScheduleSystem::refreshNpc( int32 npcIndex )
    {
        using Internal  = ScheduleSystemRuntimeInternal;
        NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        if ( npc._listInterruption.empty() == false )
        {
            npc._wakeMinute = computeWakeMinute( npcIndex );
            return;
        }
        const float32 minuteOfDay = static_cast<float32>( _minute - npc._planDay * kScheduleMinutesPerDay );
        const int32   active      = Internal::findActiveSegment( npc._listSegment, minuteOfDay );
        if ( active >= 0 )
        {
            const ScheduleSegment& segment  = npc._listSegment[static_cast<size_t>( active )];
            float32                fraction = 1.0f;
            const ScheduleNpcPhase phase    = computePhase( npc, active, minuteOfDay, fraction );
            const uint64           key      = ScheduleHash::computeSegmentKey( segment );
            const bool             bChanged = npc._bEmitted == SW_FALSE || ScheduleHash::computeSegmentKey( npc._emittedSegment ) != key;
            if ( bChanged )
            {
                if ( npc._bEmitted != SW_FALSE && npc._emittedPhase == ScheduleNpcPhase::Performing )
                    emitEvent( ScheduleEvent::Kind::ActivityEnded, npcIndex, &npc._emittedSegment, npc._emittedSegment._sourceID );
                if ( phase == ScheduleNpcPhase::Traveling )
                    emitEvent( ScheduleEvent::Kind::Departed, npcIndex, &segment, segment._sourceID );
            }
            const bool bArrivedNow = phase == ScheduleNpcPhase::Performing && ( bChanged || npc._emittedPhase != ScheduleNpcPhase::Performing );
            if ( bArrivedNow )
            {
                emitEvent( ScheduleEvent::Kind::Arrived, npcIndex, &segment, segment._sourceID );
                emitEvent( ScheduleEvent::Kind::ActivityStarted, npcIndex, &segment, segment._sourceID );
                const bool bAppointment = segment._source == ScheduleSegmentSource::Appointment;
                bool       bMetBefore   = false;
                for ( const int32 metIndex : _listMetAppointment )
                {
                    bMetBefore = bMetBefore || metIndex == segment._ownerIndex;
                }
                const bool bFirstAppointmentMet = bAppointment && bMetBefore == false && isAppointmentMet( segment._sourceID );
                if ( bFirstAppointmentMet )
                {
                    _listMetAppointment.push_back( segment._ownerIndex );
                    emitEvent( ScheduleEvent::Kind::AppointmentMet, npcIndex, &segment, segment._sourceID );
                }
            }
            npc._emittedSegment = segment;
            npc._emittedPhase   = phase;
            npc._bEmitted       = SW_TRUE;
        }
        npc._wakeMinute = computeWakeMinute( npcIndex );
    }

    int32 ScheduleSystem::computeWakeMinute( int32 npcIndex ) const
    {
        using Internal         = ScheduleSystemRuntimeInternal;
        const NpcRuntime& npc  = _listNpc[static_cast<size_t>( npcIndex )];
        int32             wake = Internal::kNoWake;
        // 끼어들기 만료는 늘 그 분에 — 계획의 출발점이 되므로 LOD 와 상관없이 같아야 한다.
        for ( const Interruption& interruption : npc._listInterruption )
        {
            if ( interruption._expireMinute > _minute )
                wake = MathUtil::min( wake, interruption._expireMinute );
        }
        if ( npc._listInterruption.empty() == false )
            return wake;
        int32       segmentWake = Internal::kNoWake;
        const int32 dayStart    = npc._planDay * kScheduleMinutesPerDay;
        for ( const ScheduleSegment& segment : npc._listSegment )
        {
            if ( segment._bPast != SW_FALSE )
                continue;
            const int32 arrArrivalBoundary[] = { segment._departMinute, segment.getArriveMinute(), segment._endMinute };
            for ( const int32 boundary : arrArrivalBoundary )
            {
                if ( dayStart + boundary > _minute )
                    segmentWake = MathUtil::min( segmentWake, dayStart + boundary );
            }
        }
        // 화면 밖은 상태 사건만 굵은 간격으로 미룬다(계획 · 자리 · 판정은 시각의 함수라 그대로다).
        if ( npc._lod == ScheduleLOD::Far && segmentWake != Internal::kNoWake )
            segmentWake = Internal::roundUpToStep( segmentWake, _settings._farStepMinutes );
        return MathUtil::min( wake, segmentWake );
    }

    void ScheduleSystem::emitEvent( ScheduleEvent::Kind kind, int32 npcIndex, const ScheduleSegment* pSegment, const hashed_string& sourceID )
    {
        if ( _bSuppressEvents != SW_FALSE )
            return;
        ScheduleEvent event;
        event._kind     = kind;
        event._npcIndex = npcIndex;
        event._minute   = _minute;
        event._sourceID = sourceID;
        if ( pSegment != nullptr )
        {
            event._activity  = pSegment->_activity;
            event._animation = pSegment->_animation;
        }
        _eventBuffer.push( event );

        const NpcRuntime& npc         = _listNpc[static_cast<size_t>( npcIndex )];
        const bool        bAnimation  = kind == ScheduleEvent::Kind::ActivityStarted || kind == ScheduleEvent::Kind::ActivityEnded;
        const bool        bShouldPlay = _pAnimator != nullptr && npc._lod == ScheduleLOD::Near && bAnimation && pSegment != nullptr;
        if ( bShouldPlay == false )
            return;
        ScheduleActivityCue cue;
        cue._location  = pSegment->_target;
        cue._activity  = pSegment->_activity;
        cue._animation = pSegment->_animation;
        cue._npc       = getNpcID( npcIndex );
        cue._npcIndex  = npcIndex;
        if ( kind == ScheduleEvent::Kind::ActivityStarted )
            _pAnimator->onActivityStarted( cue );
        else
            _pAnimator->onActivityEnded( cue );
    }

    void ScheduleSystem::emitSnapped( int32 npcIndex )
    {
        const ScheduleNpcView view = getNpcView( npcIndex );
        emitEvent( ScheduleEvent::Kind::Snapped, npcIndex, nullptr, view._interruption );
        if ( view._phase != ScheduleNpcPhase::Performing || view._segmentIndex < 0 )
            return;
        const NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        emitEvent( ScheduleEvent::Kind::ActivityStarted, npcIndex, &npc._listSegment[static_cast<size_t>( view._segmentIndex )], npc._listSegment[static_cast<size_t>( view._segmentIndex )]._sourceID );
    }

    void ScheduleSystem::applyConditionChanges()
    {
        const bool bFlagsChanged = _pFlags != nullptr && _pFlags->getRevision() != _flagsRevision;
        const bool bAll          = _bConditionsDirty != SW_FALSE || bFlagsChanged;
        _flagsRevision           = _pFlags != nullptr ? _pFlags->getRevision() : 0;
        _bConditionsDirty        = SW_FALSE;
        bool bAnyReplanned       = false;
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
            if ( bAll == false && npc._bTagsDirty == SW_FALSE )
                continue;
            npc._bTagsDirty = SW_FALSE;
            replanNpc( npcIndex, _minute, true );
            bAnyReplanned = true;
        }
        if ( bAnyReplanned )
            rebuildAppointmentChecks( getDay() );
    }

    void ScheduleSystem::updateNearRoutes()
    {
        using Internal = ScheduleSystemRuntimeInternal;
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
            if ( npc._lod != ScheduleLOD::Near || npc._listInterruption.empty() == false )
                continue;
            const int32 active = Internal::findActiveSegment( npc._listSegment, static_cast<float32>( _minute - npc._planDay * kScheduleMinutesPerDay ) );
            if ( active < 0 )
                continue;
            const ScheduleSegment& segment = npc._listSegment[static_cast<size_t>( active )];
            const uint64           key     = ScheduleHash::computeSegmentKey( segment );
            if ( key == npc._routeKey || segment._travelMinutes <= 0 )
                continue;
            _pFine->makeRoute( getStartLocation( npc, active ), segment._target, npc._listRoutePoint );
            npc._routeKey = key;
        }
    }

    void ScheduleSystem::popExpiredInterruptions( int32 npcIndex, int32 minute )
    {
        NpcRuntime& npc      = _listNpc[static_cast<size_t>( npcIndex )];
        bool        bRemoved = false;
        for ( size_t interruptionIndex = npc._listInterruption.size(); interruptionIndex > 0; --interruptionIndex )
        {
            const Interruption& interruption = npc._listInterruption[interruptionIndex - 1];
            if ( interruption._expireMinute < 0 || interruption._expireMinute > minute )
                continue;
            npc._listInterruption.erase( npc._listInterruption.begin() + static_cast<ptrdiff_t>( interruptionIndex - 1 ) );
            bRemoved = true;
        }
        if ( bRemoved && npc._listInterruption.empty() )
            resumeNpc( npcIndex );
    }

    void ScheduleSystem::resumeNpc( int32 npcIndex )
    {
        NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        emitEvent( ScheduleEvent::Kind::Resumed, npcIndex, nullptr, hashed_string{} );
        npc._bEmitted = SW_FALSE;
        // 끼어든 동안에도 일정 시계는 흘렀다 — 지금 시각의 칸으로, 끼어든 자리에서 다시 세운다.
        planNpc( npcIndex, _minute, npc._held );
    }
} // namespace sw
