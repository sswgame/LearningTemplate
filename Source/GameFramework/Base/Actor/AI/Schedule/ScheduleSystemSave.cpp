/**
 * @file ScheduleSystemSave.cpp
 * @brief `ScheduleSystem` 의 저장 · 복제 부분입니다.
 * @details 저장 상태 채우기 · 되살리기, 네트워크 요약 만들기 · 바이트 인코딩 · 디코딩, 결정성 확인용 상태 해시를 맡습니다.
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
    SW_LOG_CALLER( "ScheduleSystemSave" );

    namespace
    {
        struct ScheduleSystemSaveInternal
        {
            static constexpr size_t kNetHeaderSize = 6;  ///< 분 int32 + 수 uint16
            static constexpr size_t kNetEntrySize  = 26; ///< 자리 3 × float32 + 지역 · 활동 해시 2 × uint32 + NPC uint16 + 칸 int16 + 상태 2 × uint8

            static int32 toDay( int32 absoluteMinute ) { return absoluteMinute / kScheduleMinutesPerDay; }

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

            static void writeBytes( vector<uint8>& outBytes, const void* pData, size_t size )
            {
                const uint8* pByte = static_cast<const uint8*>( pData );
                outBytes.insert( outBytes.end(), pByte, pByte + size );
            }

            template <typename T>
            static void writeValue( vector<uint8>& outBytes, const T& value )
            {
                // 엔진은 리틀 엔디언(x64 · ARM64)에서만 돈다 — 바이트 그대로가 리틀 엔디언이다.
                writeBytes( outBytes, &value, sizeof( T ) );
            }

            template <typename T>
            static T readValue( const vector<uint8>& bytes, size_t& inoutOffset )
            {
                T value{};
                ::memcpy( &value, bytes.data() + inoutOffset, sizeof( T ) );
                inoutOffset += sizeof( T );
                return value;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void ScheduleSystem::fillSaveState( ScheduleSaveState& outState ) const
    {
        outState          = ScheduleSaveState{};
        outState._minute  = _minute;
        outState._seed    = _settings._seed;
        outState._weather = _weather;
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            const NpcRuntime&    npc = _listNpc[static_cast<size_t>( npcIndex )];
            ScheduleNpcSaveState npcState;
            npcState._id             = getNpcID( npcIndex );
            npcState._originArea     = npc._origin._area;
            npcState._originPosition = npc._origin._position;
            npcState._originMinute   = npc._originMinute;
            npcState._heldArea       = npc._held._area;
            npcState._heldPosition   = npc._held._position;
            npcState._affinity       = npc._affinity;
            for ( const Interruption& interruption : npc._listInterruption )
            {
                ScheduleInterruptionSaveState interruptionState;
                interruptionState._id           = interruption._id;
                interruptionState._startMinute  = interruption._startMinute;
                interruptionState._expireMinute = interruption._expireMinute;
                npcState._listInterruption.push_back( interruptionState );
            }
            for ( const TagID& tag : npc._tags.getTags() )
            {
                npcState._listTag.push_back( tag.getString() != nullptr ? string( tag.getString() ) : string() );
            }
            outState._listNpc.push_back( npcState );
        }
        for ( const int32 appointmentIndex : _listBrokenAppointment )
        {
            outState._listBrokenAppointment.push_back( string( _pCatalog->getAppointments()[static_cast<size_t>( appointmentIndex )]._id.c_str() ) );
        }
        if ( _pLocator == &_defaultLocator )
            _defaultLocator.fillState( outState._listReservation );
    }

    void ScheduleSystem::restoreSaveState( const ScheduleSaveState& state )
    {
        if ( _pCatalog == nullptr )
            return;
        _bSuppressEvents = SW_TRUE;
        for ( NpcRuntime& npc : _listNpc )
        {
            releaseUnused( npc._listSegment, vector<ScheduleSegment>{} ); // 예약은 아래에서 저장된 것으로 바꾼다
            npc._listSegment.clear();
            npc._listInterruption.clear();
        }
        _minute         = MathUtil::max( 0, state._minute );
        _minuteFraction = 0.0f;
        _settings._seed = state._seed;
        _weather        = state._weather;
        _eventBuffer.clear();
        _listMetAppointment.clear();
        _listBrokenAppointment.clear();
        for ( const string& appointmentID : state._listBrokenAppointment )
        {
            const int32 appointmentIndex = _pCatalog->findAppointmentIndex( hashed_string( appointmentID ) );
            if ( appointmentIndex >= 0 )
                _listBrokenAppointment.push_back( appointmentIndex );
        }
        if ( _pLocator == &_defaultLocator )
            _defaultLocator.restoreState( state._listReservation );

        vector<uint8> listRestored( _listNpc.size(), SW_FALSE );
        for ( const ScheduleNpcSaveState& npcState : state._listNpc )
        {
            const int32 npcIndex = findNpcIndex( npcState._id );
            if ( npcIndex < 0 )
                continue;
            NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
            npc._affinity   = npcState._affinity;
            npc._tags.clear();
            for ( const string& tagName : npcState._listTag )
            {
                npc._tags.addTag( TagID::request( tagName ) );
            }
            for ( const ScheduleInterruptionSaveState& interruptionState : npcState._listInterruption )
            {
                const ScheduleInterruptDef* pDef = _pCatalog->findInterrupt( interruptionState._id );
                if ( pDef == nullptr )
                    continue;
                npc._listInterruption.push_back( Interruption{ interruptionState._id, pDef->_priority, interruptionState._startMinute, interruptionState._expireMinute } );
            }
            npc._held                     = ScheduleLocation{ npcState._heldPosition, npcState._heldArea };
            const ScheduleLocation origin = ScheduleLocation{ npcState._originPosition, npcState._originArea };
            const bool             bToday = ScheduleSystemSaveInternal::toDay( npcState._originMinute ) == getDay() && npcState._originMinute <= _minute;
            planNpc( npcIndex, bToday ? npcState._originMinute : _minute, origin );
            listRestored[static_cast<size_t>( npcIndex )] = SW_TRUE;
        }
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            if ( listRestored[static_cast<size_t>( npcIndex )] == SW_FALSE )
                planNpc( npcIndex, _minute, _listNpc[static_cast<size_t>( npcIndex )]._origin );
        }
        rebuildAppointmentChecks( getDay() );
        _flagsRevision    = _pFlags != nullptr ? _pFlags->getRevision() : 0;
        _bConditionsDirty = SW_FALSE;
        _bSuppressEvents  = SW_FALSE;
        updateNearRoutes();
    }

    void ScheduleSystem::fillNetSummary( vector<ScheduleNetSummary>& outListSummary ) const
    {
        outListSummary.clear();
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            const NpcRuntime&      npc         = _listNpc[static_cast<size_t>( npcIndex )];
            const float32          minuteOfDay = static_cast<float32>( _minute - npc._planDay * kScheduleMinutesPerDay );
            const int32            active      = ScheduleSystemSaveInternal::findActiveSegment( npc._listSegment, minuteOfDay );
            float32                fraction    = 1.0f;
            const ScheduleLocation location    = computeLogicLocation( npcIndex, _minute );
            ScheduleNetSummary     summary;
            summary._position          = location._position;
            summary._areaHash          = ScheduleHash::computeNameKey( location._area );
            summary._npcIndex          = static_cast<uint16>( npcIndex );
            summary._segmentIndex      = static_cast<int16>( active );
            summary._phase             = static_cast<uint8>( active >= 0 ? computePhase( npc, active, minuteOfDay, fraction ) : ScheduleNpcPhase::Performing );
            summary._interruptionDepth = static_cast<uint8>( MathUtil::min( npc._listInterruption.size(), static_cast<size_t>( 255 ) ) );
            if ( active >= 0 )
                summary._activityHash = ScheduleHash::computeNameKey( npc._listSegment[static_cast<size_t>( active )]._activity );
            outListSummary.push_back( summary );
        }
    }

    void ScheduleSystem::encodeNetSummary( int32 minute, const vector<ScheduleNetSummary>& listSummary, vector<uint8>& outBytes )
    {
        using Internal = ScheduleSystemSaveInternal;
        outBytes.clear();
        outBytes.reserve( Internal::kNetHeaderSize + listSummary.size() * Internal::kNetEntrySize );
        Internal::writeValue( outBytes, minute );
        Internal::writeValue( outBytes, static_cast<uint16>( listSummary.size() ) );
        for ( const ScheduleNetSummary& summary : listSummary )
        {
            Internal::writeValue( outBytes, summary._position._x );
            Internal::writeValue( outBytes, summary._position._y );
            Internal::writeValue( outBytes, summary._position._z );
            Internal::writeValue( outBytes, summary._areaHash );
            Internal::writeValue( outBytes, summary._activityHash );
            Internal::writeValue( outBytes, summary._npcIndex );
            Internal::writeValue( outBytes, summary._segmentIndex );
            Internal::writeValue( outBytes, summary._phase );
            Internal::writeValue( outBytes, summary._interruptionDepth );
        }
    }

    bool ScheduleSystem::decodeNetSummary( const vector<uint8>& bytes, int32& outMinute, vector<ScheduleNetSummary>& outListSummary )
    {
        using Internal = ScheduleSystemSaveInternal;
        outListSummary.clear();
        if ( bytes.size() < Internal::kNetHeaderSize )
            return false;
        size_t       offset = 0;
        const int32  minute = Internal::readValue<int32>( bytes, offset );
        const uint16 count  = Internal::readValue<uint16>( bytes, offset );
        if ( bytes.size() != Internal::kNetHeaderSize + static_cast<size_t>( count ) * Internal::kNetEntrySize )
            return false;
        for ( uint16 entryIndex = 0; entryIndex < count; ++entryIndex )
        {
            ScheduleNetSummary summary;
            summary._position._x       = Internal::readValue<float32>( bytes, offset );
            summary._position._y       = Internal::readValue<float32>( bytes, offset );
            summary._position._z       = Internal::readValue<float32>( bytes, offset );
            summary._areaHash          = Internal::readValue<uint32>( bytes, offset );
            summary._activityHash      = Internal::readValue<uint32>( bytes, offset );
            summary._npcIndex          = Internal::readValue<uint16>( bytes, offset );
            summary._segmentIndex      = Internal::readValue<int16>( bytes, offset );
            summary._phase             = Internal::readValue<uint8>( bytes, offset );
            summary._interruptionDepth = Internal::readValue<uint8>( bytes, offset );
            outListSummary.push_back( summary );
        }
        outMinute = minute;
        return true;
    }

    uint64 ScheduleSystem::computeStateHash() const
    {
        using Internal = ScheduleSystemSaveInternal;
        vector<ScheduleNetSummary> listSummary;
        fillNetSummary( listSummary );
        vector<uint8> bytes;
        encodeNetSummary( _minute, listSummary, bytes );
        uint64 hash = HashUtil::appendBytes64( HashUtil::kFnvOffset64, bytes.data(), bytes.size() );
        for ( const NpcRuntime& npc : _listNpc )
        {
            hash = HashUtil::appendValue64( hash, npc._originMinute );
            hash = ScheduleHash::appendPosition( hash, npc._origin._position );
            for ( const ScheduleSegment& segment : npc._listSegment )
            {
                hash = HashUtil::appendValue64( hash, ScheduleHash::computeSegmentKey( segment ) );
            }
            for ( const Interruption& interruption : npc._listInterruption )
            {
                hash = ScheduleHash::appendName( hash, interruption._id );
                hash = HashUtil::appendValue64( hash, interruption._expireMinute );
            }
        }
        for ( const int32 appointmentIndex : _listBrokenAppointment )
        {
            hash = HashUtil::appendValue64( hash, appointmentIndex );
        }
        return hash;
    }
} // namespace sw
