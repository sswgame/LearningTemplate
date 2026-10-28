/**
 * @file ScheduleSystemDebug.cpp
 * @brief `ScheduleSystem` 의 디버그 출력 부분입니다.
 * @details "왜 여기 있나" 설명 · 오늘 시간표 덤프 · `-gv_scheduleTrace` 요청 처리를 맡습니다.
 */
#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleActivity.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleHash.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

namespace sw
{
    SW_LOG_CALLER( "ScheduleSystemDebug" );

    namespace
    {
        struct ScheduleSystemDebugInternal
        {
            static int32 toMinuteOfDay( int32 absoluteMinute ) { return absoluteMinute % kScheduleMinutesPerDay; }

            template <uint32 Capacity>
            static void appendClock( StringBuilder<Capacity>& inoutText, int32 minuteOfDay )
            {
                const int32 hours   = minuteOfDay / 60;
                const int32 minutes = minuteOfDay % 60;
                inoutText.appendFormat( "%#%#:%#%#", hours < 10 ? "0" : "", hours, minutes < 10 ? "0" : "", minutes );
            }
        };
    } // namespace

    /** @brief `-gv_scheduleTrace=<npc id | *>` — 값이 바뀌면 그 NPC 의 "왜 여기 있나" 와 오늘 시간표를 로그에 한 번 남깁니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_scheduleTrace, "", "Schedule: log the why-am-I-here trace and today's timeline of this NPC id (* = all) when the value changes" );
} // namespace sw

namespace sw
{
    void ScheduleSystem::explainNpc( int32 npcIndex, string& outText ) const
    {
        using Internal = ScheduleSystemDebugInternal;
        outText.clear();
        if ( _pCatalog == nullptr || isValidNpc( npcIndex ) == false )
            return;
        const NpcRuntime&                       npc     = _listNpc[static_cast<size_t>( npcIndex )];
        const ScheduleNpcDef&                   def     = _pCatalog->getNpcs()[static_cast<size_t>( npc._defIndex )];
        const ScheduleNpcView                   view    = getNpcView( npcIndex );
        const ScheduleConditionContext          context = makeContext( npcIndex, npc._planDay, 0 );
        const int32                             now     = Internal::toMinuteOfDay( _minute );
        StringBuilder<constant::kMaxBuffer4096> text;
        text.appendFormat( "npc '%#' day %# (%#, %#, day %# of season, weather '%#') ", def._id.c_str(), npc._planDay, context._weekday.c_str(), context._season.c_str(),
                           context._dayOfSeason, _weather.c_str() );
        Internal::appendClock( text, now );
        text.appendFormat( " - %# %# at (%#, %#, %#) area '%#' [%#]\n", getEnumName( view._phase ), view._activity.c_str(), view._location._position._x,
                           view._location._position._y, view._location._position._z, view._location._area.c_str(), getEnumName( view._lod ) );
        if ( view._segmentIndex >= 0 )
        {
            const ScheduleSegment& segment = npc._listSegment[static_cast<size_t>( view._segmentIndex )];
            text.appendFormat( "  active: %# '%#' block #%# ", toString( segment._source ), segment._sourceID.c_str(), segment._blockIndex );
            Internal::appendClock( text, segment._startMinute );
            text.append( "-" );
            Internal::appendClock( text, segment._endMinute );
            text.appendFormat( " priority %#, left ", segment._priority );
            Internal::appendClock( text, segment._departMinute );
            text.appendFormat( " (travel %# min) for '%#'\n", segment._travelMinutes, segment._target._area.c_str() );
        }
        if ( npc._listInterruption.empty() )
            text.append( "  interruptions: none\n" );
        for ( size_t interruptionIndex = npc._listInterruption.size(); interruptionIndex > 0; --interruptionIndex )
        {
            const Interruption& interruption = npc._listInterruption[interruptionIndex - 1];
            text.appendFormat( "  interruption: %# priority %# since minute %# until %#\n", interruption._id.c_str(), interruption._priority, interruption._startMinute,
                               interruption._expireMinute );
        }

        // 후보 — 지금 시각을 덮는 루틴 · 행사 칸과 그 조건 결과.
        text.append( "  candidates now:\n" );
        for ( int32 routineIndex = 0; routineIndex < static_cast<int32>( def._listRoutine.size() ); ++routineIndex )
        {
            const ScheduleRoutineDef& routine    = def._listRoutine[static_cast<size_t>( routineIndex )];
            const uint32              routineKey = ScheduleHash::mixKey( ScheduleHash::computeNameKey( def._id ), ScheduleHash::computeNameKey( routine._id ) );
            for ( int32 blockIndex = 0; blockIndex < static_cast<int32>( routine._listBlock.size() ); ++blockIndex )
            {
                const ScheduleBlockDef& block = routine._listBlock[static_cast<size_t>( blockIndex )];
                if ( block._startMinute > now || now >= block._endMinute )
                    continue;
                ScheduleConditionContext blockContext       = makeContext( npcIndex, npc._planDay, routineKey );
                blockContext._phase                         = WorldClock::computePhaseAt( _settings._clock, static_cast<float32>( block._startMinute ) / 60.0f );
                const ScheduleConditionResult routineResult = routine._condition.evaluate( blockContext );
                blockContext._chanceKey                     = ScheduleHash::mixKey( routineKey, static_cast<uint32>( blockIndex ) );
                const ScheduleConditionResult blockResult   = block._condition.evaluate( blockContext );
                text.appendFormat( "    %# routine '%#' p%# block #%# %#", routineResult.isPassed() && blockResult.isPassed() ? "+" : "-", routine._id.c_str(),
                                   routine._priority, blockIndex, block._activity.c_str() );
                for ( uint32 clauseIndex = 0; clauseIndex < static_cast<uint32>( ScheduleConditionClause::Count ); ++clauseIndex )
                {
                    const ScheduleConditionClause clause = static_cast<ScheduleConditionClause>( clauseIndex );
                    if ( routineResult.hasFailed( clause ) || blockResult.hasFailed( clause ) )
                        text.appendFormat( " [%# failed]", toString( clause ) );
                }
                if ( block._appointment.empty() == false && isAppointmentBroken( block._appointment ) )
                    text.appendFormat( " [appointment '%#' broken]", block._appointment.c_str() );
                text.append( "\n" );
            }
        }
        const vector<ScheduleEventDef>& listEvent = _pCatalog->getEvents();
        for ( const ScheduleEventDef& event : listEvent )
        {
            ScheduleConditionContext      eventContext = makeContext( npcIndex, npc._planDay, ScheduleHash::computeNameKey( event._id ) );
            const ScheduleConditionResult result       = event._condition.evaluate( eventContext );
            text.appendFormat( "    %# event '%#' p%#", result.isPassed() ? "+" : "-", event._id.c_str(), event._priority );
            for ( uint32 clauseIndex = 0; clauseIndex < static_cast<uint32>( ScheduleConditionClause::Count ); ++clauseIndex )
            {
                const ScheduleConditionClause clause = static_cast<ScheduleConditionClause>( clauseIndex );
                if ( result.hasFailed( clause ) )
                    text.appendFormat( " [%# failed]", toString( clause ) );
            }
            text.append( "\n" );
        }
        outText.assign( text.c_str() );
    }

    void ScheduleSystem::dumpTimeline( int32 npcIndex, string& outText ) const
    {
        using Internal = ScheduleSystemDebugInternal;
        outText.clear();
        if ( _pCatalog == nullptr || isValidNpc( npcIndex ) == false )
            return;
        const NpcRuntime&                       npc = _listNpc[static_cast<size_t>( npcIndex )];
        StringBuilder<constant::kMaxBuffer4096> text;
        text.appendFormat( "timeline '%#' day %# (planned at ", getNpcID( npcIndex ).c_str(), npc._planDay );
        Internal::appendClock( text, Internal::toMinuteOfDay( npc._originMinute ) );
        text.append( ")\n" );
        for ( const ScheduleSegment& segment : npc._listSegment )
        {
            text.append( segment._bPast != SW_FALSE ? "  (past) " : "  " );
            Internal::appendClock( text, segment._startMinute );
            text.append( "-" );
            Internal::appendClock( text, segment._endMinute );
            text.appendFormat( " %# @ '%#' (%#, %#, %#)", segment._activity.c_str(), segment._target._area.c_str(), segment._target._position._x, segment._target._position._y,
                               segment._target._position._z );
            if ( segment._travelMinutes > 0 )
            {
                text.append( " leave " );
                Internal::appendClock( text, segment._departMinute );
                text.appendFormat( " (+%# min)", segment._travelMinutes );
            }
            text.appendFormat( " [%# '%#' p%#]\n", toString( segment._source ), segment._sourceID.c_str(), segment._priority );
        }
        outText.assign( text.c_str() );
    }

    void ScheduleSystem::logTraceRequest()
    {
        if ( gv_scheduleTrace == _lastTraceRequest )
            return;
        _lastTraceRequest = gv_scheduleTrace;
        if ( _lastTraceRequest.empty() )
            return;
        const bool bAll = _lastTraceRequest == "*";
        string     text;
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            if ( bAll == false && getNpcID( npcIndex ) != hashed_string( _lastTraceRequest ) )
                continue;
            explainNpc( npcIndex, text );
            SW_LOG_INFO( "[Schedule] %#", text );
            dumpTimeline( npcIndex, text );
            SW_LOG_INFO( "[Schedule] %#", text );
        }
    }
} // namespace sw
