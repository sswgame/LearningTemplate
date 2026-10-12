#include "pch.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleActivity.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

namespace sw
{
    SW_LOG_CALLER( "ScheduleSystem" );

    namespace
    {
        struct ScheduleSystemInternal
        {
            static int32 toDay( int32 absoluteMinute ) { return absoluteMinute / kScheduleMinutesPerDay; }

            static uint64 hashBytes( uint64 hash, const void* pData, size_t size )
            {
                const uint8* pByte = static_cast<const uint8*>( pData );
                for ( size_t byteIndex = 0; byteIndex < size; ++byteIndex )
                {
                    hash ^= pByte[byteIndex];
                    hash *= HashUtil::kFnvPrime64;
                }
                return hash;
            }

            template <typename T>
            static uint64 hashValue( uint64 hash, const T& value )
            {
                return hashBytes( hash, &value, sizeof( T ) );
            }

            static uint64 hashName( uint64 hash, const hashed_string& name )
            {
                return hashBytes( hash, name.c_str(), name.size() );
            }

            static uint64 hashPosition( uint64 hash, const float3& position )
            {
                hash = hashValue( hash, position._x );
                hash = hashValue( hash, position._y );
                return hashValue( hash, position._z );
            }

            /** @brief 칸의 정체 — 같은 출처 · 칸 · 시간 · 자리 · 출발이면 같은 값입니다(사건 · 경로 캐시 · 계획 비교). */
            static uint64 computeSegmentKey( const ScheduleSegment& segment )
            {
                uint64 hash = HashUtil::kFnvOffset64;
                hash        = hashName( hash, segment._sourceID );
                hash        = hashName( hash, segment._activity );
                hash        = hashValue( hash, segment._startMinute );
                hash        = hashValue( hash, segment._endMinute );
                hash        = hashValue( hash, segment._blockIndex );
                hash        = hashValue( hash, segment._ownerIndex );
                hash        = hashValue( hash, segment._source );
                hash        = hashPosition( hash, segment._target._position );
                hash        = hashValue( hash, segment._departMinute );
                hash        = hashValue( hash, segment._travelMinutes );
                return hash == 0 ? 1 : hash;
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
    const utf8* toString( ScheduleLOD lod )
    {
        switch ( lod )
        {
            case ScheduleLOD::Near:
                return "Near";
            case ScheduleLOD::Far:
                return "Far";
        }
        return "Unknown";
    }

    const utf8* toString( ScheduleNpcPhase phase )
    {
        switch ( phase )
        {
            case ScheduleNpcPhase::Traveling:
                return "Traveling";
            case ScheduleNpcPhase::Performing:
                return "Performing";
            case ScheduleNpcPhase::Interrupted:
                return "Interrupted";
        }
        return "Unknown";
    }

    const utf8* toString( ScheduleSegmentSource source )
    {
        switch ( source )
        {
            case ScheduleSegmentSource::Idle:
                return "idle";
            case ScheduleSegmentSource::Routine:
                return "routine";
            case ScheduleSegmentSource::Event:
                return "event";
            case ScheduleSegmentSource::Appointment:
                return "appointment";
        }
        return "unknown";
    }

    const utf8* toString( ScheduleEvent::Kind kind )
    {
        switch ( kind )
        {
            case ScheduleEvent::Kind::Departed:
                return "Departed";
            case ScheduleEvent::Kind::Arrived:
                return "Arrived";
            case ScheduleEvent::Kind::ActivityStarted:
                return "ActivityStarted";
            case ScheduleEvent::Kind::ActivityEnded:
                return "ActivityEnded";
            case ScheduleEvent::Kind::Interrupted:
                return "Interrupted";
            case ScheduleEvent::Kind::Resumed:
                return "Resumed";
            case ScheduleEvent::Kind::AppointmentMet:
                return "AppointmentMet";
            case ScheduleEvent::Kind::AppointmentBroken:
                return "AppointmentBroken";
            case ScheduleEvent::Kind::Snapped:
                return "Snapped";
        }
        return "Unknown";
    }

    ScheduleSystem::ScheduleSystem()
        : _pCatalog{ nullptr }
        , _pPlanning{ nullptr }
        , _pFine{ nullptr }
        , _pLocator{ nullptr }
        , _pAnimator{ nullptr }
        , _pFlags{ nullptr }
        , _pWorldTags{ nullptr }
        , _defaultLocator{}
        , _defaultPathing{}
        , _settings{}
        , _listNpc{}
        , _eventBuffer{}
        , _listAppointmentCheck{}
        , _listBrokenAppointment{}
        , _listMetAppointment{}
        , _weather{}
        , _lastTraceRequest{}
        , _minuteFraction{ 0.0f }
        , _minute{ 0 }
        , _flagsRevision{ 0 }
        , _bConditionsDirty{ SW_FALSE }
        , _bSuppressEvents{ SW_FALSE }
    {
    }

    ScheduleSystem::~ScheduleSystem() { shutdown(); }

    void ScheduleSystem::initialize( const ScheduleCatalog* pCatalog, const ScheduleSystemSettings& settings, int32 absoluteMinute )
    {
        shutdown();
        _pCatalog                       = pCatalog;
        _settings                       = settings;
        _settings._clock._daysPerSeason = MathUtil::max( 1, _settings._clock._daysPerSeason );
        _settings._farStepMinutes       = MathUtil::max( 1, _settings._farStepMinutes );
        if ( _settings._clock._listSeason.empty() )
            _settings._clock._listSeason.push_back( hashed_string( "Default" ) );
        _minute = MathUtil::max( 0, absoluteMinute );
        _defaultLocator.initialize( pCatalog );
        if ( _pLocator == nullptr )
            _pLocator = &_defaultLocator;
        if ( _pPlanning == nullptr )
            _pPlanning = &_defaultPathing;
        if ( _pFine == nullptr )
            _pFine = _pPlanning;
        _flagsRevision = _pFlags != nullptr ? _pFlags->getRevision() : 0;
        if ( _pCatalog == nullptr )
            return;

        const vector<ScheduleNpcDef>& listDef = _pCatalog->getNpcs();
        for ( int32 defIndex = 0; defIndex < static_cast<int32>( listDef.size() ); ++defIndex )
        {
            const ScheduleNpcDef& def = listDef[static_cast<size_t>( defIndex )];
            NpcRuntime            npc;
            npc._defIndex = defIndex;
            for ( const TagID& tag : def._listTag )
            {
                npc._tags.addTag( tag );
            }
            const SchedulePlaceDef* pHome = _pCatalog->findPlace( def._home );
            if ( pHome != nullptr )
                npc._origin = ScheduleLocation{ pHome->_position, pHome->_area };
            npc._held = npc._origin;
            _listNpc.push_back( npc );
        }
        _bSuppressEvents = SW_TRUE; // 처음 상태는 사건이 아니다
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            planNpc( npcIndex, _minute, _listNpc[static_cast<size_t>( npcIndex )]._origin );
        }
        _bSuppressEvents = SW_FALSE;
        rebuildAppointmentChecks( getDay() );
    }

    void ScheduleSystem::shutdown()
    {
        for ( NpcRuntime& npc : _listNpc )
        {
            for ( const ScheduleSegment& segment : npc._listSegment )
            {
                if ( segment._reservationID != 0 && _pLocator != nullptr )
                    _pLocator->release( segment._reservationID );
            }
        }
        _listNpc.clear();
        _eventBuffer.clear();
        _listAppointmentCheck.clear();
        _listBrokenAppointment.clear();
        _listMetAppointment.clear();
        _defaultLocator.clear();
        _pCatalog         = nullptr;
        _minute           = 0;
        _minuteFraction   = 0.0f;
        _bConditionsDirty = SW_FALSE;
    }

    void ScheduleSystem::setPathing( const ISchedulePathing* pPlanning, const ISchedulePathing* pFine )
    {
        _pPlanning = pPlanning != nullptr ? pPlanning : &_defaultPathing;
        _pFine     = pFine != nullptr ? pFine : _pPlanning;
        if ( _listNpc.empty() == false )
            _bConditionsDirty = SW_TRUE;
    }

    void ScheduleSystem::setActivityLocator( IScheduleActivityLocator* pLocator )
    {
        _pLocator = pLocator != nullptr ? pLocator : &_defaultLocator;
        if ( _listNpc.empty() == false )
            _bConditionsDirty = SW_TRUE;
    }

    void ScheduleSystem::setFlags( const GameFlags* pFlags )
    {
        _pFlags           = pFlags;
        _flagsRevision    = pFlags != nullptr ? pFlags->getRevision() : 0;
        _bConditionsDirty = SW_TRUE;
    }

    void ScheduleSystem::setWorldTags( const TagContainer* pTags )
    {
        _pWorldTags       = pTags;
        _bConditionsDirty = SW_TRUE;
    }

    void ScheduleSystem::setWeather( const hashed_string& weatherID )
    {
        if ( _weather.isEqual( weatherID, NameCase::CaseSensitive ) )
            return;
        _weather          = weatherID;
        _bConditionsDirty = SW_TRUE;
    }

    void ScheduleSystem::update( const WorldClock& clock )
    {
        const float32 minuteOfDay = MathUtil::clamp( clock.getHour() * 60.0f, 0.0f, static_cast<float32>( kScheduleMinutesPerDay - 1 ) + 0.999f );
        const int32   wholeMinute = static_cast<int32>( minuteOfDay );
        advanceTo( clock.getDay() * kScheduleMinutesPerDay + wholeMinute );
        _minuteFraction = minuteOfDay - static_cast<float32>( wholeMinute );
        logTraceRequest();
    }

    void ScheduleSystem::advanceTo( int32 absoluteMinute )
    {
        using Internal = ScheduleSystemInternal;
        if ( _pCatalog == nullptr )
            return;
        applyConditionChanges();
        // (지금, 목표] 의 사건을 시각 순으로 — 같은 시각이면 날 시작 → 끼어들기 만료 → 약속 판정 → NPC 순.
        for ( ;; )
        {
            int32 next = ( Internal::toDay( _minute ) + 1 ) * kScheduleMinutesPerDay;
            for ( const AppointmentCheck& check : _listAppointmentCheck )
            {
                if ( check._bDone == SW_FALSE )
                    next = MathUtil::min( next, check._minute );
            }
            for ( const NpcRuntime& npc : _listNpc )
            {
                next = MathUtil::min( next, npc._wakeMinute );
            }
            if ( next > absoluteMinute )
                break;
            _minute = MathUtil::max( _minute, next );
            processMinute( _minute );
        }
        _minute         = MathUtil::max( _minute, absoluteMinute );
        _minuteFraction = 0.0f;
        updateNearRoutes();
    }

    void ScheduleSystem::skipTo( int32 absoluteMinute )
    {
        if ( _pCatalog == nullptr || absoluteMinute <= _minute )
            return;
        applyConditionChanges();
        _bSuppressEvents = SW_TRUE;
        // 잠 — 끼어들기는 모두 끝난다. 끼어든 자리에서 지금 칸으로 돌아간 뒤 건너뛴다.
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
            if ( npc._listInterruption.empty() )
                continue;
            npc._listInterruption.clear();
            resumeNpc( npcIndex );
        }
        advanceTo( absoluteMinute );
        _bSuppressEvents = SW_FALSE;
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            emitSnapped( npcIndex );
        }
    }

    void ScheduleSystem::setNpcLOD( int32 npcIndex, ScheduleLOD lod )
    {
        if ( isValidNpc( npcIndex ) == false )
            return;
        NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        if ( npc._lod == lod )
            return;
        npc._lod = lod;
        // 화면 안으로 — 굵은 간격으로 미뤄 둔 사건을 지금 맞추고, 그 시각 자리로 옮긴다(계획은 LOD 와 상관없어 그대로다).
        refreshNpc( npcIndex );
        if ( lod == ScheduleLOD::Near )
        {
            npc._routeKey = 0;
            updateNearRoutes();
            emitSnapped( npcIndex );
        }
    }

    ScheduleLOD ScheduleSystem::getNpcLOD( int32 npcIndex ) const
    {
        return isValidNpc( npcIndex ) ? _listNpc[static_cast<size_t>( npcIndex )]._lod : ScheduleLOD::Far;
    }

    void ScheduleSystem::addNpcTag( int32 npcIndex, const TagID& tag )
    {
        const bool bHasTag = isValidNpc( npcIndex ) && _listNpc[static_cast<size_t>( npcIndex )]._tags.hasTag( tag, true );
        if ( isValidNpc( npcIndex ) == false || bHasTag )
            return;
        _listNpc[static_cast<size_t>( npcIndex )]._tags.addTag( tag );
        _listNpc[static_cast<size_t>( npcIndex )]._bTagsDirty = SW_TRUE;
    }

    void ScheduleSystem::removeNpcTag( int32 npcIndex, const TagID& tag )
    {
        if ( isValidNpc( npcIndex ) == false || _listNpc[static_cast<size_t>( npcIndex )]._tags.hasTag( tag, true ) == false )
            return; // 없는 NPC · 없는 태그
        _listNpc[static_cast<size_t>( npcIndex )]._tags.removeTag( tag );
        _listNpc[static_cast<size_t>( npcIndex )]._bTagsDirty = SW_TRUE;
    }

    bool ScheduleSystem::pushInterruption( int32 npcIndex, const hashed_string& interruptID )
    {
        if ( _pCatalog == nullptr || isValidNpc( npcIndex ) == false )
            return false;
        const ScheduleInterruptDef* pDef = _pCatalog->findInterrupt( interruptID );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING( "unknown interruption '%#' for npc '%#' - ignored", interruptID.c_str(), getNpcID( npcIndex ).c_str() );
            return false;
        }
        NpcRuntime& npc          = _listNpc[static_cast<size_t>( npcIndex )];
        const int32 expireMinute = pDef->_timeoutMinutes > 0 ? _minute + pDef->_timeoutMinutes : -1;
        for ( Interruption& interruption : npc._listInterruption )
        {
            if ( interruption._id == interruptID )
            {
                interruption._expireMinute = expireMinute;
                npc._wakeMinute            = computeWakeMinute( npcIndex );
                return true;
            }
        }
        if ( npc._listInterruption.empty() )
        {
            refreshNpc( npcIndex ); // 끼어들기 앞까지의 사건을 맞춘다
            npc._held = computeLogicLocation( npcIndex, _minute );
            if ( npc._bEmitted != SW_FALSE && npc._emittedPhase == ScheduleNpcPhase::Performing )
                emitEvent( ScheduleEvent::Kind::ActivityEnded, npcIndex, &npc._emittedSegment, npc._emittedSegment._sourceID );
            npc._bEmitted = SW_FALSE;
        }
        Interruption interruption;
        interruption._id           = interruptID;
        interruption._priority     = pDef->_priority;
        interruption._startMinute  = _minute;
        interruption._expireMinute = expireMinute;
        size_t insertIndex         = npc._listInterruption.size();
        while ( insertIndex > 0 && npc._listInterruption[insertIndex - 1]._priority > interruption._priority )
        {
            --insertIndex;
        }
        npc._listInterruption.insert( npc._listInterruption.begin() + static_cast<ptrdiff_t>( insertIndex ), interruption );
        emitEvent( ScheduleEvent::Kind::Interrupted, npcIndex, nullptr, interruptID );
        npc._wakeMinute = computeWakeMinute( npcIndex );
        return true;
    }

    bool ScheduleSystem::popInterruption( int32 npcIndex, const hashed_string& interruptID )
    {
        if ( isValidNpc( npcIndex ) == false )
            return false;
        NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        for ( size_t interruptionIndex = 0; interruptionIndex < npc._listInterruption.size(); ++interruptionIndex )
        {
            if ( npc._listInterruption[interruptionIndex]._id != interruptID )
                continue;
            npc._listInterruption.erase( npc._listInterruption.begin() + static_cast<ptrdiff_t>( interruptionIndex ) );
            if ( npc._listInterruption.empty() )
                resumeNpc( npcIndex );
            else
                npc._wakeMinute = computeWakeMinute( npcIndex );
            return true;
        }
        return false;
    }

    void ScheduleSystem::reportInterruptedLocation( int32 npcIndex, const ScheduleLocation& location )
    {
        if ( isValidNpc( npcIndex ) == false || _listNpc[static_cast<size_t>( npcIndex )]._listInterruption.empty() )
            return;
        _listNpc[static_cast<size_t>( npcIndex )]._held = location;
    }

    int32 ScheduleSystem::findNpcIndex( const hashed_string& npcID ) const
    {
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            if ( getNpcID( npcIndex ) == npcID )
                return npcIndex;
        }
        return -1;
    }

    hashed_string ScheduleSystem::getNpcID( int32 npcIndex ) const
    {
        if ( _pCatalog == nullptr || isValidNpc( npcIndex ) == false )
            return hashed_string{};
        return _pCatalog->getNpcs()[static_cast<size_t>( _listNpc[static_cast<size_t>( npcIndex )]._defIndex )]._id;
    }

    ScheduleNpcView ScheduleSystem::getNpcView( int32 npcIndex ) const
    {
        ScheduleNpcView view;
        if ( isValidNpc( npcIndex ) == false )
            return view;
        const NpcRuntime& npc         = _listNpc[static_cast<size_t>( npcIndex )];
        const float32     minuteOfDay = static_cast<float32>( _minute - npc._planDay * kScheduleMinutesPerDay ) + _minuteFraction;
        view._lod                     = npc._lod;
        view._segmentIndex            = ScheduleSystemInternal::findActiveSegment( npc._listSegment, minuteOfDay );
        if ( npc._listInterruption.empty() == false )
            view._interruption = npc._listInterruption.back()._id;
        if ( view._segmentIndex < 0 )
        {
            view._location = npc._origin;
            view._target   = npc._origin;
            return view;
        }
        const ScheduleSegment& segment = npc._listSegment[static_cast<size_t>( view._segmentIndex )];
        view._target                   = segment._target;
        view._activity                 = segment._activity;
        view._animation                = segment._animation;
        view._phase                    = computePhase( npc, view._segmentIndex, minuteOfDay, view._travelFraction );
        switch ( view._phase )
        {
            case ScheduleNpcPhase::Interrupted:
            {
                view._location = npc._held;
                break;
            }
            case ScheduleNpcPhase::Performing:
            {
                view._location = segment._target;
                break;
            }
            case ScheduleNpcPhase::Traveling:
            {
                view._location               = computePlanningLocationOnRoute( npc, view._segmentIndex, view._travelFraction );
                const bool bNearRouteCurrent = npc._lod == ScheduleLOD::Near && npc._routeKey == ScheduleSystemInternal::computeSegmentKey( segment );
                if ( bNearRouteCurrent )
                    view._location._position = SchedulePathingUtil::computePointAlong( npc._listRoutePoint, view._travelFraction );
                break;
            }
        }
        return view;
    }

    const vector<ScheduleSegment>& ScheduleSystem::getPlan( int32 npcIndex ) const
    {
        static const vector<ScheduleSegment> s_empty;
        return isValidNpc( npcIndex ) ? _listNpc[static_cast<size_t>( npcIndex )]._listSegment : s_empty;
    }

    bool ScheduleSystem::isAppointmentMet( const hashed_string& appointmentID ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const int32 appointmentIndex = _pCatalog->findAppointmentIndex( appointmentID );
        if ( appointmentIndex < 0 || isAppointmentBroken( appointmentID ) )
            return false;
        const ScheduleAppointmentDef& appointment = _pCatalog->getAppointments()[static_cast<size_t>( appointmentIndex )];
        if ( appointment._listAttendee.empty() )
            return false;
        for ( const hashed_string& attendee : appointment._listAttendee )
        {
            if ( isAttendeePresent( findNpcIndex( attendee ), appointmentIndex ) == false )
                return false;
        }
        return true;
    }

    bool ScheduleSystem::isAppointmentBroken( const hashed_string& appointmentID ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const int32 appointmentIndex = _pCatalog->findAppointmentIndex( appointmentID );
        for ( const int32 brokenIndex : _listBrokenAppointment )
        {
            if ( brokenIndex == appointmentIndex )
                return true;
        }
        return false;
    }

    int32 ScheduleSystem::getDay() const { return ScheduleSystemInternal::toDay( _minute ); }

    void ScheduleSystem::drainEvents( vector<ScheduleEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }
} // namespace sw
