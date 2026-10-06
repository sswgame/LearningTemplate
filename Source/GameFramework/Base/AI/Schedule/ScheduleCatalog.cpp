#include "pch.h"

#include "GameFramework/Base/AI/Schedule/ScheduleCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"
#include "Engine/Utility/Xml/XmlNameCheck.h"

#include "GameFramework/Base/AI/Schedule/ScheduleActivity.h"
#include "GameFramework/Base/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "ScheduleCatalog" );

    namespace
    {
        struct ScheduleCatalogInternal
        {
            static constexpr const utf8* kArrCalendarAttribute[]    = { "days", "seasons", "weathers" };
            static constexpr const utf8* kArrPlaceAttribute[]       = { "id", "area", "position", "radius" };
            static constexpr const utf8* kArrSpotAttribute[]        = { "id", "kind", "area", "position", "capacity" };
            static constexpr const utf8* kArrInterruptAttribute[]   = { "id", "priority", "timeout" };
            static constexpr const utf8* kArrAppointmentAttribute[] = { "id", "place", "start", "end", "wait", "priority" };
            static constexpr const utf8* kArrArchetypeAttribute[]   = { "id", "parent" };
            static constexpr const utf8* kArrNpcAttribute[]         = { "id", "archetype", "home", "speed", "idle", "tags" };
            static constexpr const utf8* kArrRoutineAttribute[]     = { "id", "priority" };
            static constexpr const utf8* kArrEventAttribute[]       = { "id", "priority", "npcs", "archetypes" };
            static constexpr const utf8* kArrBlockAttribute[]       = { "start", "end", "activity", "place", "objectKind", "appointment", "animation", "radius", "every" };
            /** @brief 원소를 읽는 바퀴입니다 — 가리켜지는 것을 먼저 읽어 파일 안 순서와 상관없이 이름을 검사한다. */
            struct ElementPass
            {
                const utf8* _pName;
                int32       _pass;
            };
            static constexpr int32       kPassCount        = 4;
            static constexpr ElementPass kArrElementPass[] = {
                {   "Calendar", 0},
                {      "Place", 0},
                {       "Spot", 0},
                {  "Interrupt", 0},
                {"Appointment", 1},
                {  "Archetype", 2},
                {        "Npc", 3},
                {      "Event", 3},
            };

            /** @brief 원소의 바퀴입니다. 모르는 원소면 −1 입니다. */
            static int32 findElementPass( const utf8* pName )
            {
                for ( const ElementPass& entry : kArrElementPass )
                {
                    if ( StringUtil::equals( pName, entry._pName, true ) )
                        return entry._pass;
                }
                return -1;
            }

            /** @brief 표에 없는 속성마다 경고합니다. @p bCondition 이면 조건 속성도 받습니다. */
            template <size_t Count>
            static void warnUnknownAttributes( const XmlNode& node, const utf8* const ( &arrKnown )[Count], bool bCondition, string_view sourceName )
            {
                (void)XmlNameCheck::reportUnknownAttributes( node, arrKnown, sourceName, LogLevel::Warning, // 경고만 하고 읽기를 잇는다
                                                             bCondition ? &ScheduleCondition::isConditionAttribute : nullptr );
            }

            static int32 readClock( const XmlNode& node, const utf8* pName, int32 fallback, string_view sourceName, string_view ownerName, bool& inoutbValid )
            {
                const utf8* pText = node.findAttribute( pName );
                if ( pText == nullptr )
                    return fallback;
                int32 minutes = 0;
                if ( ScheduleCatalog::parseClockMinutes( pText, minutes ) )
                    return minutes;
                SW_LOG_WARNING( "%#: '%#' has an invalid %# time '%#' (expected H:MM in 0:00..24:00)", sourceName, ownerName, pName, pText );
                inoutbValid = false;
                return fallback;
            }

            static bool contains( const vector<hashed_string>& listName, const hashed_string& name )
            {
                for ( const hashed_string& entry : listName )
                {
                    if ( entry == name )
                        return true;
                }
                return false;
            }

            static void parseTagList( string_view text, vector<TagID>& outListTag )
            {
                outListTag.clear();
                GameDataXml::forEachToken( text, ",;| ", [&]( string_view token )
                { outListTag.push_back( TagID::request( token ) ); } );
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
    {
    }

    bool ScheduleCatalog::loadFromResource( string_view path )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, "Schedules", root, sourceName ) && loadRoot( root, sourceName ) > 0;
    }

    bool ScheduleCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, "Schedules", root ) && loadRoot( root, sourceName ) > 0;
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
        _vocabulary = ScheduleConditionVocabulary{};
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
            const string_view minuteText = trimmed.substr( colon + 1 );
            if ( minuteText.size() != 2 || StringUtil::parseInt( trimmed.substr( 0, colon ), hours ) == false || StringUtil::parseInt( minuteText, minutes ) == false )
                return false;
        }
        const bool  bMinuteValid = 0 <= minutes && minutes < 60;
        const int32 total        = hours * 60 + minutes;
        if ( bMinuteValid == false || hours < 0 || total > kScheduleMinutesPerDay )
            return false;
        outMinutes = total;
        return true;
    }

    const ScheduleActivityRegistry& ScheduleCatalog::getActivityRegistry() const
    {
        return _pActivityRegistry != nullptr ? *_pActivityRegistry : ScheduleActivityRegistry::getBuiltin();
    }

    bool ScheduleCatalog::isNpcOfArchetype( const ScheduleNpcDef& npc, const hashed_string& archetypeId ) const
    {
        hashed_string current = npc._archetype;
        // 순환은 읽을 때 끊었지만 묶음 수만큼만 걷는다.
        for ( size_t step = 0; step <= _archetypeCatalog.getCount() && current.empty() == false; ++step )
        {
            if ( current == archetypeId )
                return true;
            const ScheduleArchetypeDef* pArchetype = findArchetype( current );
            current                                = pArchetype != nullptr ? pArchetype->_parent : hashed_string{};
        }
        return false;
    }

    uint32 ScheduleCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        using Internal = ScheduleCatalogInternal;
        // 바퀴마다 그 바퀴의 원소만 읽는다 — 달력 · 장소 · 자리 · 끼어들기 → 약속 → 묶음 → NPC · 행사.
        uint32 loadedCount = 0;
        for ( int32 pass = 0; pass < Internal::kPassCount; ++pass )
        {
            for ( XmlNode node = root.findChild(); node; node = node.findNextSibling() )
            {
                const utf8* pName       = node.getName();
                const int32 elementPass = Internal::findElementPass( pName );
                if ( elementPass < 0 && pass == 0 )
                    SW_LOG_WARNING( "%#: unknown element <%#>", sourceName, pName );
                if ( elementPass == pass )
                    loadedCount += loadElement( node, sourceName );
            }
        }
        resolveReferences( sourceName );
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Npc> entries", sourceName );
        return loadedCount;
    }

    uint32 ScheduleCatalog::loadElement( const XmlNode& node, string_view sourceName )
    {
        using Internal    = ScheduleCatalogInternal;
        const utf8* pName = node.getName();
        if ( StringUtil::equals( pName, "Calendar", true ) )
        {
            Internal::warnUnknownAttributes( node, Internal::kArrCalendarAttribute, false, sourceName );
            ScheduleCondition::parseNameList( node.getAttributeText( "days" ), _vocabulary._listWeekday );
            ScheduleCondition::parseNameList( node.getAttributeText( "seasons" ), _vocabulary._listSeason );
            ScheduleCondition::parseNameList( node.getAttributeText( "weathers" ), _vocabulary._listWeather );
            return 0;
        }
        const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
        if ( pId == nullptr )
            return 0;
        if ( StringUtil::equals( pName, "Place", true ) )
        {
            Internal::warnUnknownAttributes( node, Internal::kArrPlaceAttribute, false, sourceName );
            SchedulePlaceDef place;
            place._id       = hashed_string( pId );
            place._area     = hashed_string( node.getAttributeText( "area" ) );
            place._position = GameDataXml::parseFloat3( node.getAttributeText( "position" ), place._position );
            place._radius   = MathUtil::max( 0.0f, node.getAttributeFloat( "radius", place._radius ) );
            (void)_placeCatalog.add( place );
        }
        else if ( StringUtil::equals( pName, "Spot", true ) )
        {
            Internal::warnUnknownAttributes( node, Internal::kArrSpotAttribute, false, sourceName );
            ScheduleSpotDef spot;
            spot._id       = hashed_string( pId );
            spot._kind     = hashed_string( node.getAttributeText( "kind" ) );
            spot._area     = hashed_string( node.getAttributeText( "area" ) );
            spot._position = GameDataXml::parseFloat3( node.getAttributeText( "position" ), spot._position );
            spot._capacity = MathUtil::max( 1, node.getAttributeInt( "capacity", spot._capacity ) );
            if ( spot._kind.empty() )
                SW_LOG_WARNING( "%#: spot '%#' has no kind - skipped", sourceName, pId );
            else
                (void)_spotCatalog.add( spot );
        }
        else if ( StringUtil::equals( pName, "Interrupt", true ) )
        {
            Internal::warnUnknownAttributes( node, Internal::kArrInterruptAttribute, false, sourceName );
            ScheduleInterruptDef interrupt;
            interrupt._id             = hashed_string( pId );
            interrupt._priority       = node.getAttributeInt( "priority", interrupt._priority );
            interrupt._timeoutMinutes = MathUtil::max( 0, node.getAttributeInt( "timeout", interrupt._timeoutMinutes ) );
            (void)_interruptCatalog.add( interrupt );
        }
        else if ( StringUtil::equals( pName, "Appointment", true ) )
        {
            Internal::warnUnknownAttributes( node, Internal::kArrAppointmentAttribute, true, sourceName );
            ScheduleAppointmentDef appointment;
            appointment._id          = hashed_string( pId );
            appointment._place       = hashed_string( node.getAttributeText( "place" ) );
            bool bValid              = true;
            appointment._startMinute = Internal::readClock( node, "start", -1, sourceName, pId, bValid );
            appointment._endMinute   = Internal::readClock( node, "end", -1, sourceName, pId, bValid );
            appointment._waitMinutes = MathUtil::max( 0, node.getAttributeInt( "wait", appointment._waitMinutes ) );
            appointment._priority    = node.getAttributeInt( "priority", appointment._priority );
            appointment._condition.readFromNode( node, _vocabulary, sourceName, pId );
            if ( findPlace( appointment._place ) == nullptr )
            {
                SW_LOG_WARNING( "%#: appointment '%#' has an unknown place '%#' - skipped", sourceName, pId, appointment._place.c_str() );
                bValid = false;
            }
            if ( bValid && ( appointment._startMinute < 0 || appointment._endMinute <= appointment._startMinute ) )
            {
                SW_LOG_WARNING( "%#: appointment '%#' needs start < end - skipped", sourceName, pId );
                bValid = false;
            }
            if ( bValid )
                (void)_appointmentCatalog.add( appointment );
        }
        else if ( StringUtil::equals( pName, "Archetype", true ) )
        {
            Internal::warnUnknownAttributes( node, Internal::kArrArchetypeAttribute, false, sourceName );
            ScheduleArchetypeDef archetype;
            archetype._id     = hashed_string( pId );
            archetype._parent = hashed_string( node.getAttributeText( "parent" ) );
            for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
            {
                if ( StringUtil::equals( child.getName(), "Routine", true ) == false )
                {
                    SW_LOG_WARNING( "%#: archetype '%#' has unknown element <%#>", sourceName, pId, child.getName() );
                    continue;
                }
                ScheduleRoutineDef routine;
                readRoutine( child, sourceName, pId, routine );
                archetype._listRoutine.push_back( routine );
            }
            (void)_archetypeCatalog.add( archetype );
        }
        else if ( StringUtil::equals( pName, "Npc", true ) )
        {
            Internal::warnUnknownAttributes( node, Internal::kArrNpcAttribute, false, sourceName );
            ScheduleNpcDef npc;
            npc._id             = hashed_string( pId );
            npc._archetype      = hashed_string( node.getAttributeText( "archetype" ) );
            npc._home           = hashed_string( node.getAttributeText( "home" ) );
            npc._idleActivity   = hashed_string( node.findAttribute( "idle" ) != nullptr ? node.getAttributeText( "idle" ) : string_view( "StayHome" ) );
            npc._unitsPerMinute = MathUtil::max( 0.01f, node.getAttributeFloat( "speed", npc._unitsPerMinute ) );
            Internal::parseTagList( node.getAttributeText( "tags" ), npc._listTag );
            if ( npc._home.empty() == false && findPlace( npc._home ) == nullptr )
                SW_LOG_WARNING( "%#: npc '%#' has an unknown home '%#'", sourceName, pId, npc._home.c_str() );
            const ScheduleActivityDef* pIdle = getActivityRegistry().findActivity( npc._idleActivity );
            if ( pIdle == nullptr || pIdle->_target != ScheduleActivityTarget::Home )
            {
                SW_LOG_WARNING( "%#: npc '%#' has idle activity '%#' that is not a home activity - StayHome used", sourceName, pId, npc._idleActivity.c_str() );
                npc._idleActivity = hashed_string( "StayHome" );
            }
            for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
            {
                if ( StringUtil::equals( child.getName(), "Routine", true ) == false )
                {
                    SW_LOG_WARNING( "%#: npc '%#' has unknown element <%#>", sourceName, pId, child.getName() );
                    continue;
                }
                ScheduleRoutineDef routine;
                readRoutine( child, sourceName, pId, routine );
                npc._listRoutine.push_back( routine );
            }
            (void)_npcCatalog.add( npc );
            return 1;
        }
        else
        {
            Internal::warnUnknownAttributes( node, Internal::kArrEventAttribute, true, sourceName );
            ScheduleEventDef event;
            event._id       = hashed_string( pId );
            event._priority = node.getAttributeInt( "priority", event._priority );
            ScheduleCondition::parseNameList( node.getAttributeText( "npcs" ), event._listNpc );
            ScheduleCondition::parseNameList( node.getAttributeText( "archetypes" ), event._listArchetype );
            event._condition.readFromNode( node, _vocabulary, sourceName, pId );
            for ( const hashed_string& archetypeId : event._listArchetype )
            {
                if ( findArchetype( archetypeId ) == nullptr )
                    SW_LOG_WARNING( "%#: event '%#' has an unknown archetype '%#'", sourceName, pId, archetypeId.c_str() );
            }
            for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
            {
                if ( StringUtil::equals( child.getName(), "Block", true ) == false )
                {
                    SW_LOG_WARNING( "%#: event '%#' has unknown element <%#>", sourceName, pId, child.getName() );
                    continue;
                }
                ScheduleBlockDef block;
                if ( readBlock( child, sourceName, pId, false, block ) )
                    event._listBlock.push_back( block );
            }
            warnOverlappingBlocks( event._listBlock, sourceName, pId );
            (void)_eventCatalog.add( event );
        }
        return 0;
    }

    void ScheduleCatalog::readRoutine( const XmlNode& node, string_view sourceName, string_view ownerName, ScheduleRoutineDef& outRoutine ) const
    {
        using Internal = ScheduleCatalogInternal;
        Internal::warnUnknownAttributes( node, Internal::kArrRoutineAttribute, true, sourceName );
        outRoutine._id       = hashed_string( node.getAttributeText( "id" ) );
        outRoutine._priority = node.getAttributeInt( "priority", outRoutine._priority );
        if ( outRoutine._id.empty() )
            SW_LOG_WARNING( "%#: '%#' has a <Routine> without an id", sourceName, ownerName );
        outRoutine._condition.readFromNode( node, _vocabulary, sourceName, ownerName );
        for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Block", true ) == false )
            {
                SW_LOG_WARNING( "%#: routine '%#' of '%#' has unknown element <%#>", sourceName, outRoutine._id.c_str(), ownerName, child.getName() );
                continue;
            }
            ScheduleBlockDef block;
            if ( readBlock( child, sourceName, ownerName, true, block ) )
                outRoutine._listBlock.push_back( block );
        }
        warnOverlappingBlocks( outRoutine._listBlock, sourceName, ownerName );
    }

    bool ScheduleCatalog::readBlock( const XmlNode& node, string_view sourceName, string_view ownerName, bool bInRoutine, ScheduleBlockDef& outBlock ) const
    {
        using Internal = ScheduleCatalogInternal;
        Internal::warnUnknownAttributes( node, Internal::kArrBlockAttribute, true, sourceName );
        outBlock._activity      = hashed_string( node.getAttributeText( "activity" ) );
        outBlock._place         = hashed_string( node.getAttributeText( "place" ) );
        outBlock._objectKind    = hashed_string( node.getAttributeText( "objectKind" ) );
        outBlock._appointment   = hashed_string( node.getAttributeText( "appointment" ) );
        outBlock._animation     = hashed_string( node.getAttributeText( "animation" ) );
        outBlock._radius        = node.getAttributeFloat( "radius", outBlock._radius );
        outBlock._wanderMinutes = MathUtil::max( 1, node.getAttributeInt( "every", outBlock._wanderMinutes ) );
        outBlock._condition.readFromNode( node, _vocabulary, sourceName, ownerName );

        const ScheduleActivityDef* pActivity = getActivityRegistry().findActivity( outBlock._activity );
        if ( pActivity == nullptr )
        {
            SW_LOG_WARNING( "%#: '%#' has a block with an unknown activity '%#' - skipped", sourceName, ownerName, outBlock._activity.c_str() );
            return false;
        }
        if ( outBlock._place.empty() == false && findPlace( outBlock._place ) == nullptr )
        {
            SW_LOG_WARNING( "%#: '%#' has a block with an unknown place '%#' - skipped", sourceName, ownerName, outBlock._place.c_str() );
            return false;
        }

        bool bValid = true;
        switch ( pActivity->_target )
        {
            case ScheduleActivityTarget::Place:
            case ScheduleActivityTarget::Wander:
            {
                if ( outBlock._place.empty() )
                {
                    SW_LOG_WARNING( "%#: '%#' has a %# block without a place - skipped", sourceName, ownerName, outBlock._activity.c_str() );
                    return false;
                }
                break;
            }
            case ScheduleActivityTarget::SmartObject:
            {
                if ( outBlock._objectKind.empty() )
                {
                    SW_LOG_WARNING( "%#: '%#' has a %# block without an objectKind - skipped", sourceName, ownerName, outBlock._activity.c_str() );
                    return false;
                }
                break;
            }
            case ScheduleActivityTarget::Appointment:
            {
                const ScheduleAppointmentDef* pAppointment = findAppointment( outBlock._appointment );
                if ( bInRoutine == false || pAppointment == nullptr )
                {
                    SW_LOG_WARNING( "%#: '%#' has a %# block with an unknown appointment '%#' (or outside a routine) - skipped", sourceName, ownerName,
                                    outBlock._activity.c_str(), outBlock._appointment.c_str() );
                    return false;
                }
                if ( node.findAttribute( "start" ) != nullptr || node.findAttribute( "end" ) != nullptr )
                    SW_LOG_WARNING( "%#: '%#' has a %# block with its own start/end - the appointment's time is used", sourceName, ownerName, outBlock._activity.c_str() );
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
        outBlock._startMinute = Internal::readClock( node, "start", -1, sourceName, ownerName, bValid );
        outBlock._endMinute   = Internal::readClock( node, "end", -1, sourceName, ownerName, bValid );
        if ( bValid && ( outBlock._startMinute < 0 || outBlock._endMinute <= outBlock._startMinute ) )
        {
            SW_LOG_WARNING( "%#: '%#' has a %# block that needs start < end - skipped", sourceName, ownerName, outBlock._activity.c_str() );
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
        std::stable_sort( listOrder.begin(), listOrder.end(), ScheduleCatalogInternal::BlockStartLess{ &listBlock } );
        for ( size_t orderIndex = 1; orderIndex < listOrder.size(); ++orderIndex )
        {
            const ScheduleBlockDef& previous = listBlock[static_cast<size_t>( listOrder[orderIndex - 1] )];
            const ScheduleBlockDef& current  = listBlock[static_cast<size_t>( listOrder[orderIndex] )];
            if ( current._startMinute < previous._endMinute )
                SW_LOG_WARNING( "%#: '%#' has overlapping blocks (%# and %#)", sourceName, ownerName, previous._activity.c_str(), current._activity.c_str() );
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
                {
                    SW_LOG_WARNING( "%#: archetype '%#' has an unknown parent '%#'", sourceName, archetype._id.c_str(), current.c_str() );
                    break;
                }
                current = pParent->_parent;
            }
            const bool bCycle = current.empty() == false && ( current == archetype._id || step > archetypeCount );
            if ( bCycle )
            {
                SW_LOG_WARNING( "%#: archetype '%#' is part of a parent cycle - parent dropped", sourceName, archetype._id.c_str() );
                archetype._parent = hashed_string{};
                (void)_archetypeCatalog.add( archetype );
            }
        }

        // NPC 루틴 — 자기 것 다음에 묶음 사슬의 것(같은 id 는 가까운 쪽이 덮는다). 약속 참가자를 모은다.
        vector<vector<hashed_string>> listAttendeePerAppointment( _appointmentCatalog.getCount() );
        for ( size_t npcIndex = 0; npcIndex < _npcCatalog.getCount(); ++npcIndex )
        {
            ScheduleNpcDef npc = _npcCatalog.getAt( npcIndex );
            if ( npc._archetype.empty() == false && findArchetype( npc._archetype ) == nullptr )
                SW_LOG_WARNING( "%#: npc '%#' has an unknown archetype '%#'", sourceName, npc._id.c_str(), npc._archetype.c_str() );
            vector<hashed_string> listRoutineId;
            for ( const ScheduleRoutineDef& routine : npc._listRoutine )
                listRoutineId.push_back( routine._id );
            hashed_string current = npc._archetype;
            for ( size_t step = 0; step <= archetypeCount && current.empty() == false; ++step )
            {
                const ScheduleArchetypeDef* pArchetype = findArchetype( current );
                if ( pArchetype == nullptr )
                    break;
                for ( const ScheduleRoutineDef& routine : pArchetype->_listRoutine )
                {
                    if ( ScheduleCatalogInternal::contains( listRoutineId, routine._id ) )
                        continue;
                    listRoutineId.push_back( routine._id );
                    npc._listRoutine.push_back( routine );
                }
                current = pArchetype->_parent;
            }
            for ( const ScheduleRoutineDef& routine : npc._listRoutine )
            {
                for ( const ScheduleBlockDef& block : routine._listBlock )
                {
                    const int32 appointmentIndex = _appointmentCatalog.findIndex( block._appointment );
                    if ( block._appointment.empty() || appointmentIndex < 0 )
                        continue;
                    vector<hashed_string>& listAttendee = listAttendeePerAppointment[static_cast<size_t>( appointmentIndex )];
                    if ( ScheduleCatalogInternal::contains( listAttendee, npc._id ) == false )
                        listAttendee.push_back( npc._id );
                }
            }
            (void)_npcCatalog.add( npc );
        }
        for ( size_t appointmentIndex = 0; appointmentIndex < _appointmentCatalog.getCount(); ++appointmentIndex )
        {
            ScheduleAppointmentDef appointment = _appointmentCatalog.getAt( appointmentIndex );
            appointment._listAttendee          = listAttendeePerAppointment[appointmentIndex];
            if ( appointment._listAttendee.size() < 2 )
                SW_LOG_WARNING( "%#: appointment '%#' is referenced by %# npc(s) - a meeting needs two", sourceName, appointment._id.c_str(),
                                static_cast<uint32>( appointment._listAttendee.size() ) );
            (void)_appointmentCatalog.add( appointment );
        }
        for ( const ScheduleEventDef& event : _eventCatalog.getAll() )
        {
            for ( const hashed_string& npcId : event._listNpc )
            {
                if ( findNpc( npcId ) == nullptr )
                    SW_LOG_WARNING( "%#: event '%#' has an unknown npc '%#'", sourceName, event._id.c_str(), npcId.c_str() );
            }
        }
    }
} // namespace sw
