#include "pch.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleActivity.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct ScheduleCatalogInternal
        {
            /** @brief 표에 없는 속성마다 경고합니다. @p bCondition 이면 조건 속성도 받습니다. */
            /** @brief 시각 속성을 분으로 읽는다. 속성이 없거나 깨졌으면(깨진 경우 @p inoutbValid 를 내린다) 값이 없다. 없을 때의 값은 호출부가 정한다. */
            [[nodiscard]] static optional<int32> readClock( const XMLNode& node, const utf8* pName, string_view sourceName, string_view ownerName, bool& inoutbValid )
            {
                const utf8* pText = node.findAttribute( pName );
                if ( pText == nullptr )
                    return nullopt;
                int32 minutes = 0;
                if ( ScheduleCatalog::parseClockMinutes( pText, minutes ) )
                    return minutes;
                SW_LOG_WARNING_ID( "Schedule.TimeInvalid", "%#: '%#' has an invalid %# time '%#' (expected H:MM in 0:00..24:00)", sourceName, ownerName, pName, pText );
                inoutbValid = false;
                return nullopt;
            }

            /** @brief 시작 순으로 놓은 칸 번호 — 겹침 검사가 쓴다. */
            struct BlockStartLess
            {
                const vector<ScheduleBlockDef>* _pListBlock;

                bool operator()( int32 lhs, int32 rhs ) const
                {
                    return ( *_pListBlock )[static_cast<size_t>( lhs )]._startMinute < ( *_pListBlock )[static_cast<size_t>( rhs )]._startMinute;
                }
            };

            /** @brief @p node 아래 모든 깊이의 `Block` 원소마다 @p visit 를 부릅니다(NPC · 아키타입의 루틴, 행사). */
            template <typename TVisit>
            static void visitBlocks( const XMLNode& node, const TVisit& visit )
            {
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Block" ) )
                        visit( child );
                    else
                        visitBlocks( child, visit );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ScheduleCatalog::ScheduleCatalog()
        : _placeCatalog{}
        , _spotCatalog{}
        , _interruptCatalog{}
        , _appointmentCatalog{}
        , _archetypeCatalog{}
        , _npcCatalog{}
        , _eventCatalog{}
        , _vocabulary{}
        , _pActivityRegistry{ nullptr }
        , _bUnknownIdleActivity{ SW_FALSE }
    {
    }

    void ScheduleCatalog::clear()
    {
        _placeCatalog.clear();
        _spotCatalog.clear();
        _interruptCatalog.clear();
        _appointmentCatalog.clear();
        _archetypeCatalog.clear();
        _npcCatalog.clear();
        _eventCatalog.clear();
        _vocabulary           = ScheduleConditionSchema{};
        _bUnknownIdleActivity = SW_FALSE;
    }

    bool ScheduleCatalog::parseClockMinutes( string_view text, int32& outMinutes )
    {
        const string_view trimmed = StringUtil::trim( text );
        const size_t      colon   = trimmed.find( ':' );
        int32             hours   = 0;
        int32             minutes = 0;
        if ( colon == string_view::npos )
        {
            if ( StringUtil::parseInt( trimmed, hours ) == false )
                return false;
        }
        else
        {
            const string_view minuteText      = trimmed.substr( colon + 1 );
            const bool        bMalformedClock = minuteText.size() != 2 || StringUtil::parseInt( trimmed.substr( 0, colon ), hours ) == false || StringUtil::parseInt( minuteText, minutes ) == false;
            if ( bMalformedClock )
                return false;
        }
        const bool  bMinuteValid      = 0 <= minutes && minutes < 60;
        const int32 total             = hours * 60 + minutes;
        const bool  bInvalidClockTime = bMinuteValid == false || hours < 0 || total > kScheduleMinutesPerDay;
        if ( bInvalidClockTime )
            return false;
        outMinutes = total;
        return true;
    }

    const ScheduleActivityRegistry& ScheduleCatalog::getActivityRegistry() const
    {
        return _pActivityRegistry != nullptr ? *_pActivityRegistry : ScheduleActivityRegistry::getBuiltin();
    }

    bool ScheduleCatalog::isNPCOfArchetype( const ScheduleNPCDef& npc, const hashed_string& archetypeID ) const
    {
        hashed_string current = npc._archetype;
        // 순환은 읽을 때 끊었지만 묶음 수만큼만 걷는다.
        for ( size_t step = 0; step <= _archetypeCatalog.getCount() && current.empty() == false; ++step )
        {
            if ( current == archetypeID )
                return true;
            const ScheduleArchetypeDef* pArchetype = findArchetype( current );
            current                                = pArchetype != nullptr ? pArchetype->_parent : hashed_string{};
        }
        return false;
    }

    uint32 ScheduleCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        /** 원소를 읽는 쪽은 id 글을 받고 알림은 스스로 남기므로 줄 모양으로 잇는다. 함수 안의 형식이라 이 클래스의 비공개를 볼 수 있다. id 는 원소마다의 칸 구조체가 필수로 읽는다 — 여기서는 오류 문장에 쓸 글만 꺼낸다. */
        struct ScheduleElementReader
        {
            ScheduleCatalog& _catalog;

            static const utf8* findID( const XMLNode& node ) { return node.getAttributeText( "id" ).data(); }

            bool loadCalendar( const XMLNode& node, string_view sourceName )
            {
                _catalog.loadCalendar( node, sourceName );
                return true;
            }
            bool loadPlace( const XMLNode& node, string_view sourceName )
            {
                _catalog.loadPlace( node, sourceName, findID( node ) );
                return true;
            }
            bool loadSpot( const XMLNode& node, string_view sourceName )
            {
                _catalog.loadSpot( node, sourceName, findID( node ) );
                return true;
            }
            bool loadInterrupt( const XMLNode& node, string_view sourceName )
            {
                _catalog.loadInterrupt( node, sourceName, findID( node ) );
                return true;
            }
            bool loadAppointment( const XMLNode& node, string_view sourceName )
            {
                _catalog.loadAppointment( node, sourceName, findID( node ) );
                return true;
            }
            bool loadArchetype( const XMLNode& node, string_view sourceName )
            {
                _catalog.loadArchetype( node, sourceName, findID( node ) );
                return true;
            }
            bool loadNPC( const XMLNode& node, string_view sourceName ) { return _catalog.loadNPC( node, sourceName, findID( node ) ); }
            bool loadEvent( const XMLNode& node, string_view sourceName )
            {
                _catalog.loadEvent( node, sourceName, findID( node ) );
                return true;
            }
        };
        // 가리켜지는 것을 먼저 읽어 파일 안 순서와 상관없이 이름을 검사한다 — 달력 · 장소 · 자리 · 끼어들기 → 약속 → 묶음 → NPC · 행사.
        using Element                          = XMLCatalogElement<ScheduleElementReader>;
        static constexpr Element kArrElement[] = {
            Element::of<&ScheduleElementReader::loadCalendar>( "Calendar" ).uncounted(),
            Element::of<&ScheduleElementReader::loadPlace>( "Place" ).uncounted(),
            Element::of<&ScheduleElementReader::loadSpot>( "Spot" ).uncounted(),
            Element::of<&ScheduleElementReader::loadInterrupt>( "Interrupt" ).uncounted(),
            Element::of<&ScheduleElementReader::loadAppointment>( "Appointment" ).uncounted(),
            Element::of<&ScheduleElementReader::loadArchetype>( "Archetype" ).uncounted(),
            Element::of<&ScheduleElementReader::loadNPC>( "NPC" ),
            Element::of<&ScheduleElementReader::loadEvent>( "Event" ).uncounted(),
        };
        _bUnknownIdleActivity = SW_FALSE;
        ScheduleElementReader reader{ *this };
        const uint32          loadedCount = XMLCatalogReadResult::readInTableOrder( reader, root, sourceName, kArrElement )._loadedCount;
        resolveReferences( sourceName );
        // 같은 파일의 id 연결은 참조 표가 본다(로드 실패). 코드 등록 이름(활동)은 여기서 보고 같은 정책(로드 실패)으로 맞춘다.
        const bool bActivitiesKnown = validateActivityNames( root.getNodeForHandRead(), sourceName ) && _bUnknownIdleActivity == SW_FALSE; // 손 읽기: 표가 읽은 뒤 깊은 <Block> 의 활동 이름만 다시 본다
        return bActivitiesKnown ? loadedCount : 0;
    }

    void ScheduleCatalog::loadCalendar( const XMLNode& node, string_view sourceName )
    {
        (void)XMLFields::readProperties( node, _vocabulary, sourceName );
    }

    void ScheduleCatalog::loadPlace( const XMLNode& node, string_view sourceName, const utf8* pID )
    {
        // 위치 없는 장소를 원점에 두지 않는다 — 읽히지 않으면 오류를 알리고 장소를 더하지 않는다(그 장소를 가리키는 약속이 "unknown place" 로 걸러진다).
        SchedulePlaceDef place;
        if ( XMLFields::readProperties( node, place, sourceName ) )
            (void)_placeCatalog.add( place );
        else
            SW_LOG_ERROR_ID( "ScheduleCatalog.PlaceInvalidFieldsSkipped", "%#: place '%#' has invalid fields - skipped", sourceName, pID );
    }

    void ScheduleCatalog::loadSpot( const XMLNode& node, string_view sourceName, const utf8* pID )
    {
        ScheduleSpotDef spot;
        if ( XMLFields::readProperties( node, spot, sourceName ) == false ) // 종류 · 위치 필수, 수용 >= 1
            SW_LOG_ERROR_ID( "ScheduleCatalog.SpotInvalidFieldsSkipped", "%#: spot '%#' has invalid fields - skipped", sourceName, pID );
        else if ( spot._kind.empty() )
            SW_LOG_ERROR_ID( "ScheduleCatalog.SpotNoKindSkipped", "%#: spot '%#' has no kind - skipped", sourceName, pID );
        else
            (void)_spotCatalog.add( spot );
    }

    void ScheduleCatalog::loadInterrupt( const XMLNode& node, string_view sourceName, const utf8* pID )
    {
        ScheduleInterruptDef interrupt;
        if ( XMLFields::readProperties( node, interrupt, sourceName ) )
            (void)_interruptCatalog.add( interrupt );
        else
            SW_LOG_ERROR_ID( "ScheduleCatalog.InterruptInvalidFieldsSkipped", "%#: interrupt '%#' has invalid fields - skipped", sourceName, pID );
    }

    void ScheduleCatalog::loadAppointment( const XMLNode& node, string_view sourceName, const utf8* pID )
    {
        using Internal = ScheduleCatalogInternal;
        ScheduleAppointmentDef appointment;
        // 시각(H:MM)과 조건 속성은 손으로 읽는다.
        bool bValid              = XMLFields::readProperties( node, appointment, sourceName, { "start", "end" }, &ScheduleCondition::isConditionAttribute );
        appointment._startMinute = Internal::readClock( node, "start", sourceName, pID, bValid ).value_or( -1 );
        appointment._endMinute   = Internal::readClock( node, "end", sourceName, pID, bValid ).value_or( -1 );
        bValid                   = appointment._condition.loadFromXML( node, _vocabulary, sourceName, pID ) && bValid;
        if ( bValid && ( appointment._startMinute < 0 || appointment._endMinute <= appointment._startMinute ) )
        {
            SW_LOG_ERROR_ID( "Schedule.AppointmentTimeOrder", "%#: appointment '%#' needs start < end - skipped", sourceName, pID );
            bValid = false;
        }
        if ( bValid )
            (void)_appointmentCatalog.add( appointment );
    }

    void ScheduleCatalog::readRoutineChildren( const XMLNode& node, string_view sourceName, string_view ownerKind, string_view ownerID, vector<ScheduleRoutineDef>& outListRoutine ) const
    {
        for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Routine" ) == false )
            {
                SW_LOG_WARNING_ID( "ScheduleCatalog.UnknownElement", "%#: %# '%#' has unknown element <%#>", sourceName, ownerKind, ownerID, child.getName() );
                continue;
            }
            ScheduleRoutineDef routine;
            if ( readRoutine( child, sourceName, ownerID, routine ) )
                outListRoutine.push_back( routine );
        }
    }

    void ScheduleCatalog::loadArchetype( const XMLNode& node, string_view sourceName, const utf8* pID )
    {
        ScheduleArchetypeDef archetype;
        if ( XMLFields::readProperties( node, archetype, sourceName ) == false )
            return;
        readRoutineChildren( node, sourceName, "archetype", pID, archetype._listRoutine );
        (void)_archetypeCatalog.add( archetype );
    }

    bool ScheduleCatalog::loadNPC( const XMLNode& node, string_view sourceName, const utf8* pID )
    {
        ScheduleNPCDef npc;
        if ( XMLFields::readProperties( node, npc, sourceName ) == false ) // 속도 >= 0.01 은 PROPERTY 가 본다
        {
            SW_LOG_ERROR_ID( "ScheduleCatalog.NpcInvalidFieldsSkipped", "%#: npc '%#' has invalid fields - skipped", sourceName, pID );
            return false;
        }
        if ( npc._idleActivity.empty() )
            npc._idleActivity = hashed_string( "StayHome" ); // 적지 않으면 집에 머문다
        const ScheduleActivityDef* pIdle = getActivityRegistry().findActivity( npc._idleActivity );
        if ( pIdle == nullptr || pIdle->_target != ScheduleActivityTarget::Home )
        {
            SW_LOG_ERROR_ID( "ScheduleCatalog.NpcIdleActivityNot", "%#: npc '%#' has idle activity '%#' that is not a home activity - the catalog does not load", sourceName, pID, npc._idleActivity.c_str() );
            _bUnknownIdleActivity = SW_TRUE; // 코드 등록 이름이라 표가 보지 못한다 — 블록의 활동과 같은 정책(로드 실패)
            return false;
        }
        readRoutineChildren( node, sourceName, "npc", pID, npc._listRoutine );
        (void)_npcCatalog.add( npc );
        return true;
    }

    void ScheduleCatalog::loadEvent( const XMLNode& node, string_view sourceName, const utf8* pID )
    {
        ScheduleEventDef event;
        bool             bValid = XMLFields::readProperties( node, event, sourceName, {}, &ScheduleCondition::isConditionAttribute );
        bValid                  = event._condition.loadFromXML( node, _vocabulary, sourceName, pID ) && bValid;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "ScheduleCatalog.EventInvalidFieldsSkipped", "%#: event '%#' has invalid fields - skipped", sourceName, pID );
            return;
        }
        for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Block" ) == false )
            {
                SW_LOG_WARNING_ID( "ScheduleCatalog.EventUnknownElement", "%#: event '%#' has unknown element <%#>", sourceName, pID, child.getName() );
                continue;
            }
            readBlock( child, sourceName, pID, false, event._listBlock );
        }
        warnOverlappingBlocks( event._listBlock, sourceName, pID );
        (void)_eventCatalog.add( event );
    }

    bool ScheduleCatalog::readRoutine( const XMLNode& node, string_view sourceName, string_view ownerName, ScheduleRoutineDef& outRoutine ) const
    {
        bool bValid = XMLFields::readProperties( node, outRoutine, sourceName, {}, &ScheduleCondition::isConditionAttribute );
        if ( outRoutine._id.empty() )
        {
            SW_LOG_ERROR_ID( "ScheduleCatalog.RoutineWithoutIdSkipped", "%#: '%#' has a <Routine> without an id - skipped", sourceName, ownerName );
            bValid = false;
        }
        bValid = outRoutine._condition.loadFromXML( node, _vocabulary, sourceName, ownerName ) && bValid;
        for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Block" ) == false )
            {
                SW_LOG_WARNING_ID( "ScheduleCatalog.RoutineUnknownElement", "%#: routine '%#' of '%#' has unknown element <%#>", sourceName, outRoutine._id.c_str(), ownerName, child.getName() );
                continue;
            }
            readBlock( child, sourceName, ownerName, true, outRoutine._listBlock );
        }
        warnOverlappingBlocks( outRoutine._listBlock, sourceName, ownerName );
        return bValid;
    }

    void ScheduleCatalog::readBlock( const XMLNode& node, string_view sourceName, string_view ownerName, bool bInRoutine, vector<ScheduleBlockDef>& inoutListBlock ) const
    {
        ScheduleBlockDef block;
        int32            tailEndMinute{ -1 };
        if ( readSingleBlock( node, sourceName, ownerName, bInRoutine, block, tailEndMinute ) == false )
            return;
        inoutListBlock.push_back( block );
        if ( tailEndMinute > 0 )
        {
            // 자정을 넘는 칸의 다음 날 새벽 쪽이다 — 같은 활동 · 장소 · 조건이고 0:00 에서 시작한다.
            ScheduleBlockDef tail        = block;
            tail._startMinute            = 0;
            tail._endMinute              = tailEndMinute;
            tail._onStart                = GameActionCall{}; // 나뉘어도 활동은 하나다 — 시작은 앞 칸, 끝은 뒤 칸에서 한 번씩 부른다
            inoutListBlock.back()._onEnd = GameActionCall{};
            inoutListBlock.push_back( tail );
        }
    }

    bool ScheduleCatalog::readSingleBlock( const XMLNode& node, string_view sourceName, string_view ownerName, bool bInRoutine, ScheduleBlockDef& outBlock,
                                           int32& outTailEndMinute ) const
    {
        using Internal   = ScheduleCatalogInternal;
        outTailEndMinute = -1;
        // 시각은 시계 표기이고 자정 넘김은 칸이 아니라 시각을 고치는 표시다.
        bool bPropertiesValid = XMLFields::readProperties( node, outBlock, sourceName, { "start", "end", "overnight", "onStart", "onEnd" }, &ScheduleCondition::isConditionAttribute );
        bPropertiesValid      = outBlock._condition.loadFromXML( node, _vocabulary, sourceName, ownerName ) && bPropertiesValid;
        if ( const utf8* const pOnStart = node.findAttribute( "onStart" ); pOnStart != nullptr )
            bPropertiesValid = GameActionCall::compileText( pOnStart, sourceName, outBlock._onStart ) && bPropertiesValid;
        if ( const utf8* const pOnEnd = node.findAttribute( "onEnd" ); pOnEnd != nullptr )
            bPropertiesValid = GameActionCall::compileText( pOnEnd, sourceName, outBlock._onEnd ) && bPropertiesValid;
        bool bOvernight  = false;
        bPropertiesValid = XMLReadUtil::readOptionalBool( node, "overnight", bOvernight ) && bPropertiesValid;
        if ( bPropertiesValid == false )
        {
            SW_LOG_ERROR_ID( "ScheduleCatalog.BlockInvalidFieldsSkipped", "%#: '%#' has a block with invalid fields - skipped", sourceName, ownerName );
            return false;
        }

        // 모르는 활동 · 장소는 로드 실패다(활동은 `validateActivityNames`, 장소는 참조 표가 남긴다) — 여기서는 그 칸만 빼고 다시 알리지 않는다.
        const ScheduleActivityDef* pActivity = getActivityRegistry().findActivity( outBlock._activity );
        if ( pActivity == nullptr || ( outBlock._place.empty() == false && findPlace( outBlock._place ) == nullptr ) )
            return false;

        bool bValid = true;
        switch ( pActivity->_target )
        {
            case ScheduleActivityTarget::Place:
            case ScheduleActivityTarget::Wander:
            {
                if ( outBlock._place.empty() )
                {
                    SW_LOG_WARNING_ID( "Schedule.BlockWithoutPlace", "%#: '%#' has a %# block without a place - skipped", sourceName, ownerName, outBlock._activity.c_str() );
                    return false;
                }
                break;
            }
            case ScheduleActivityTarget::SmartObject:
            {
                if ( outBlock._objectKind.empty() )
                {
                    SW_LOG_WARNING_ID( "Schedule.BlockWithoutObjectKind", "%#: '%#' has a %# block without an objectKind - skipped", sourceName, ownerName, outBlock._activity.c_str() );
                    return false;
                }
                break;
            }
            case ScheduleActivityTarget::Appointment:
            {
                if ( bInRoutine == false )
                {
                    SW_LOG_WARNING_ID( "ScheduleCatalog.BlockAppointmentOutsideRoutine", "%#: '%#' has a %# block outside a routine - skipped", sourceName, ownerName, outBlock._activity.c_str() );
                    return false;
                }
                const ScheduleAppointmentDef* pAppointment = findAppointment( outBlock._appointment );
                if ( pAppointment == nullptr )
                    return false; // 모르는 약속은 참조 표가 남기고 로드를 실패시킨다(빈 약속은 Required 가 아니라 여기서 빠진다)
                if ( node.findAttribute( "start" ) != nullptr || node.findAttribute( "end" ) != nullptr )
                    SW_LOG_WARNING_ID( "ScheduleCatalog.BlockOwnStartEnd", "%#: '%#' has a %# block with its own start/end - the appointment's time is used", sourceName, ownerName, outBlock._activity.c_str() );
                outBlock._startMinute = pAppointment->_startMinute;
                outBlock._endMinute   = pAppointment->_endMinute;
                outBlock._place       = pAppointment->_place;
                return true;
            }
            case ScheduleActivityTarget::Home:
            {
                break;
            }
        }
        outBlock._startMinute = Internal::readClock( node, "start", sourceName, ownerName, bValid ).value_or( -1 );
        outBlock._endMinute   = Internal::readClock( node, "end", sourceName, ownerName, bValid ).value_or( -1 );
        // `overnight="true"` 인 칸은 끝이 시작보다 이른 것이 정상이다(22:00–6:00, 자정을 넘는다). 끝이 0:00 이면 24:00 으로 읽는다.
        // 표시가 없는 칸의 start ≥ end 는 실수라서 아래에서 경고하고 뺀다.
        const bool bWrapsMidnight = bValid && bOvernight && outBlock._startMinute > 0 && outBlock._endMinute >= 0 &&
                                    outBlock._endMinute < outBlock._startMinute;
        if ( bWrapsMidnight )
        {
            outTailEndMinute    = outBlock._endMinute;
            outBlock._endMinute = kScheduleMinutesPerDay;
            if ( outTailEndMinute == 0 )
                outTailEndMinute = -1; // 24:00 에서 끝나는 칸일 뿐, 새벽 쪽은 없다
        }
        if ( bValid && ( outBlock._startMinute < 0 || outBlock._endMinute <= outBlock._startMinute ) )
        {
            SW_LOG_WARNING_ID( "Schedule.BlockTimeOrder", "%#: '%#' has a %# block that needs start < end - skipped", sourceName, ownerName, outBlock._activity.c_str() );
            bValid = false;
        }
        return bValid;
    }

    void ScheduleCatalog::warnOverlappingBlocks( const vector<ScheduleBlockDef>& listBlock, string_view sourceName, string_view ownerName ) const
    {
        // 조건이 없는 칸끼리 겹치면 늘 뒤 칸이 지므로 실수다. 조건이 있는 칸은 "그 조건이면 이것" 이라 겹쳐도 되고(앞 칸이 이긴다),
        // 약속 칸은 약속의 우선순위로 그 시간을 덮는 것이라 겹쳐도 된다(약속이 깨지면 아래 칸이 드러난다).
        vector<int32> listOrder;
        for ( int32 blockIndex = 0; blockIndex < static_cast<int32>( listBlock.size() ); ++blockIndex )
        {
            const ScheduleBlockDef& block = listBlock[static_cast<size_t>( blockIndex )];
            if ( block._condition.isEmpty() && block._appointment.empty() )
                listOrder.push_back( blockIndex );
        }
        VectorUtil::stableSort( listOrder, ScheduleCatalogInternal::BlockStartLess{ &listBlock } );
        for ( size_t orderIndex = 1; orderIndex < listOrder.size(); ++orderIndex )
        {
            const ScheduleBlockDef& previous = listBlock[static_cast<size_t>( listOrder[orderIndex - 1] )];
            const ScheduleBlockDef& current  = listBlock[static_cast<size_t>( listOrder[orderIndex] )];
            if ( current._startMinute < previous._endMinute )
                SW_LOG_WARNING_ID( "Schedule.BlocksOverlap", "%#: '%#' has overlapping blocks (%# and %#)", sourceName, ownerName, previous._activity.c_str(), current._activity.c_str() );
        }
    }

    void ScheduleCatalog::resolveReferences( string_view sourceName )
    {
        // 묶음 순환 — 부모를 묶음 수보다 많이 따라가면 순환이다. 그 묶음의 부모를 끊는다.
        const size_t archetypeCount = _archetypeCatalog.getCount();
        for ( size_t archetypeIndex = 0; archetypeIndex < archetypeCount; ++archetypeIndex )
        {
            ScheduleArchetypeDef archetype = _archetypeCatalog.getAt( archetypeIndex );
            hashed_string        current   = archetype._parent;
            size_t               step      = 0;
            for ( ; step <= archetypeCount && current.empty() == false && current != archetype._id; ++step )
            {
                const ScheduleArchetypeDef* pParent = findArchetype( current );
                if ( pParent == nullptr )
                    break; // 모르는 부모는 참조 표가 남기고 로드를 실패시킨다
                current = pParent->_parent;
            }
            const bool bCycle = current.empty() == false && ( current == archetype._id || step > archetypeCount );
            if ( bCycle )
            {
                SW_LOG_WARNING_ID( "Schedule.ArchetypeParentCycle", "%#: archetype '%#' is part of a parent cycle - parent dropped", sourceName, archetype._id.c_str() );
                archetype._parent = hashed_string{};
                (void)_archetypeCatalog.replace( archetype );
            }
        }

        // NPC 루틴 — 자기 것 다음에 묶음 사슬의 것(같은 id 는 가까운 쪽이 덮는다). 약속 참가자를 모은다.
        vector<vector<hashed_string>> listAttendeePerAppointment( _appointmentCatalog.getCount() );
        for ( size_t npcIndex = 0; npcIndex < _npcCatalog.getCount(); ++npcIndex )
        {
            ScheduleNPCDef        npc = _npcCatalog.getAt( npcIndex );
            vector<hashed_string> listRoutineID;
            for ( const ScheduleRoutineDef& routine : npc._listRoutine )
            {
                listRoutineID.push_back( routine._id );
            }
            hashed_string current = npc._archetype;
            for ( size_t step = 0; step <= archetypeCount && current.empty() == false; ++step )
            {
                const ScheduleArchetypeDef* pArchetype = findArchetype( current );
                if ( pArchetype == nullptr )
                    break;
                for ( const ScheduleRoutineDef& routine : pArchetype->_listRoutine )
                {
                    if ( VectorUtil::contains( listRoutineID, routine._id ) )
                        continue;
                    listRoutineID.push_back( routine._id );
                    npc._listRoutine.push_back( routine );
                }
                current = pArchetype->_parent;
            }
            for ( const ScheduleRoutineDef& routine : npc._listRoutine )
            {
                for ( const ScheduleBlockDef& block : routine._listBlock )
                {
                    const optional<int32> appointmentIndex = _appointmentCatalog.findIndex( block._appointment );
                    if ( block._appointment.empty() || appointmentIndex.has_value() == false )
                        continue;
                    vector<hashed_string>& listAttendee = listAttendeePerAppointment[static_cast<size_t>( *appointmentIndex )];
                    if ( VectorUtil::contains( listAttendee, npc._id ) == false )
                        listAttendee.push_back( npc._id );
                }
            }
            (void)_npcCatalog.replace( npc );
        }
        for ( size_t appointmentIndex = 0; appointmentIndex < _appointmentCatalog.getCount(); ++appointmentIndex )
        {
            ScheduleAppointmentDef appointment = _appointmentCatalog.getAt( appointmentIndex );
            appointment._listAttendee          = listAttendeePerAppointment[appointmentIndex];
            if ( appointment._listAttendee.size() < 2 )
                SW_LOG_WARNING_ID( "ScheduleCatalog.AppointmentReferencedNpcMeeting", "%#: appointment '%#' is referenced by %# npc(s) - a meeting needs two", sourceName, appointment._id.c_str(),
                                   static_cast<uint32>( appointment._listAttendee.size() ) );
            (void)_appointmentCatalog.replace( appointment );
        }
    }

    bool ScheduleCatalog::validateActivityNames( const XMLNode& root, string_view sourceName ) const
    {
        const ScheduleActivityRegistry& registry = getActivityRegistry();
        bool                            bKnown   = true;
        ScheduleCatalogInternal::visitBlocks( root, [&]( const XMLNode& block )
        {
            const string_view activity = block.getAttributeText( "activity" );
            if ( activity.empty() == false && registry.findActivity( hashed_string( activity ) ) != nullptr )
                return;
            SW_LOG_ERROR_ID( "Schedule.BlockActivityUnknown", "%#: a <Block> names an unknown activity '%#' - the catalog does not load", sourceName, activity );
            bKnown = false;
        } );
        return bKnown;
    }
} // namespace sw

namespace sw
{
    /** @brief `ScheduleCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( Schedules, ScheduleCatalog, SW_GF_API, ScheduleCatalog::kAssetType.withLoader( AssetTypeLoader::of<ScheduleCatalog>( MemoryTag::Game ) ) );
} // namespace sw
