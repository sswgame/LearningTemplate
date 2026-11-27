/**
 * @file ScheduleSystemDebug.cpp
 * @brief `ScheduleSystem` 의 디버그 설명(`explainNPC` · `dumpTimeline`)입니다. 개발 명령 · 감시 · 에디터 일정 디버그 창만 불러 `Dev/` 에 두고 배포 구성은 짓지 않는다.
 * @details 언리얼 Gameplay Debugger 가 `WITH_GAMEPLAY_DEBUGGER` 로 빠지는 것과 같다. 멤버라 GameFrameworkDev 로 옮기지 못한다(다른 모듈은 내보낸 클래스의 멤버를 정의하지 못한다).
 */
#include "pch.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystemInternal.h"

namespace sw
{
    void ScheduleSystem::explainNPC( int32 npcIndex, string& outText ) const
    {
        using Internal = ScheduleSystemInternal;
        outText.clear();
        if ( _pCatalog == nullptr || isValidNPC( npcIndex ) == false )
            return;
        const NPCRuntime&                         npc     = _listNPC[static_cast<size_t>( npcIndex )];
        const NPCScheduleDef&                     def     = _pCatalog->getNPCs()[static_cast<size_t>( npc._defIndex )];
        const NPCScheduleView                     view    = getNPCView( npcIndex );
        const ScheduleConditionContext            context = makeContext( npcIndex, npc._planDay, 0 );
        const int32                               now     = Internal::toMinuteOfDay( _minute );
        StringBuilder<constant::buffer::kMax4096> text;
        text.appendFormat( "npc '%#' day %# (%#, %#, day %# of season, weather '%#') ", def._id.c_str(), npc._planDay, context._weekday.c_str(), context._season.c_str(),
                           context._dayOfSeason, _weather.c_str() );
        Internal::appendClock( text, now );
        text.appendFormat( " - %# %# at (%#, %#, %#) area '%#' [%#]\n", getEnumName( view._phase ), view._activity.c_str(), view._location._position._x,
                           view._location._position._y, view._location._position._z, view._location._area.c_str(), getEnumName( view._lod ) );
        if ( view._segmentIndex.has_value() )
        {
            const ScheduleSegment& segment = npc._listSegment[static_cast<size_t>( *view._segmentIndex )];
            text.appendFormat( "  active: %# '%#' block #%# ", getEnumName( segment._source ), segment._sourceID.c_str(), segment._blockIndex );
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
                blockContext._phase                         = WorldClock::computePhaseAt( _settings._clock, static_cast<float32>( block._startMinute ) / static_cast<float32>( kScheduleMinutesPerHour ) );
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
        using Internal = ScheduleSystemInternal;
        outText.clear();
        if ( _pCatalog == nullptr || isValidNPC( npcIndex ) == false )
            return;
        const NPCRuntime&                         npc = _listNPC[static_cast<size_t>( npcIndex )];
        StringBuilder<constant::buffer::kMax4096> text;
        text.appendFormat( "timeline '%#' day %# (planned at ", getNPCID( npcIndex ).c_str(), npc._planDay );
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
            text.appendFormat( " [%# '%#' p%#]\n", getEnumName( segment._source ), segment._sourceID.c_str(), segment._priority );
        }
        outText.assign( text.c_str() );
    }
} // namespace sw
