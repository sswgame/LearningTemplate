#include "pch.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleSystem.h"

#include "Core/Common/HashUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleActivity.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleHash.h"
#include "GameFramework/Base/Character/AI/Schedule/ScheduleSaveState.h"
#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

#include <mutex>

namespace sw
{
    SW_LOG_CALLER( "ScheduleSystem" );

    namespace
    {
        struct ScheduleSystemInternal
        {
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
        };

        /** @brief 살아 있는 일정 시스템 목록입니다. 에디터 디버그 창(`collectLiveSystems`)만 읽습니다. */
        struct ScheduleSystemRegistry
        {
            mutex                         _mutex;
            vector<const ScheduleSystem*> _listSystem;
        };

        /**
         * @brief 목록의 소유자입니다 — 엔진 수명 범위가 소유해 엔진 종료 때 사라진다(첫 일정 시스템이 만들어질 때 만들어진다).
         * @details 일정 시스템은 게임 컴포넌트의 값 멤버이거나 테스트의 지역 객체라 서비스 범위 없이도 서고 사라진다. 그래서 자기 생성자에서 오르고 소멸자에서 빠지되,
         *          목록이 먼저 사라졌으면(엔진 종료 뒤) 뺄 것이 없다.
         */
        ScopeOwned<ScheduleSystemRegistry> s_scheduleSystemRegistry;
    } // namespace
} // namespace sw

namespace sw
{
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

    void ScheduleSystem::collectLiveSystems( vector<const ScheduleSystem*>& outListSystem )
    {
        ScheduleSystemRegistry* const pRegistry = s_scheduleSystemRegistry.find();
        if ( pRegistry == nullptr )
        {
            outListSystem.clear(); // 아직 일정 시스템이 하나도 서지 않았다
            return;
        }
        ScopedLock<mutex> lock{ pRegistry->_mutex };
        outListSystem = pRegistry->_listSystem;
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
        , _listNPC{}
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
        ScheduleSystemRegistry* const pRegistry = s_scheduleSystemRegistry.getOrCreate();
        if ( pRegistry == nullptr )
            return; // 엔진 수명 범위가 닫힌 뒤다(Debug 는 `getOrCreate` 가 단언한다) — 올릴 목록이 없다
        ScopedLock<mutex> lock{ pRegistry->_mutex };
        pRegistry->_listSystem.push_back( this );
    }

    ScheduleSystem::~ScheduleSystem()
    {
        shutdown();
        ScheduleSystemRegistry* const pRegistry = s_scheduleSystemRegistry.find();
        if ( pRegistry == nullptr )
            return; // 목록이 먼저 사라졌다(엔진 종료 · 프로세스 끝) — 뺄 것이 없다
        ScopedLock<mutex> lock{ pRegistry->_mutex };
        for ( size_t systemIndex = 0; systemIndex < pRegistry->_listSystem.size(); ++systemIndex )
        {
            if ( pRegistry->_listSystem[systemIndex] != this )
                continue;
            pRegistry->_listSystem.erase( pRegistry->_listSystem.begin() + static_cast<ptrdiff_t>( systemIndex ) );
            break;
        }
    }

    bool ScheduleSystem::initialize( const ScheduleCatalog* pCatalog, const ScheduleSystemSettings& settings, int32 absoluteMinute )
    {
        // 값을 범위 안으로 맞춰 돌지 않는다. 상태를 바꾸기 전에 본다.
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false || absoluteMinute < 0 )
        {
            SW_LOG_ERROR( "ScheduleSystem: refused to start - settings '%#', minute %#", rangeError.c_str(), absoluteMinute );
            return false;
        }
        shutdown();
        _pCatalog = pCatalog;
        _settings = settings;
        if ( _settings._clock._listSeason.empty() ) // `WorldClockSettings::_listSeason` 의 계약(계절 없음 = 이름 하나)을 시계와 똑같이 따른다
            _settings._clock._listSeason.push_back( hashed_string( "Default" ) );
        _minute = absoluteMinute;
        _defaultLocator.initialize( pCatalog );
        if ( _pLocator == nullptr )
            _pLocator = &_defaultLocator;
        if ( _pPlanning == nullptr )
            _pPlanning = &_defaultPathing;
        if ( _pFine == nullptr )
            _pFine = _pPlanning;
        _flagsRevision = _pFlags != nullptr ? _pFlags->getRevision() : 0;
        if ( _pCatalog == nullptr )
            return true;

        const vector<ScheduleNPCDef>& listDef = _pCatalog->getNPCs();
        for ( int32 defIndex = 0; defIndex < static_cast<int32>( listDef.size() ); ++defIndex )
        {
            const ScheduleNPCDef& def = listDef[static_cast<size_t>( defIndex )];
            NPCRuntime            npc;
            npc._defIndex = defIndex;
            for ( const TagID& tag : def._listTag )
            {
                npc._tags.addTag( tag );
            }
            const SchedulePlaceDef* pHome = _pCatalog->findPlace( def._home );
            if ( pHome != nullptr )
                npc._origin = ScheduleLocation{ pHome->_position, pHome->_area };
            npc._held = npc._origin;
            _listNPC.push_back( npc );
        }
        _bSuppressEvents = SW_TRUE; // 처음 상태는 사건이 아니다
        for ( int32 npcIndex = 0; npcIndex < getNPCCount(); ++npcIndex )
        {
            planNPC( npcIndex, _minute, _listNPC[static_cast<size_t>( npcIndex )]._origin );
        }
        _bSuppressEvents = SW_FALSE;
        rebuildAppointmentChecks( getDay() );
        return true;
    }

    void ScheduleSystem::shutdown()
    {
        for ( NPCRuntime& npc : _listNPC )
        {
            for ( const ScheduleSegment& segment : npc._listSegment )
            {
                if ( segment._reservationID != 0 && _pLocator != nullptr )
                    _pLocator->release( segment._reservationID );
            }
        }
        _listNPC.clear();
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
        if ( _listNPC.empty() == false )
            _bConditionsDirty = SW_TRUE;
    }

    void ScheduleSystem::setActivityLocator( IScheduleActivityLocator* pLocator )
    {
        _pLocator = pLocator != nullptr ? pLocator : &_defaultLocator;
        if ( _listNPC.empty() == false )
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
            for ( const NPCRuntime& npc : _listNPC )
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
        for ( int32 npcIndex = 0; npcIndex < getNPCCount(); ++npcIndex )
        {
            NPCRuntime& npc = _listNPC[static_cast<size_t>( npcIndex )];
            if ( npc._listInterruption.empty() )
                continue;
            npc._listInterruption.clear();
            resumeNPC( npcIndex );
        }
        advanceTo( absoluteMinute );
        _bSuppressEvents = SW_FALSE;
        for ( int32 npcIndex = 0; npcIndex < getNPCCount(); ++npcIndex )
        {
            emitSnapped( npcIndex );
        }
    }

    void ScheduleSystem::setNPCLOD( int32 npcIndex, ScheduleLOD lod )
    {
        if ( isValidNPC( npcIndex ) == false )
            return;
        NPCRuntime& npc = _listNPC[static_cast<size_t>( npcIndex )];
        if ( npc._lod == lod )
            return;
        npc._lod = lod;
        // 화면 안으로 — 굵은 간격으로 미뤄 둔 사건을 지금 맞추고, 그 시각 자리로 옮긴다(계획은 LOD 와 상관없어 그대로다).
        refreshNPC( npcIndex );
        if ( lod == ScheduleLOD::Near )
        {
            npc._routeKey = 0;
            updateNearRoutes();
            emitSnapped( npcIndex );
        }
    }

    ScheduleLOD ScheduleSystem::getNPCLOD( int32 npcIndex ) const
    {
        return isValidNPC( npcIndex ) ? _listNPC[static_cast<size_t>( npcIndex )]._lod : ScheduleLOD::Far;
    }

    void ScheduleSystem::addNPCTag( int32 npcIndex, const TagID& tag )
    {
        const bool bHasTag = isValidNPC( npcIndex ) && _listNPC[static_cast<size_t>( npcIndex )]._tags.hasTag( tag, true );
        if ( isValidNPC( npcIndex ) == false || bHasTag )
            return;
        _listNPC[static_cast<size_t>( npcIndex )]._tags.addTag( tag );
        _listNPC[static_cast<size_t>( npcIndex )]._bTagsDirty = SW_TRUE;
    }

    void ScheduleSystem::removeNPCTag( int32 npcIndex, const TagID& tag )
    {
        if ( isValidNPC( npcIndex ) == false || _listNPC[static_cast<size_t>( npcIndex )]._tags.hasTag( tag, true ) == false )
            return; // 없는 NPC · 없는 태그
        _listNPC[static_cast<size_t>( npcIndex )]._tags.removeTag( tag );
        _listNPC[static_cast<size_t>( npcIndex )]._bTagsDirty = SW_TRUE;
    }

    void ScheduleSystem::setNPCAffinity( int32 npcIndex, int32 affinity )
    {
        if ( isValidNPC( npcIndex ) == false || _listNPC[static_cast<size_t>( npcIndex )]._affinity == affinity )
            return;
        _listNPC[static_cast<size_t>( npcIndex )]._affinity   = affinity;
        _listNPC[static_cast<size_t>( npcIndex )]._bTagsDirty = SW_TRUE;
    }

    int32 ScheduleSystem::getNPCAffinity( int32 npcIndex ) const
    {
        return isValidNPC( npcIndex ) ? _listNPC[static_cast<size_t>( npcIndex )]._affinity : 0;
    }

    bool ScheduleSystem::pushInterruption( int32 npcIndex, const hashed_string& interruptID )
    {
        if ( _pCatalog == nullptr || isValidNPC( npcIndex ) == false )
            return false;
        const ScheduleInterruptDef* pDef = _pCatalog->findInterrupt( interruptID );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING( "unknown interruption '%#' for npc '%#' - ignored", interruptID.c_str(), getNPCID( npcIndex ).c_str() );
            return false;
        }
        NPCRuntime& npc          = _listNPC[static_cast<size_t>( npcIndex )];
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
            refreshNPC( npcIndex ); // 끼어들기 앞까지의 사건을 맞춘다
            npc._held = computeLogicLocation( npcIndex, _minute );
            if ( npc._bEmitted != SW_FALSE && npc._emittedPhase == ScheduleNPCPhase::Performing )
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
        if ( isValidNPC( npcIndex ) == false )
            return false;
        NPCRuntime& npc = _listNPC[static_cast<size_t>( npcIndex )];
        for ( size_t interruptionIndex = 0; interruptionIndex < npc._listInterruption.size(); ++interruptionIndex )
        {
            if ( npc._listInterruption[interruptionIndex]._id != interruptID )
                continue;
            npc._listInterruption.erase( npc._listInterruption.begin() + static_cast<ptrdiff_t>( interruptionIndex ) );
            if ( npc._listInterruption.empty() )
                resumeNPC( npcIndex );
            else
                npc._wakeMinute = computeWakeMinute( npcIndex );
            return true;
        }
        return false;
    }

    void ScheduleSystem::reportInterruptedLocation( int32 npcIndex, const ScheduleLocation& location )
    {
        if ( isValidNPC( npcIndex ) == false || _listNPC[static_cast<size_t>( npcIndex )]._listInterruption.empty() )
            return;
        _listNPC[static_cast<size_t>( npcIndex )]._held = location;
    }

    int32 ScheduleSystem::findNPCIndex( const hashed_string& npcID ) const
    {
        for ( int32 npcIndex = 0; npcIndex < getNPCCount(); ++npcIndex )
        {
            if ( getNPCID( npcIndex ) == npcID )
                return npcIndex;
        }
        return -1;
    }

    hashed_string ScheduleSystem::getNPCID( int32 npcIndex ) const
    {
        if ( _pCatalog == nullptr || isValidNPC( npcIndex ) == false )
            return hashed_string{};
        return _pCatalog->getNPCs()[static_cast<size_t>( _listNPC[static_cast<size_t>( npcIndex )]._defIndex )]._id;
    }

    ScheduleNPCView ScheduleSystem::getNPCView( int32 npcIndex ) const
    {
        ScheduleNPCView view;
        if ( isValidNPC( npcIndex ) == false )
            return view;
        const NPCRuntime& npc         = _listNPC[static_cast<size_t>( npcIndex )];
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
            case ScheduleNPCPhase::Interrupted:
            {
                view._location = npc._held;
                break;
            }
            case ScheduleNPCPhase::Performing:
            {
                view._location = segment._target;
                break;
            }
            case ScheduleNPCPhase::Traveling:
            {
                view._location               = computePlanningLocationOnRoute( npc, view._segmentIndex, view._travelFraction );
                const bool bNearRouteCurrent = npc._lod == ScheduleLOD::Near && npc._routeKey == ScheduleHash::computeSegmentKey( segment );
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
        return isValidNPC( npcIndex ) ? _listNPC[static_cast<size_t>( npcIndex )]._listSegment : s_empty;
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
            if ( isAttendeePresent( findNPCIndex( attendee ), appointmentIndex ) == false )
                return false;
        }
        return true;
    }

    bool ScheduleSystem::isAppointmentBroken( const hashed_string& appointmentID ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const int32 appointmentIndex = _pCatalog->findAppointmentIndex( appointmentID );
        return VectorUtil::contains( _listBrokenAppointment, appointmentIndex );
    }

    int32 ScheduleSystem::getDay() const
    {
        return ScheduleSystemInternal::toDay( _minute );
    }

    void ScheduleSystem::drainEvents( vector<ScheduleEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }
} // namespace sw
