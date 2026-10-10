#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterCollectorCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"

namespace sw
{
    namespace
    {
        struct MonsterCollectorCatalogInternal
        {
            static constexpr const utf8* kArrStatName[kMonsterStatCount] = { "Hp", "Attack", "Defense", "SpecialAttack", "SpecialDefense", "Speed" };

            /** @brief "39 52 43 60 50 65" 같은 정수 여섯입니다. 빠진 칸은 그대로 둡니다. */
            static void parseSixInts( string_view text, int32 ( &inoutArrValue )[kMonsterStatCount], int32 minValue )
            {
                int32 slot = 0;
                GameDataXML::forEachToken( text, ",; \t", [&]( string_view token )
                {
                    if ( slot >= kMonsterStatCount )
                        return;
                    int32 value = inoutArrValue[slot];
                    if ( StringUtil::parseInt( token, value ) )
                        inoutArrValue[slot] = MathUtil::max( minValue, value );
                    ++slot;
                } );
            }

            static void parseNameList( string_view text, vector<hashed_string>& outListName )
            {
                GameDataXML::forEachToken( text, ",; \t", [&]( string_view token )
                { outListName.push_back( hashed_string( token ) ); } );
            }

            [[nodiscard]] static bool parseExpGroup( string_view text, MonsterExpGroup& outGroup )
            {
                if ( StringUtil::equals( text, string_view( "Fast" ), true ) )
                    outGroup = MonsterExpGroup::Fast;
                else if ( StringUtil::equals( text, string_view( "Medium" ), true ) )
                    outGroup = MonsterExpGroup::Medium;
                else if ( StringUtil::equals( text, string_view( "Slow" ), true ) )
                    outGroup = MonsterExpGroup::Slow;
                else
                    return false;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "MonsterCollectorCatalog" );

    const utf8* toString( MonsterStatus status )
    {
        switch ( status )
        {
            case MonsterStatus::None:
                return "None";
            case MonsterStatus::Poison:
                return "Poison";
            case MonsterStatus::Toxic:
                return "Toxic";
            case MonsterStatus::Burn:
                return "Burn";
            case MonsterStatus::Paralysis:
                return "Paralysis";
            case MonsterStatus::Sleep:
                return "Sleep";
            case MonsterStatus::Freeze:
                return "Freeze";
        }
        return "Unknown";
    }

    bool MonsterSpeciesDef::hasType( const hashed_string& type ) const
    {
        for ( const hashed_string& entry : _listType )
        {
            if ( entry == type )
                return true;
        }
        return false;
    }

    int32 MonsterNatureDef::computePercent( MonsterStat stat ) const
    {
        if ( stat == MonsterStat::Hp || _raised == _lowered )
            return 100;
        if ( stat == _raised )
            return 110;
        if ( stat == _lowered )
            return 90;
        return 100;
    }

    MonsterCollectorCatalog::MonsterCollectorCatalog()
        : _speciesCatalog{}
        , _moveCatalog{}
        , _natureCatalog{}
        , _weatherCatalog{}
        , _encounterCatalog{}
        , _listStatusImmunity{}
    {
    }

    void MonsterCollectorCatalog::clear()
    {
        _speciesCatalog.clear();
        _moveCatalog.clear();
        _natureCatalog.clear();
        _weatherCatalog.clear();
        _encounterCatalog.clear();
        _listStatusImmunity.clear();
    }

    bool MonsterCollectorCatalog::parseStat( string_view text, MonsterStat& outStat )
    {
        for ( int32 index = 0; index < kMonsterStatCount; ++index )
        {
            if ( StringUtil::equals( text, string_view( MonsterCollectorCatalogInternal::kArrStatName[index] ), true ) )
            {
                outStat = static_cast<MonsterStat>( index );
                return true;
            }
        }
        return false;
    }

    bool MonsterCollectorCatalog::parseStatus( string_view text, MonsterStatus& outStatus )
    {
        for ( uint8 index = static_cast<uint8>( MonsterStatus::Poison ); index <= static_cast<uint8>( MonsterStatus::Freeze ); ++index )
        {
            const MonsterStatus status = static_cast<MonsterStatus>( index );
            if ( StringUtil::equals( text, string_view( toString( status ) ), true ) )
            {
                outStatus = status;
                return true;
            }
        }
        return false;
    }

    int64 MonsterCollectorCatalog::computeTotalExp( MonsterExpGroup group, int32 level )
    {
        if ( level <= 1 )
            return 0;
        const int64 clampedLevel = MathUtil::min( level, kMaxLevel );
        const int64 cube         = clampedLevel * clampedLevel * clampedLevel;
        switch ( group )
        {
            case MonsterExpGroup::Fast:
                return cube * 4 / 5;
            case MonsterExpGroup::Medium:
                return cube;
            case MonsterExpGroup::Slow:
                return cube * 5 / 4;
        }
        return cube;
    }

    int32 MonsterCollectorCatalog::computeLevelForExp( MonsterExpGroup group, int64 exp )
    {
        int32 level = 1;
        while ( level < kMaxLevel && computeTotalExp( group, level + 1 ) <= exp )
        {
            ++level;
        }
        return level;
    }

    bool MonsterCollectorCatalog::rollEncounter( const hashed_string& area, const hashed_string& timeOfDay, GameRandom& random, hashed_string& outSpeciesId,
                                                 int32& outLevel ) const
    {
        // 후보를 목록에 모으지 않고 같은 순서로 두 번 훑는다(합 → 고르기). 조우마다 할당이 없다.
        const auto forEachCandidate = [&]( auto&& visit )
        {
            for ( const MonsterEncounterDef& table : _encounterCatalog.getAll() )
            {
                if ( table._area != area )
                    continue;
                bool bTimeMatches = table._listTime.empty();
                for ( const hashed_string& time : table._listTime )
                {
                    bTimeMatches = bTimeMatches || time == timeOfDay;
                }
                if ( bTimeMatches == false )
                    continue;
                for ( const MonsterEncounterSlot& slot : table._listSlot )
                {
                    if ( slot._weight > 0 && visit( slot ) )
                        return;
                }
            }
        };
        int32 totalWeight = 0;
        forEachCandidate( [&totalWeight]( const MonsterEncounterSlot& slot )
        {
            totalWeight += slot._weight;
            return false;
        } );
        if ( totalWeight <= 0 )
            return false;

        int32                       pick    = random.nextInt( 0, totalWeight - 1 );
        const MonsterEncounterSlot* pPicked = nullptr;
        forEachCandidate( [&pick, &pPicked]( const MonsterEncounterSlot& slot )
        {
            if ( pick < slot._weight )
            {
                pPicked = &slot;
                return true;
            }
            pick -= slot._weight;
            return false;
        } );
        if ( pPicked == nullptr )
            return false;
        outSpeciesId = pPicked->_speciesId;
        outLevel     = random.nextInt( pPicked->_minLevel, pPicked->_maxLevel );
        return true;
    }

    bool MonsterCollectorCatalog::isStatusImmune( MonsterStatus status, const vector<hashed_string>& listType ) const
    {
        for ( const StatusImmunity& immunity : _listStatusImmunity )
        {
            if ( immunity._status != status )
                continue;
            for ( const hashed_string& immuneType : immunity._listType )
            {
                for ( const hashed_string& type : listType )
                {
                    if ( type == immuneType )
                        return true;
                }
            }
        }
        return false;
    }

    uint32 MonsterCollectorCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const utf8* pName = node.getName();
            if ( StringUtil::equals( pName, "StatusImmunity", true ) )
            {
                StatusImmunity immunity;
                if ( parseStatus( node.getAttributeText( "status" ), immunity._status ) == false )
                {
                    SW_LOG_WARNING( "%#: StatusImmunity with an unknown status '%#' - skipped", sourceName, node.getAttributeText( "status" ) );
                    continue;
                }
                MonsterCollectorCatalogInternal::parseNameList( node.getAttributeText( "types" ), immunity._listType );
                _listStatusImmunity.push_back( immunity );
                ++loadedCount;
                continue;
            }

            const bool bMove      = StringUtil::equals( pName, "Move", true );
            const bool bSpecies   = StringUtil::equals( pName, "Species", true );
            const bool bNature    = StringUtil::equals( pName, "Nature", true );
            const bool bWeather   = StringUtil::equals( pName, "Weather", true );
            const bool bEncounter = StringUtil::equals( pName, "Encounter", true );
            if ( ( bMove || bSpecies || bNature || bWeather || bEncounter ) == false )
                continue;
            const utf8* pId = GameDataXML::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;

            if ( bMove )
            {
                loadMove( node, pId, sourceName );
            }
            else if ( bSpecies )
            {
                loadSpecies( node, pId, sourceName );
            }
            else if ( bNature )
            {
                // 칸이 없으면 기본값(Attack) 그대로 — up · down 이 같으면 무보정 성격이다. 틀린 이름은 그 성격을 빼고 알린다(반쪽만 보정되지 않게).
                MonsterNatureDef def;
                def._id                    = hashed_string( pId );
                const string_view upText   = node.getAttributeText( "up" );
                const string_view downText = node.getAttributeText( "down" );
                const bool        bUpOk    = upText.empty() || parseStat( upText, def._raised );
                const bool        bDownOk  = downText.empty() || parseStat( downText, def._lowered );
                if ( bUpOk == false || bDownOk == false )
                {
                    SW_LOG_WARNING( "%#: Nature '%#' names an unknown stat (up '%#', down '%#') - skipped", sourceName, pId, upText, downText );
                    continue;
                }
                (void)_natureCatalog.add( def );
            }
            else if ( bWeather )
            {
                MonsterWeatherDef def;
                def._id           = hashed_string( pId );
                def._boostedType  = hashed_string( node.getAttributeText( "boost" ) );
                def._weakenedType = hashed_string( node.getAttributeText( "weaken" ) );
                def._chipDivisor  = MathUtil::max( 0, node.getAttributeInt( "chip", 0 ) );
                def._turns        = node.getAttributeInt( "turns", def._turns );
                MonsterCollectorCatalogInternal::parseNameList( node.getAttributeText( "immune" ), def._listChipImmuneType );
                (void)_weatherCatalog.add( def );
            }
            else
            {
                loadEncounter( node, pId );
            }
            ++loadedCount;
        }

        // 다른 정의를 가리키는 id 는 다 읽은 뒤에 확인한다(앞에 적힌 종이 뒤의 종으로 진화할 수 있다).
        for ( const MonsterSpeciesDef& species : _speciesCatalog.getAll() )
        {
            for ( const MonsterLearnEntry& learn : species._listLearn )
            {
                if ( _moveCatalog.find( learn._moveId ) == nullptr )
                    SW_LOG_WARNING( "%#: species '%#' learns unknown move '%#'", sourceName, species._id.c_str(), learn._moveId.c_str() );
            }
            for ( const MonsterEvolutionDef& evolution : species._listEvolution )
            {
                if ( _speciesCatalog.find( evolution._targetId ) == nullptr )
                    SW_LOG_WARNING( "%#: species '%#' evolves into unknown '%#'", sourceName, species._id.c_str(), evolution._targetId.c_str() );
            }
        }
        for ( const MonsterEncounterDef& table : _encounterCatalog.getAll() )
        {
            for ( const MonsterEncounterSlot& slot : table._listSlot )
            {
                if ( _speciesCatalog.find( slot._speciesId ) == nullptr )
                    SW_LOG_WARNING( "%#: encounter '%#' names unknown species '%#'", sourceName, table._id.c_str(), slot._speciesId.c_str() );
            }
        }
        return loadedCount;
    }

    void MonsterCollectorCatalog::loadMove( const XMLNode& node, const utf8* pId, string_view sourceName )
    {
        MonsterMoveDef def;
        def._id           = hashed_string( pId );
        const utf8* pName = node.findAttribute( "name" );
        def._name         = pName != nullptr ? pName : pId;
        def._type         = hashed_string( node.getAttributeText( "type" ) );
        def._weatherId    = hashed_string( node.getAttributeText( "weather" ) );
        def._power        = MathUtil::max( 0, node.getAttributeInt( "power", def._power ) );
        def._accuracy     = MathUtil::clamp( node.getAttributeInt( "accuracy", def._accuracy ), 0, 100 );
        def._pp           = MathUtil::max( 1, node.getAttributeInt( "pp", def._pp ) );
        def._priority     = MathUtil::clamp( node.getAttributeInt( "priority", def._priority ), -7, 5 );
        def._critStage    = MathUtil::max( 0, node.getAttributeInt( "critStage", def._critStage ) );

        const string_view category = node.getAttributeText( "category" );
        if ( StringUtil::equals( category, string_view( "Special" ), true ) )
            def._category = MonsterMoveCategory::Special;
        else if ( StringUtil::equals( category, string_view( "Status" ), true ) )
            def._category = MonsterMoveCategory::Status;
        else if ( category.empty() == false && StringUtil::equals( category, string_view( "Physical" ), true ) == false )
            SW_LOG_WARNING( "%#: move '%#' has an unknown category '%#' - read as physical", sourceName, pId, category );

        const string_view status = node.getAttributeText( "status" );
        if ( status.empty() == false && parseStatus( status, def._status ) == false )
            SW_LOG_WARNING( "%#: move '%#' has an unknown status '%#' - ignored", sourceName, pId, status );
        const int32 defaultStatusChance = def._category == MonsterMoveCategory::Status ? 100 : 0;
        def._statusChance               = MathUtil::clamp( node.getAttributeInt( "statusChance", defaultStatusChance ), 0, 100 );

        const string_view stat = node.getAttributeText( "stat" );
        if ( stat.empty() == false )
        {
            if ( parseStat( stat, def._stat ) == false || def._stat == MonsterStat::Hp )
            {
                SW_LOG_WARNING( "%#: move '%#' changes an invalid stat '%#' - ignored", sourceName, pId, stat );
            }
            else
            {
                def._statStages = MathUtil::clamp( node.getAttributeInt( "stages", 0 ), -6, 6 );
                def._statChance = MathUtil::clamp( node.getAttributeInt( "statChance", 100 ), 0, 100 );
                def._statTarget = StringUtil::equals( node.getAttributeText( "target" ), string_view( "Self" ), true ) ? MonsterEffectTarget::Self
                                                                                                                       : MonsterEffectTarget::Foe;
            }
        }
        (void)_moveCatalog.add( def );
    }

    void MonsterCollectorCatalog::loadSpecies( const XMLNode& node, const utf8* pId, string_view sourceName )
    {
        MonsterSpeciesDef def;
        def._id           = hashed_string( pId );
        const utf8* pName = node.findAttribute( "name" );
        def._name         = pName != nullptr ? pName : pId;
        MonsterCollectorCatalogInternal::parseNameList( node.getAttributeText( "types" ), def._listType );
        if ( def._listType.empty() || def._listType.size() > 2 )
            SW_LOG_WARNING( "%#: species '%#' has %# types - expected 1 or 2", sourceName, pId, static_cast<uint32>( def._listType.size() ) );
        MonsterCollectorCatalogInternal::parseSixInts( node.getAttributeText( "stats" ), def._arrBaseStat, 1 );
        MonsterCollectorCatalogInternal::parseSixInts( node.getAttributeText( "evYield" ), def._arrEvYield, 0 );
        def._catchRate             = MathUtil::clamp( node.getAttributeInt( "catchRate", def._catchRate ), 1, 255 );
        def._baseExp               = MathUtil::max( 1, node.getAttributeInt( "baseExp", def._baseExp ) );
        const string_view expGroup = node.getAttributeText( "expGroup" );
        if ( expGroup.empty() == false && MonsterCollectorCatalogInternal::parseExpGroup( expGroup, def._expGroup ) == false )
            SW_LOG_WARNING( "%#: species '%#' has an unknown expGroup '%#' - read as Medium", sourceName, pId, expGroup );

        for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Learn", true ) )
            {
                MonsterLearnEntry entry;
                entry._moveId = hashed_string( child.getAttributeText( "move" ) );
                entry._level  = MathUtil::clamp( child.getAttributeInt( "level", 1 ), 1, kMaxLevel );
                if ( entry._moveId.empty() == false )
                    def._listLearn.push_back( entry );
            }
            else if ( StringUtil::equals( child.getName(), "Evolve", true ) )
            {
                MonsterEvolutionDef evolution;
                evolution._targetId   = hashed_string( child.getAttributeText( "to" ) );
                evolution._itemId     = hashed_string( child.getAttributeText( "item" ) );
                evolution._level      = MathUtil::max( 0, child.getAttributeInt( "level", 0 ) );
                evolution._friendship = MathUtil::max( 0, child.getAttributeInt( "friendship", 0 ) );
                if ( evolution._targetId.empty() == false )
                    def._listEvolution.push_back( evolution );
            }
        }
        // 레벨 순서가 흐트러져 있어도 "그 레벨까지 배운 마지막 넷" 이 맞게 — 같은 레벨은 적힌 순서를 지킨다.
        for ( size_t index = 1; index < def._listLearn.size(); ++index )
        {
            MonsterLearnEntry entry  = def._listLearn[index];
            size_t            cursor = index;
            while ( cursor > 0 && def._listLearn[cursor - 1]._level > entry._level )
            {
                def._listLearn[cursor] = def._listLearn[cursor - 1];
                --cursor;
            }
            def._listLearn[cursor] = entry;
        }
        (void)_speciesCatalog.add( def );
    }

    void MonsterCollectorCatalog::loadEncounter( const XMLNode& node, const utf8* pId )
    {
        MonsterEncounterDef def;
        def._id   = hashed_string( pId );
        def._area = hashed_string( node.getAttributeText( "area" ) );
        if ( def._area.empty() )
            def._area = def._id;
        MonsterCollectorCatalogInternal::parseNameList( node.getAttributeText( "time" ), def._listTime );
        for ( XMLNode child = node.findChild( "Slot" ); child; child = child.findNextSibling( "Slot" ) )
        {
            MonsterEncounterSlot slot;
            slot._speciesId = hashed_string( child.getAttributeText( "species" ) );
            slot._minLevel  = MathUtil::clamp( child.getAttributeInt( "min", slot._minLevel ), 1, kMaxLevel );
            slot._maxLevel  = MathUtil::clamp( child.getAttributeInt( "max", slot._minLevel ), slot._minLevel, kMaxLevel );
            slot._weight    = MathUtil::max( 0, child.getAttributeInt( "weight", slot._weight ) );
            if ( slot._speciesId.empty() == false )
                def._listSlot.push_back( slot );
        }
        (void)_encounterCatalog.add( def );
    }
} // namespace sw
