/**
 * @file ScheduleSystemPlan.cpp
 * @brief `ScheduleSystem` 의 계획 부분입니다.
 * @details 하루 계획 세우기 · 다시 세우기, 후보 블록 모으기 · 목표 자리 정하기 · 이동 시간 넣기, 하루 시작 · 약속 확인 목록을 맡습니다.
 */
#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleActivity.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleHash.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

namespace sw
{
    SW_LOG_CALLER( "ScheduleSystemPlan" );

    namespace
    {
        struct ScheduleSystemPlanInternal
        {
            static constexpr float32 kSamePlaceDistance = 1.0e-3f;

            static int32 toDay( int32 absoluteMinute ) { return absoluteMinute / kScheduleMinutesPerDay; }
            static int32 toMinuteOfDay( int32 absoluteMinute ) { return absoluteMinute % kScheduleMinutesPerDay; }

            static bool isSamePlace( const ScheduleLocation& lhs, const ScheduleLocation& rhs )
            {
                return float3::getDistance( lhs._position, rhs._position ) <= kSamePlaceDistance;
            }

            static int32 ceilMinutes( float32 minutes )
            {
                if ( minutes <= 0.0f )
                    return 0;
                return static_cast<int32>( ::ceilf( minutes - 1.0e-4f ) );
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
    void ScheduleSystem::planNpc( int32 npcIndex, int32 originMinute, const ScheduleLocation& origin )
    {
        NpcRuntime& npc   = _listNpc[static_cast<size_t>( npcIndex )];
        npc._origin       = origin;
        npc._originMinute = originMinute;
        npc._planDay      = ScheduleSystemPlanInternal::toDay( originMinute );
        vector<ScheduleSegment> listSegment;
        makePlan( npcIndex, npc._planDay, ScheduleSystemPlanInternal::toMinuteOfDay( originMinute ), origin, listSegment );
        releaseUnused( npc._listSegment, listSegment );
        npc._listSegment = std::move( listSegment );
        npc._routeKey    = 0;
        npc._listRoutePoint.clear();
        refreshNpc( npcIndex );
    }

    void ScheduleSystem::replanNpc( int32 npcIndex, int32 minute, bool bKeepOriginIfSame )
    {
        using Internal  = ScheduleSystemPlanInternal;
        NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        if ( bKeepOriginIfSame && npc._planDay == Internal::toDay( minute ) )
        {
            // 같은 출발점으로 다시 세워 지금 칸까지 같으면 그대로 쓴다 — 바뀐 조건이 앞날만 바꾸면 지금 가는 길을 끊지 않는다.
            vector<ScheduleSegment> listTrial;
            makePlan( npcIndex, npc._planDay, Internal::toMinuteOfDay( npc._originMinute ), npc._origin, listTrial );
            const float32 minuteOfDay = static_cast<float32>( Internal::toMinuteOfDay( minute ) );
            const int32   oldActive   = Internal::findActiveSegment( npc._listSegment, minuteOfDay );
            const int32   newActive   = Internal::findActiveSegment( listTrial, minuteOfDay );
            bool          bSame       = oldActive == newActive && oldActive >= 0;
            for ( int32 segmentIndex = 0; bSame && segmentIndex <= oldActive; ++segmentIndex )
            {
                const uint64 oldKey = ScheduleHash::computeSegmentKey( npc._listSegment[static_cast<size_t>( segmentIndex )] );
                bSame               = oldKey == ScheduleHash::computeSegmentKey( listTrial[static_cast<size_t>( segmentIndex )] );
            }
            if ( bSame )
            {
                releaseUnused( npc._listSegment, listTrial );
                npc._listSegment = std::move( listTrial );
                refreshNpc( npcIndex );
                return;
            }
            releaseUnused( listTrial, npc._listSegment );
        }
        planNpc( npcIndex, minute, computeLogicLocation( npcIndex, minute ) );
    }

    void ScheduleSystem::makePlan( int32 npcIndex, int32 day, int32 originMinuteOfDay, const ScheduleLocation& origin, vector<ScheduleSegment>& outListSegment )
    {
        outListSegment.clear();
        vector<BlockCandidate> listCandidate;
        collectCandidates( npcIndex, day, listCandidate );

        // 칸 경계로 하루를 나눈 조각마다 우선순위가 가장 높은(같으면 먼저 적은) 후보가 이기고, 같은 후보가 이어진 조각은 하나로 합친다.
        vector<int32> listBoundary;
        listBoundary.push_back( 0 );
        listBoundary.push_back( kScheduleMinutesPerDay );
        for ( const BlockCandidate& candidate : listCandidate )
        {
            listBoundary.push_back( candidate._startMinute );
            listBoundary.push_back( candidate._endMinute );
        }
        VectorUtil::sortUnique( listBoundary );

        int32 currentWinner = -2;
        int32 runStart      = 0;
        for ( size_t boundaryIndex = 0; boundaryIndex + 1 < listBoundary.size(); ++boundaryIndex )
        {
            const int32 pieceStart = listBoundary[boundaryIndex];
            const int32 pieceEnd   = listBoundary[boundaryIndex + 1];
            int32       winner     = -1;
            for ( int32 candidateIndex = 0; candidateIndex < static_cast<int32>( listCandidate.size() ); ++candidateIndex )
            {
                const BlockCandidate& candidate = listCandidate[static_cast<size_t>( candidateIndex )];
                const bool            bCovers   = candidate._startMinute <= pieceStart && pieceEnd <= candidate._endMinute;
                if ( bCovers == false )
                    continue;
                const bool bBetter = winner < 0 || candidate._priority > listCandidate[static_cast<size_t>( winner )]._priority;
                if ( bBetter )
                    winner = candidateIndex;
            }
            if ( winner == currentWinner )
                continue;
            if ( currentWinner != -2 )
                resolveTarget( npcIndex, day, currentWinner >= 0 ? &listCandidate[static_cast<size_t>( currentWinner )] : nullptr, runStart, pieceStart, outListSegment );
            currentWinner = winner;
            runStart      = pieceStart;
        }
        resolveTarget( npcIndex, day, currentWinner >= 0 ? &listCandidate[static_cast<size_t>( currentWinner )] : nullptr, runStart, kScheduleMinutesPerDay,
                       outListSegment );
        planTravel( npcIndex, originMinuteOfDay, origin, outListSegment );
    }

    void ScheduleSystem::collectCandidates( int32 npcIndex, int32 day, vector<BlockCandidate>& outListCandidate ) const
    {
        outListCandidate.clear();
        const NpcRuntime&     npc    = _listNpc[static_cast<size_t>( npcIndex )];
        const ScheduleNpcDef& def    = _pCatalog->getNpcs()[static_cast<size_t>( npc._defIndex )];
        const uint32          npcKey = ScheduleHash::computeNameKey( def._id );
        int32                 order  = 0;

        for ( int32 routineIndex = 0; routineIndex < static_cast<int32>( def._listRoutine.size() ); ++routineIndex )
        {
            const ScheduleRoutineDef& routine    = def._listRoutine[static_cast<size_t>( routineIndex )];
            const uint32              routineKey = ScheduleHash::mixKey( npcKey, ScheduleHash::computeNameKey( routine._id ) );
            for ( int32 blockIndex = 0; blockIndex < static_cast<int32>( routine._listBlock.size() ); ++blockIndex )
            {
                const ScheduleBlockDef&  block   = routine._listBlock[static_cast<size_t>( blockIndex )];
                ScheduleConditionContext context = makeContext( npcIndex, day, routineKey );
                context._phase                   = WorldClock::computePhaseAt( _settings._clock, static_cast<float32>( block._startMinute ) / 60.0f );
                if ( routine._condition.matches( context ) == false )
                    continue;
                context._chanceKey = ScheduleHash::mixKey( routineKey, static_cast<uint32>( blockIndex ) );
                if ( block._condition.matches( context ) == false )
                    continue;

                BlockCandidate candidate;
                candidate._pBlock      = &block;
                candidate._sourceID    = routine._id;
                candidate._startMinute = block._startMinute;
                candidate._endMinute   = block._endMinute;
                candidate._priority    = routine._priority;
                candidate._ownerIndex  = routineIndex;
                candidate._blockIndex  = blockIndex;
                candidate._source      = ScheduleSegmentSource::Routine;
                if ( block._appointment.empty() == false )
                {
                    const int32 appointmentIndex = _pCatalog->findAppointmentIndex( block._appointment );
                    bool        bBroken          = false;
                    for ( const int32 brokenIndex : _listBrokenAppointment )
                    {
                        bBroken = bBroken || brokenIndex == appointmentIndex;
                    }
                    if ( appointmentIndex < 0 || bBroken )
                        continue;
                    const ScheduleAppointmentDef& appointment = _pCatalog->getAppointments()[static_cast<size_t>( appointmentIndex )];
                    // 약속의 확률은 참가자 모두에게 같아야 한다 — 열쇠에 NPC 를 섞지 않는다.
                    context._chanceKey = ScheduleHash::computeNameKey( appointment._id );
                    if ( appointment._condition.matches( context ) == false )
                        continue;
                    candidate._sourceID   = appointment._id;
                    candidate._priority   = MathUtil::max( routine._priority, appointment._priority );
                    candidate._ownerIndex = appointmentIndex;
                    candidate._source     = ScheduleSegmentSource::Appointment;
                }
                candidate._order = order++;
                outListCandidate.push_back( candidate );
            }
        }

        const vector<ScheduleEventDef>& listEvent = _pCatalog->getEvents();
        for ( int32 eventIndex = 0; eventIndex < static_cast<int32>( listEvent.size() ); ++eventIndex )
        {
            const ScheduleEventDef& event     = listEvent[static_cast<size_t>( eventIndex )];
            bool                    bAttendee = event._listNpc.empty() && event._listArchetype.empty();
            for ( const hashed_string& npcID : event._listNpc )
            {
                bAttendee = bAttendee || npcID == def._id;
            }
            for ( const hashed_string& archetypeID : event._listArchetype )
            {
                bAttendee = bAttendee || _pCatalog->isNpcOfArchetype( def, archetypeID );
            }
            if ( bAttendee == false )
                continue;
            // 행사의 확률도 참가자 모두에게 같다(축제는 모두에게 열리거나 아무에게도 열리지 않는다).
            const uint32 eventKey = ScheduleHash::computeNameKey( event._id );
            for ( int32 blockIndex = 0; blockIndex < static_cast<int32>( event._listBlock.size() ); ++blockIndex )
            {
                const ScheduleBlockDef&  block   = event._listBlock[static_cast<size_t>( blockIndex )];
                ScheduleConditionContext context = makeContext( npcIndex, day, eventKey );
                context._phase                   = WorldClock::computePhaseAt( _settings._clock, static_cast<float32>( block._startMinute ) / 60.0f );
                if ( event._condition.matches( context ) == false )
                    continue;
                context._chanceKey = ScheduleHash::mixKey( eventKey, static_cast<uint32>( blockIndex ) );
                if ( block._condition.matches( context ) == false )
                    continue;
                BlockCandidate candidate;
                candidate._pBlock      = &block;
                candidate._sourceID    = event._id;
                candidate._startMinute = block._startMinute;
                candidate._endMinute   = block._endMinute;
                candidate._priority    = event._priority;
                candidate._order       = order++;
                candidate._ownerIndex  = eventIndex;
                candidate._blockIndex  = blockIndex;
                candidate._source      = ScheduleSegmentSource::Event;
                outListCandidate.push_back( candidate );
            }
        }
    }

    void ScheduleSystem::resolveTarget( int32 npcIndex, int32 day, const BlockCandidate* pCandidate, int32 startMinute, int32 endMinute,
                                        vector<ScheduleSegment>& outListSegment )
    {
        const NpcRuntime&          npc    = _listNpc[static_cast<size_t>( npcIndex )];
        const ScheduleNpcDef&      def    = _pCatalog->getNpcs()[static_cast<size_t>( npc._defIndex )];
        const SchedulePlaceDef*    pHome  = _pCatalog->findPlace( def._home );
        const ScheduleLocation     home   = pHome != nullptr ? ScheduleLocation{ pHome->_position, pHome->_area } : ScheduleLocation{};
        const ScheduleBlockDef*    pBlock = pCandidate != nullptr ? pCandidate->_pBlock : nullptr;
        const hashed_string        name   = pBlock != nullptr ? pBlock->_activity : def._idleActivity;
        const ScheduleActivityDef* pDef   = _pCatalog->getActivityRegistry().findActivity( name );

        ScheduleSegment segment;
        segment._activity    = name;
        segment._animation   = pBlock != nullptr && pBlock->_animation.empty() == false ? pBlock->_animation : ( pDef != nullptr ? pDef->_animation : hashed_string{} );
        segment._startMinute = startMinute;
        segment._endMinute   = endMinute;
        segment._target      = home;
        if ( pCandidate != nullptr )
        {
            segment._sourceID   = pCandidate->_sourceID;
            segment._priority   = pCandidate->_priority;
            segment._ownerIndex = pCandidate->_ownerIndex;
            segment._blockIndex = pCandidate->_blockIndex;
            segment._source     = pCandidate->_source;
        }
        const SchedulePlaceDef*      pPlace        = pBlock != nullptr ? _pCatalog->findPlace( pBlock->_place ) : nullptr;
        const ScheduleLocation       placeLocation = pPlace != nullptr ? ScheduleLocation{ pPlace->_position, pPlace->_area } : home;
        const ScheduleActivityTarget target        = pDef != nullptr ? pDef->_target : ScheduleActivityTarget::Home;
        switch ( target )
        {
            case ScheduleActivityTarget::Home:
            {
                break;
            }
            case ScheduleActivityTarget::Place:
            case ScheduleActivityTarget::Appointment:
            {
                segment._target = placeLocation;
                break;
            }
            case ScheduleActivityTarget::SmartObject:
            {
                segment._target = placeLocation;
                ScheduleReserveRequest request;
                request._npc         = def._id;
                request._objectKind  = pBlock != nullptr ? pBlock->_objectKind : hashed_string{};
                request._area        = placeLocation._area;
                request._near        = placeLocation._position;
                request._day         = day;
                request._startMinute = startMinute;
                request._endMinute   = endMinute;
                ScheduleLocation reserved;
                uint32           reservationID = 0;
                if ( _pLocator != nullptr && _pLocator->reserve( request, reserved, reservationID ) )
                {
                    segment._target        = reserved;
                    segment._reservationID = reservationID;
                }
                break;
            }
            case ScheduleActivityTarget::Wander:
            {
                // 돌아다니기 — `every` 분마다 장소 둘레의 새 자리. 자리는 (씨앗, NPC, 날, 시각)의 해시라 몇 번을 다시 세워도 같다.
                const int32   stepMinutes = pBlock != nullptr ? MathUtil::max( 1, pBlock->_wanderMinutes ) : 20;
                const float32 radius      = pBlock != nullptr && pBlock->_radius >= 0.0f ? pBlock->_radius : ( pPlace != nullptr ? pPlace->_radius : 0.0f );
                const uint32  npcKey      = ScheduleHash::computeNameKey( def._id );
                for ( int32 hopStart = startMinute; hopStart < endMinute; hopStart += stepMinutes )
                {
                    const uint32    hash   = GameHash::hashCoord( day, hopStart, _settings._seed ^ npcKey );
                    const float32   angle  = GameHash::toUnitFloat( hash ) * ( 2.0f * MathUtil::kPi );
                    const float32   length = radius * ::sqrtf( GameHash::toUnitFloat( GameHash::mix32( hash ) ) );
                    ScheduleSegment hop    = segment;
                    hop._startMinute       = hopStart;
                    hop._endMinute         = MathUtil::min( endMinute, hopStart + stepMinutes );
                    hop._bLeaveEarly       = hopStart == startMinute ? SW_TRUE : SW_FALSE;
                    hop._target._area      = placeLocation._area;
                    hop._target._position  = _pPlanning->offsetInPlane( placeLocation._position, length * ::cosf( angle ), length * ::sinf( angle ) );
                    outListSegment.push_back( hop );
                }
                return;
            }
        }
        outListSegment.push_back( segment );
    }

    void ScheduleSystem::planTravel( int32 npcIndex, int32 originMinuteOfDay, const ScheduleLocation& origin, vector<ScheduleSegment>& inoutListSegment ) const
    {
        using Internal                   = ScheduleSystemPlanInternal;
        const NpcRuntime&      npc       = _listNpc[static_cast<size_t>( npcIndex )];
        const ScheduleNpcDef&  def       = _pCatalog->getNpcs()[static_cast<size_t>( npc._defIndex )];
        ScheduleLocation       from      = origin;
        int32                  ready     = originMinuteOfDay; ///< 앞 칸 자리에 닿은 시각 — 그 전에는 나서지 못한다
        const ScheduleSegment* pPrevious = nullptr;
        for ( ScheduleSegment& segment : inoutListSegment )
        {
            if ( segment._endMinute <= originMinuteOfDay )
            {
                segment._bPast         = SW_TRUE;
                segment._departMinute  = segment._startMinute;
                segment._travelMinutes = 0;
                continue;
            }
            // 일찍 나서기는 앞 칸의 끝을 빌린다 — 앞 칸이 더 높은 우선순위(약속 · 축제)면 끝까지 있고 늦게 닿는다.
            const bool  bMayLeaveEarly = segment._bLeaveEarly != SW_FALSE && ( pPrevious == nullptr || pPrevious->_priority <= segment._priority );
            const int32 travel         = Internal::isSamePlace( from, segment._target ) ? 0 : Internal::ceilMinutes( _pPlanning->estimateTravelMinutes( from, segment._target, def._unitsPerMinute ) );
            const int32 wanted         = bMayLeaveEarly ? segment._startMinute - travel : segment._startMinute;
            segment._departMinute      = MathUtil::max( wanted, ready );
            segment._travelMinutes     = travel;
            ready                      = segment.getArriveMinute();
            from                       = segment._target;
            pPrevious                  = &segment;
        }
    }

    void ScheduleSystem::releaseUnused( const vector<ScheduleSegment>& listOld, const vector<ScheduleSegment>& listNew )
    {
        if ( _pLocator == nullptr )
            return;
        for ( const ScheduleSegment& oldSegment : listOld )
        {
            if ( oldSegment._reservationID == 0 )
                continue;
            bool bKept = false;
            for ( const ScheduleSegment& newSegment : listNew )
            {
                bKept = bKept || newSegment._reservationID == oldSegment._reservationID;
            }
            if ( bKept == false )
                _pLocator->release( oldSegment._reservationID );
        }
    }

    void ScheduleSystem::startDay( int32 day )
    {
        _listBrokenAppointment.clear();
        _listMetAppointment.clear();
        const int32              dayStart = day * kScheduleMinutesPerDay;
        vector<ScheduleLocation> listLocation;
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            listLocation.push_back( computeLogicLocation( npcIndex, dayStart ) );
        }
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            planNpc( npcIndex, dayStart, listLocation[static_cast<size_t>( npcIndex )] );
        }
        rebuildAppointmentChecks( day );
    }

    void ScheduleSystem::rebuildAppointmentChecks( int32 day )
    {
        _listAppointmentCheck.clear();
        if ( _pCatalog == nullptr )
            return;
        const vector<ScheduleAppointmentDef>& listAppointment = _pCatalog->getAppointments();
        for ( int32 appointmentIndex = 0; appointmentIndex < static_cast<int32>( listAppointment.size() ); ++appointmentIndex )
        {
            bool bScheduled = false;
            for ( const NpcRuntime& npc : _listNpc )
            {
                for ( const ScheduleSegment& segment : npc._listSegment )
                {
                    bScheduled = bScheduled || ( segment._source == ScheduleSegmentSource::Appointment && segment._ownerIndex == appointmentIndex );
                }
            }
            if ( bScheduled == false )
                continue;
            const ScheduleAppointmentDef& appointment = listAppointment[static_cast<size_t>( appointmentIndex )];
            AppointmentCheck              check;
            check._appointmentIndex = appointmentIndex;
            check._minute           = day * kScheduleMinutesPerDay + MathUtil::min( appointment._startMinute + appointment._waitMinutes, appointment._endMinute );
            check._bDone            = check._minute <= _minute ? SW_TRUE : SW_FALSE; // 이미 지난 판정(복원) — 그 결과는 깨진 약속 목록에 있다
            _listAppointmentCheck.push_back( check );
        }
    }

    ScheduleConditionContext ScheduleSystem::makeContext( int32 npcIndex, int32 day, uint32 chanceKey ) const
    {
        const ScheduleConditionVocabulary& vocabulary = _pCatalog->getVocabulary();
        const WorldClockSettings&          clock      = _settings._clock;
        ScheduleConditionContext           context;
        context._pFlags       = _pFlags;
        context._pNpcTags     = &_listNpc[static_cast<size_t>( npcIndex )]._tags;
        context._pWorldTags   = _pWorldTags;
        context._weather      = _weather;
        context._day          = day;
        context._dayOfSeason  = day % clock._daysPerSeason + 1;
        context._affinity     = _listNpc[static_cast<size_t>( npcIndex )]._affinity;
        context._pSeasonOrder = &clock._listSeason;
        context._season       = clock._listSeason[static_cast<size_t>( ( day / clock._daysPerSeason ) % static_cast<int32>( clock._listSeason.size() ) )];
        context._chanceKey    = ScheduleHash::mixKey( chanceKey, _settings._seed );
        if ( vocabulary._listWeekday.empty() == false )
            context._weekday = vocabulary._listWeekday[static_cast<size_t>( day % static_cast<int32>( vocabulary._listWeekday.size() ) )];
        return context;
    }
} // namespace sw
