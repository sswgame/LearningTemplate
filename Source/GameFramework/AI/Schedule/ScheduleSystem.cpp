#include "pch.h"

#include "GameFramework/AI/Schedule/ScheduleSystem.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/AI/Schedule/ScheduleActivity.h"
#include "GameFramework/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Utility/GameRandom.h"
#include "GameFramework/World/GameFlags.h"

namespace sw
{
    SW_LOG_CALLER( "ScheduleSystem" );

    namespace
    {
        struct ScheduleSystemInternal
        {
            static constexpr int32   kNoWake            = 0x7fffffff;
            static constexpr float32 kSamePlaceDistance = 1.0e-3f;
            static constexpr uint64  kFnvOffset         = 0xcbf29ce484222325ull;
            static constexpr uint64  kFnvPrime          = 0x100000001b3ull;
            static constexpr size_t  kNetHeaderSize     = 6;  ///< 분 int32 + 수 uint16
            static constexpr size_t  kNetEntrySize      = 26; ///< 자리 3 × float32 + 지역 · 활동 해시 2 × uint32 + NPC uint16 + 칸 int16 + 상태 2 × uint8

            static int32 toDay( int32 absoluteMinute ) { return absoluteMinute / kScheduleMinutesPerDay; }
            static int32 toMinuteOfDay( int32 absoluteMinute ) { return absoluteMinute % kScheduleMinutesPerDay; }

            static uint64 hashBytes( uint64 hash, const void* pData, size_t size )
            {
                const uint8* pByte = static_cast<const uint8*>( pData );
                for ( size_t byteIndex = 0; byteIndex < size; ++byteIndex )
                {
                    hash ^= pByte[byteIndex];
                    hash *= kFnvPrime;
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

            static uint32 hashNameToKey( const hashed_string& name ) { return static_cast<uint32>( hashName( kFnvOffset, name ) ); }

            static uint32 mixKey( uint32 lhs, uint32 rhs ) { return GameHash::mix32( lhs * 0x9e3779b1u ^ GameHash::mix32( rhs + 0x7f4a7c15u ) ); }

            /** @brief 칸의 정체 — 같은 출처 · 칸 · 시간 · 자리 · 출발이면 같은 값입니다(사건 · 경로 캐시 · 계획 비교). */
            static uint64 computeSegmentKey( const ScheduleSegment& segment )
            {
                uint64 hash = kFnvOffset;
                hash        = hashName( hash, segment._sourceId );
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

            static int32 roundUpToStep( int32 minute, int32 step )
            {
                if ( step <= 1 )
                    return minute;
                return ( ( minute + step - 1 ) / step ) * step;
            }

            template <uint32 Capacity>
            static void appendClock( StringBuilder<Capacity>& inoutText, int32 minuteOfDay )
            {
                const int32 hours   = minuteOfDay / 60;
                const int32 minutes = minuteOfDay % 60;
                inoutText.appendFormat( "%#%#:%#%#", hours < 10 ? "0" : "", hours, minutes < 10 ? "0" : "", minutes );
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

    /** @brief `-gv_scheduleTrace=<npc id | *>` — 값이 바뀌면 그 NPC 의 "왜 여기 있나" 와 오늘 시간표를 로그에 한 번 남깁니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_scheduleTrace, "", "Schedule: log the why-am-I-here trace and today's timeline of this NPC id (* = all) when the value changes" );
} // namespace sw

namespace sw
{
    const utf8* toString( ScheduleLod lod )
    {
        switch ( lod )
        {
            case ScheduleLod::Near:
                return "Near";
            case ScheduleLod::Far:
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
                npc._tags.addTag( tag );
            const SchedulePlaceDef* pHome = _pCatalog->findPlace( def._home );
            if ( pHome != nullptr )
                npc._origin = ScheduleLocation{ pHome->_position, pHome->_area };
            npc._held = npc._origin;
            _listNpc.push_back( npc );
        }
        _bSuppressEvents = SW_TRUE; // 처음 상태는 사건이 아니다
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
            planNpc( npcIndex, _minute, _listNpc[static_cast<size_t>( npcIndex )]._origin );
        _bSuppressEvents = SW_FALSE;
        rebuildAppointmentChecks( getDay() );
    }

    void ScheduleSystem::shutdown()
    {
        for ( NpcRuntime& npc : _listNpc )
        {
            for ( const ScheduleSegment& segment : npc._listSegment )
            {
                if ( segment._reservationId != 0 && _pLocator != nullptr )
                    _pLocator->release( segment._reservationId );
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

    void ScheduleSystem::setWeather( const hashed_string& weatherId )
    {
        if ( _weather.isEqual( weatherId, NameCase::CaseSensitive ) )
            return;
        _weather          = weatherId;
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
                next = MathUtil::min( next, npc._wakeMinute );
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
            emitSnapped( npcIndex );
    }

    void ScheduleSystem::setNpcLod( int32 npcIndex, ScheduleLod lod )
    {
        if ( isValidNpc( npcIndex ) == false )
            return;
        NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        if ( npc._lod == lod )
            return;
        npc._lod = lod;
        // 화면 안으로 — 굵은 간격으로 미뤄 둔 사건을 지금 맞추고, 그 시각 자리로 옮긴다(계획은 LOD 와 상관없어 그대로다).
        refreshNpc( npcIndex );
        if ( lod == ScheduleLod::Near )
        {
            npc._routeKey = 0;
            updateNearRoutes();
            emitSnapped( npcIndex );
        }
    }

    ScheduleLod ScheduleSystem::getNpcLod( int32 npcIndex ) const
    {
        return isValidNpc( npcIndex ) ? _listNpc[static_cast<size_t>( npcIndex )]._lod : ScheduleLod::Far;
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

    bool ScheduleSystem::pushInterruption( int32 npcIndex, const hashed_string& interruptId )
    {
        if ( _pCatalog == nullptr || isValidNpc( npcIndex ) == false )
            return false;
        const ScheduleInterruptDef* pDef = _pCatalog->findInterrupt( interruptId );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING( "unknown interruption '%#' for npc '%#' - ignored", interruptId.c_str(), getNpcId( npcIndex ).c_str() );
            return false;
        }
        NpcRuntime& npc          = _listNpc[static_cast<size_t>( npcIndex )];
        const int32 expireMinute = pDef->_timeoutMinutes > 0 ? _minute + pDef->_timeoutMinutes : -1;
        for ( Interruption& interruption : npc._listInterruption )
        {
            if ( interruption._id == interruptId )
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
                emitEvent( ScheduleEvent::Kind::ActivityEnded, npcIndex, &npc._emittedSegment, npc._emittedSegment._sourceId );
            npc._bEmitted = SW_FALSE;
        }
        Interruption interruption;
        interruption._id           = interruptId;
        interruption._priority     = pDef->_priority;
        interruption._startMinute  = _minute;
        interruption._expireMinute = expireMinute;
        size_t insertIndex         = npc._listInterruption.size();
        while ( insertIndex > 0 && npc._listInterruption[insertIndex - 1]._priority > interruption._priority )
            --insertIndex;
        npc._listInterruption.insert( npc._listInterruption.begin() + static_cast<ptrdiff_t>( insertIndex ), interruption );
        emitEvent( ScheduleEvent::Kind::Interrupted, npcIndex, nullptr, interruptId );
        npc._wakeMinute = computeWakeMinute( npcIndex );
        return true;
    }

    bool ScheduleSystem::popInterruption( int32 npcIndex, const hashed_string& interruptId )
    {
        if ( isValidNpc( npcIndex ) == false )
            return false;
        NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        for ( size_t interruptionIndex = 0; interruptionIndex < npc._listInterruption.size(); ++interruptionIndex )
        {
            if ( npc._listInterruption[interruptionIndex]._id != interruptId )
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

    int32 ScheduleSystem::findNpcIndex( const hashed_string& npcId ) const
    {
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            if ( getNpcId( npcIndex ) == npcId )
                return npcIndex;
        }
        return -1;
    }

    hashed_string ScheduleSystem::getNpcId( int32 npcIndex ) const
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
                const bool bNearRouteCurrent = npc._lod == ScheduleLod::Near && npc._routeKey == ScheduleSystemInternal::computeSegmentKey( segment );
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

    bool ScheduleSystem::isAppointmentMet( const hashed_string& appointmentId ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const int32 appointmentIndex = _pCatalog->findAppointmentIndex( appointmentId );
        if ( appointmentIndex < 0 || isAppointmentBroken( appointmentId ) )
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

    bool ScheduleSystem::isAppointmentBroken( const hashed_string& appointmentId ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const int32 appointmentIndex = _pCatalog->findAppointmentIndex( appointmentId );
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
            npcState._id             = getNpcId( npcIndex );
            npcState._originArea     = npc._origin._area;
            npcState._originPosition = npc._origin._position;
            npcState._originMinute   = npc._originMinute;
            npcState._heldArea       = npc._held._area;
            npcState._heldPosition   = npc._held._position;
            for ( const Interruption& interruption : npc._listInterruption )
            {
                ScheduleInterruptionSaveState interruptionState;
                interruptionState._id           = interruption._id;
                interruptionState._startMinute  = interruption._startMinute;
                interruptionState._expireMinute = interruption._expireMinute;
                npcState._listInterruption.push_back( interruptionState );
            }
            for ( const TagID& tag : npc._tags.getTags() )
                npcState._listTag.push_back( tag.getString() != nullptr ? string( tag.getString() ) : string() );
            outState._listNpc.push_back( npcState );
        }
        for ( const int32 appointmentIndex : _listBrokenAppointment )
            outState._listBrokenAppointment.push_back( string( _pCatalog->getAppointments()[static_cast<size_t>( appointmentIndex )]._id.c_str() ) );
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
        for ( const string& appointmentId : state._listBrokenAppointment )
        {
            const int32 appointmentIndex = _pCatalog->findAppointmentIndex( hashed_string( appointmentId ) );
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
            npc._tags.clear();
            for ( const string& tagName : npcState._listTag )
                npc._tags.addTag( TagID::request( tagName ) );
            for ( const ScheduleInterruptionSaveState& interruptionState : npcState._listInterruption )
            {
                const ScheduleInterruptDef* pDef = _pCatalog->findInterrupt( interruptionState._id );
                if ( pDef == nullptr )
                    continue;
                npc._listInterruption.push_back( Interruption{ interruptionState._id, pDef->_priority, interruptionState._startMinute, interruptionState._expireMinute } );
            }
            npc._held                     = ScheduleLocation{ npcState._heldPosition, npcState._heldArea };
            const ScheduleLocation origin = ScheduleLocation{ npcState._originPosition, npcState._originArea };
            const bool             bToday = ScheduleSystemInternal::toDay( npcState._originMinute ) == getDay() && npcState._originMinute <= _minute;
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
            const int32            active      = ScheduleSystemInternal::findActiveSegment( npc._listSegment, minuteOfDay );
            float32                fraction    = 1.0f;
            const ScheduleLocation location    = computeLogicLocation( npcIndex, _minute );
            ScheduleNetSummary     summary;
            summary._position          = location._position;
            summary._areaHash          = ScheduleSystemInternal::hashNameToKey( location._area );
            summary._npcIndex          = static_cast<uint16>( npcIndex );
            summary._segmentIndex      = static_cast<int16>( active );
            summary._phase             = static_cast<uint8>( active >= 0 ? computePhase( npc, active, minuteOfDay, fraction ) : ScheduleNpcPhase::Performing );
            summary._interruptionDepth = static_cast<uint8>( MathUtil::min( npc._listInterruption.size(), static_cast<size_t>( 255 ) ) );
            if ( active >= 0 )
                summary._activityHash = ScheduleSystemInternal::hashNameToKey( npc._listSegment[static_cast<size_t>( active )]._activity );
            outListSummary.push_back( summary );
        }
    }

    void ScheduleSystem::encodeNetSummary( int32 minute, const vector<ScheduleNetSummary>& listSummary, vector<uint8>& outBytes )
    {
        using Internal = ScheduleSystemInternal;
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
        using Internal = ScheduleSystemInternal;
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
        using Internal = ScheduleSystemInternal;
        vector<ScheduleNetSummary> listSummary;
        fillNetSummary( listSummary );
        vector<uint8> bytes;
        encodeNetSummary( _minute, listSummary, bytes );
        uint64 hash = Internal::hashBytes( Internal::kFnvOffset, bytes.data(), bytes.size() );
        for ( const NpcRuntime& npc : _listNpc )
        {
            hash = Internal::hashValue( hash, npc._originMinute );
            hash = Internal::hashPosition( hash, npc._origin._position );
            for ( const ScheduleSegment& segment : npc._listSegment )
                hash = Internal::hashValue( hash, Internal::computeSegmentKey( segment ) );
            for ( const Interruption& interruption : npc._listInterruption )
            {
                hash = Internal::hashName( hash, interruption._id );
                hash = Internal::hashValue( hash, interruption._expireMinute );
            }
        }
        for ( const int32 appointmentIndex : _listBrokenAppointment )
            hash = Internal::hashValue( hash, appointmentIndex );
        return hash;
    }
} // namespace sw

namespace sw
{
    void ScheduleSystem::planNpc( int32 npcIndex, int32 originMinute, const ScheduleLocation& origin )
    {
        NpcRuntime& npc   = _listNpc[static_cast<size_t>( npcIndex )];
        npc._origin       = origin;
        npc._originMinute = originMinute;
        npc._planDay      = ScheduleSystemInternal::toDay( originMinute );
        vector<ScheduleSegment> listSegment;
        buildPlan( npcIndex, npc._planDay, ScheduleSystemInternal::toMinuteOfDay( originMinute ), origin, listSegment );
        releaseUnused( npc._listSegment, listSegment );
        npc._listSegment = std::move( listSegment );
        npc._routeKey    = 0;
        npc._listRoutePoint.clear();
        refreshNpc( npcIndex );
    }

    void ScheduleSystem::replanNpc( int32 npcIndex, int32 minute, bool bKeepOriginIfSame )
    {
        using Internal  = ScheduleSystemInternal;
        NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        if ( bKeepOriginIfSame && npc._planDay == Internal::toDay( minute ) )
        {
            // 같은 출발점으로 다시 세워 지금 칸까지 같으면 그대로 쓴다 — 바뀐 조건이 앞날만 바꾸면 지금 가는 길을 끊지 않는다.
            vector<ScheduleSegment> listTrial;
            buildPlan( npcIndex, npc._planDay, Internal::toMinuteOfDay( npc._originMinute ), npc._origin, listTrial );
            const float32 minuteOfDay = static_cast<float32>( Internal::toMinuteOfDay( minute ) );
            const int32   oldActive   = Internal::findActiveSegment( npc._listSegment, minuteOfDay );
            const int32   newActive   = Internal::findActiveSegment( listTrial, minuteOfDay );
            bool          bSame       = oldActive == newActive && oldActive >= 0;
            for ( int32 segmentIndex = 0; bSame && segmentIndex <= oldActive; ++segmentIndex )
            {
                const uint64 oldKey = Internal::computeSegmentKey( npc._listSegment[static_cast<size_t>( segmentIndex )] );
                bSame               = oldKey == Internal::computeSegmentKey( listTrial[static_cast<size_t>( segmentIndex )] );
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

    void ScheduleSystem::buildPlan( int32 npcIndex, int32 day, int32 originMinuteOfDay, const ScheduleLocation& origin, vector<ScheduleSegment>& outListSegment )
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
        std::sort( listBoundary.begin(), listBoundary.end() );
        listBoundary.erase( std::unique( listBoundary.begin(), listBoundary.end() ), listBoundary.end() );

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
        using Internal = ScheduleSystemInternal;
        outListCandidate.clear();
        const NpcRuntime&     npc    = _listNpc[static_cast<size_t>( npcIndex )];
        const ScheduleNpcDef& def    = _pCatalog->getNpcs()[static_cast<size_t>( npc._defIndex )];
        const uint32          npcKey = Internal::hashNameToKey( def._id );
        int32                 order  = 0;

        for ( int32 routineIndex = 0; routineIndex < static_cast<int32>( def._listRoutine.size() ); ++routineIndex )
        {
            const ScheduleRoutineDef& routine    = def._listRoutine[static_cast<size_t>( routineIndex )];
            const uint32              routineKey = Internal::mixKey( npcKey, Internal::hashNameToKey( routine._id ) );
            for ( int32 blockIndex = 0; blockIndex < static_cast<int32>( routine._listBlock.size() ); ++blockIndex )
            {
                const ScheduleBlockDef&  block   = routine._listBlock[static_cast<size_t>( blockIndex )];
                ScheduleConditionContext context = makeContext( npcIndex, day, routineKey );
                context._phase                   = WorldClock::computePhaseAt( _settings._clock, static_cast<float32>( block._startMinute ) / 60.0f );
                if ( routine._condition.matches( context ) == false )
                    continue;
                context._chanceKey = Internal::mixKey( routineKey, static_cast<uint32>( blockIndex ) );
                if ( block._condition.matches( context ) == false )
                    continue;

                BlockCandidate candidate;
                candidate._pBlock      = &block;
                candidate._sourceId    = routine._id;
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
                        bBroken = bBroken || brokenIndex == appointmentIndex;
                    if ( appointmentIndex < 0 || bBroken )
                        continue;
                    const ScheduleAppointmentDef& appointment = _pCatalog->getAppointments()[static_cast<size_t>( appointmentIndex )];
                    // 약속의 확률은 참가자 모두에게 같아야 한다 — 열쇠에 NPC 를 섞지 않는다.
                    context._chanceKey = Internal::hashNameToKey( appointment._id );
                    if ( appointment._condition.matches( context ) == false )
                        continue;
                    candidate._sourceId   = appointment._id;
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
            for ( const hashed_string& npcId : event._listNpc )
                bAttendee = bAttendee || npcId == def._id;
            for ( const hashed_string& archetypeId : event._listArchetype )
                bAttendee = bAttendee || _pCatalog->isNpcOfArchetype( def, archetypeId );
            if ( bAttendee == false )
                continue;
            // 행사의 확률도 참가자 모두에게 같다(축제는 모두에게 열리거나 아무에게도 열리지 않는다).
            const uint32 eventKey = Internal::hashNameToKey( event._id );
            for ( int32 blockIndex = 0; blockIndex < static_cast<int32>( event._listBlock.size() ); ++blockIndex )
            {
                const ScheduleBlockDef&  block   = event._listBlock[static_cast<size_t>( blockIndex )];
                ScheduleConditionContext context = makeContext( npcIndex, day, eventKey );
                context._phase                   = WorldClock::computePhaseAt( _settings._clock, static_cast<float32>( block._startMinute ) / 60.0f );
                if ( event._condition.matches( context ) == false )
                    continue;
                context._chanceKey = Internal::mixKey( eventKey, static_cast<uint32>( blockIndex ) );
                if ( block._condition.matches( context ) == false )
                    continue;
                BlockCandidate candidate;
                candidate._pBlock      = &block;
                candidate._sourceId    = event._id;
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
        using Internal                    = ScheduleSystemInternal;
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
            segment._sourceId   = pCandidate->_sourceId;
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
                uint32           reservationId = 0;
                if ( _pLocator != nullptr && _pLocator->reserve( request, reserved, reservationId ) )
                {
                    segment._target        = reserved;
                    segment._reservationId = reservationId;
                }
                break;
            }
            case ScheduleActivityTarget::Wander:
            {
                // 돌아다니기 — `every` 분마다 장소 둘레의 새 자리. 자리는 (씨앗, NPC, 날, 시각)의 해시라 몇 번을 다시 세워도 같다.
                const int32   stepMinutes = pBlock != nullptr ? MathUtil::max( 1, pBlock->_wanderMinutes ) : 20;
                const float32 radius      = pBlock != nullptr && pBlock->_radius >= 0.0f ? pBlock->_radius : ( pPlace != nullptr ? pPlace->_radius : 0.0f );
                const uint32  npcKey      = Internal::hashNameToKey( def._id );
                for ( int32 hopStart = startMinute; hopStart < endMinute; hopStart += stepMinutes )
                {
                    const uint32    hash   = GameHash::hashCoord( day, hopStart, _settings._seed ^ npcKey );
                    const float32   angle  = GameHash::toUnitFloat( hash ) * ( 2.0f * MathUtil::Pi );
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
        using Internal                   = ScheduleSystemInternal;
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
            if ( oldSegment._reservationId == 0 )
                continue;
            bool bKept = false;
            for ( const ScheduleSegment& newSegment : listNew )
                bKept = bKept || newSegment._reservationId == oldSegment._reservationId;
            if ( bKept == false )
                _pLocator->release( oldSegment._reservationId );
        }
    }

    void ScheduleSystem::startDay( int32 day )
    {
        _listBrokenAppointment.clear();
        _listMetAppointment.clear();
        const int32              dayStart = day * kScheduleMinutesPerDay;
        vector<ScheduleLocation> listLocation;
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
            listLocation.push_back( computeLogicLocation( npcIndex, dayStart ) );
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
            planNpc( npcIndex, dayStart, listLocation[static_cast<size_t>( npcIndex )] );
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
                    bScheduled = bScheduled || ( segment._source == ScheduleSegmentSource::Appointment && segment._ownerIndex == appointmentIndex );
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
        context._pFlags      = _pFlags;
        context._pNpcTags    = &_listNpc[static_cast<size_t>( npcIndex )]._tags;
        context._pWorldTags  = _pWorldTags;
        context._weather     = _weather;
        context._day         = day;
        context._dayOfSeason = day % clock._daysPerSeason + 1;
        context._season      = clock._listSeason[static_cast<size_t>( ( day / clock._daysPerSeason ) % static_cast<int32>( clock._listSeason.size() ) )];
        context._chanceKey   = ScheduleSystemInternal::mixKey( chanceKey, _settings._seed );
        if ( vocabulary._listWeekday.empty() == false )
            context._weekday = vocabulary._listWeekday[static_cast<size_t>( day % static_cast<int32>( vocabulary._listWeekday.size() ) )];
        return context;
    }
} // namespace sw

namespace sw
{
    ScheduleLocation ScheduleSystem::computeLogicLocation( int32 npcIndex, int32 absoluteMinute ) const
    {
        const NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
        if ( npc._listInterruption.empty() == false )
            return npc._held;
        const float32 minuteOfDay = static_cast<float32>( absoluteMinute - npc._planDay * kScheduleMinutesPerDay );
        const int32   active      = ScheduleSystemInternal::findActiveSegment( npc._listSegment, minuteOfDay );
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
        const int32   active      = ScheduleSystemInternal::findActiveSegment( npc._listSegment, minuteOfDay );
        if ( active < 0 )
            return false;
        const ScheduleSegment& segment  = npc._listSegment[static_cast<size_t>( active )];
        float32                fraction = 1.0f;
        const bool             bThere   = segment._source == ScheduleSegmentSource::Appointment && segment._ownerIndex == appointmentIndex;
        return bThere && computePhase( npc, active, minuteOfDay, fraction ) == ScheduleNpcPhase::Performing;
    }

    void ScheduleSystem::processMinute( int32 minute )
    {
        if ( ScheduleSystemInternal::toMinuteOfDay( minute ) == 0 )
        {
            const int32 day  = ScheduleSystemInternal::toDay( minute );
            bool        bNew = false;
            for ( const NpcRuntime& npc : _listNpc )
                bNew = bNew || npc._planDay != day;
            if ( bNew )
                startDay( day );
        }
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
            popExpiredInterruptions( npcIndex, minute );
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
                bPlanned = bPlanned || ( segment._source == ScheduleSegmentSource::Appointment && segment._ownerIndex == inoutCheck._appointmentIndex );
            if ( bPlanned == false )
                continue;
            emitEvent( ScheduleEvent::Kind::AppointmentBroken, npcIndex, nullptr, appointment._id );
            replanNpc( npcIndex, _minute, false );
        }
    }

    void ScheduleSystem::refreshNpc( int32 npcIndex )
    {
        using Internal  = ScheduleSystemInternal;
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
            const uint64           key      = Internal::computeSegmentKey( segment );
            const bool             bChanged = npc._bEmitted == SW_FALSE || Internal::computeSegmentKey( npc._emittedSegment ) != key;
            if ( bChanged )
            {
                if ( npc._bEmitted != SW_FALSE && npc._emittedPhase == ScheduleNpcPhase::Performing )
                    emitEvent( ScheduleEvent::Kind::ActivityEnded, npcIndex, &npc._emittedSegment, npc._emittedSegment._sourceId );
                if ( phase == ScheduleNpcPhase::Traveling )
                    emitEvent( ScheduleEvent::Kind::Departed, npcIndex, &segment, segment._sourceId );
            }
            const bool bArrivedNow = phase == ScheduleNpcPhase::Performing && ( bChanged || npc._emittedPhase != ScheduleNpcPhase::Performing );
            if ( bArrivedNow )
            {
                emitEvent( ScheduleEvent::Kind::Arrived, npcIndex, &segment, segment._sourceId );
                emitEvent( ScheduleEvent::Kind::ActivityStarted, npcIndex, &segment, segment._sourceId );
                const bool bAppointment = segment._source == ScheduleSegmentSource::Appointment;
                bool       bMetBefore   = false;
                for ( const int32 metIndex : _listMetAppointment )
                    bMetBefore = bMetBefore || metIndex == segment._ownerIndex;
                if ( bAppointment && bMetBefore == false && isAppointmentMet( segment._sourceId ) )
                {
                    _listMetAppointment.push_back( segment._ownerIndex );
                    emitEvent( ScheduleEvent::Kind::AppointmentMet, npcIndex, &segment, segment._sourceId );
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
        using Internal         = ScheduleSystemInternal;
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
        if ( npc._lod == ScheduleLod::Far && segmentWake != Internal::kNoWake )
            segmentWake = Internal::roundUpToStep( segmentWake, _settings._farStepMinutes );
        return MathUtil::min( wake, segmentWake );
    }

    void ScheduleSystem::emitEvent( ScheduleEvent::Kind kind, int32 npcIndex, const ScheduleSegment* pSegment, const hashed_string& sourceId )
    {
        if ( _bSuppressEvents != SW_FALSE )
            return;
        ScheduleEvent event;
        event._kind     = kind;
        event._npcIndex = npcIndex;
        event._minute   = _minute;
        event._sourceId = sourceId;
        if ( pSegment != nullptr )
        {
            event._activity  = pSegment->_activity;
            event._animation = pSegment->_animation;
        }
        _eventBuffer.push( event );

        const NpcRuntime& npc         = _listNpc[static_cast<size_t>( npcIndex )];
        const bool        bAnimation  = kind == ScheduleEvent::Kind::ActivityStarted || kind == ScheduleEvent::Kind::ActivityEnded;
        const bool        bShouldPlay = _pAnimator != nullptr && npc._lod == ScheduleLod::Near && bAnimation && pSegment != nullptr;
        if ( bShouldPlay == false )
            return;
        ScheduleActivityCue cue;
        cue._location  = pSegment->_target;
        cue._activity  = pSegment->_activity;
        cue._animation = pSegment->_animation;
        cue._npc       = getNpcId( npcIndex );
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
        emitEvent( ScheduleEvent::Kind::ActivityStarted, npcIndex, &npc._listSegment[static_cast<size_t>( view._segmentIndex )], npc._listSegment[static_cast<size_t>( view._segmentIndex )]._sourceId );
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
        using Internal = ScheduleSystemInternal;
        for ( int32 npcIndex = 0; npcIndex < getNpcCount(); ++npcIndex )
        {
            NpcRuntime& npc = _listNpc[static_cast<size_t>( npcIndex )];
            if ( npc._lod != ScheduleLod::Near || npc._listInterruption.empty() == false )
                continue;
            const int32 active = Internal::findActiveSegment( npc._listSegment, static_cast<float32>( _minute - npc._planDay * kScheduleMinutesPerDay ) );
            if ( active < 0 )
                continue;
            const ScheduleSegment& segment = npc._listSegment[static_cast<size_t>( active )];
            const uint64           key     = Internal::computeSegmentKey( segment );
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

    void ScheduleSystem::explainNpc( int32 npcIndex, string& outText ) const
    {
        using Internal = ScheduleSystemInternal;
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
        text.appendFormat( " - %# %# at (%#, %#, %#) area '%#' [%#]\n", toString( view._phase ), view._activity.c_str(), view._location._position._x,
                           view._location._position._y, view._location._position._z, view._location._area.c_str(), toString( view._lod ) );
        if ( view._segmentIndex >= 0 )
        {
            const ScheduleSegment& segment = npc._listSegment[static_cast<size_t>( view._segmentIndex )];
            text.appendFormat( "  active: %# '%#' block #%# ", toString( segment._source ), segment._sourceId.c_str(), segment._blockIndex );
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
            const uint32              routineKey = Internal::mixKey( Internal::hashNameToKey( def._id ), Internal::hashNameToKey( routine._id ) );
            for ( int32 blockIndex = 0; blockIndex < static_cast<int32>( routine._listBlock.size() ); ++blockIndex )
            {
                const ScheduleBlockDef& block = routine._listBlock[static_cast<size_t>( blockIndex )];
                if ( block._startMinute > now || now >= block._endMinute )
                    continue;
                ScheduleConditionContext blockContext       = makeContext( npcIndex, npc._planDay, routineKey );
                blockContext._phase                         = WorldClock::computePhaseAt( _settings._clock, static_cast<float32>( block._startMinute ) / 60.0f );
                const ScheduleConditionResult routineResult = routine._condition.evaluate( blockContext );
                blockContext._chanceKey                     = Internal::mixKey( routineKey, static_cast<uint32>( blockIndex ) );
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
            ScheduleConditionContext      eventContext = makeContext( npcIndex, npc._planDay, Internal::hashNameToKey( event._id ) );
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
        if ( _pCatalog == nullptr || isValidNpc( npcIndex ) == false )
            return;
        const NpcRuntime&                       npc = _listNpc[static_cast<size_t>( npcIndex )];
        StringBuilder<constant::kMaxBuffer4096> text;
        text.appendFormat( "timeline '%#' day %# (planned at ", getNpcId( npcIndex ).c_str(), npc._planDay );
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
            text.appendFormat( " [%# '%#' p%#]\n", toString( segment._source ), segment._sourceId.c_str(), segment._priority );
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
            if ( bAll == false && getNpcId( npcIndex ) != hashed_string( _lastTraceRequest ) )
                continue;
            explainNpc( npcIndex, text );
            SW_LOG_INFO( "[Schedule] %#", text );
            dumpTimeline( npcIndex, text );
            SW_LOG_INFO( "[Schedule] %#", text );
        }
    }
} // namespace sw
