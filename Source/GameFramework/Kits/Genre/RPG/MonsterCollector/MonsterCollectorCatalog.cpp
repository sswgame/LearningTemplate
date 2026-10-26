#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterCollectorCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct MonsterCollectorCatalogInternal
        {
            static constexpr const utf8* kArrStatName[kMonsterStatCount] = { "Hp", "Attack", "Defense", "SpecialAttack", "SpecialDefense", "Speed" };

            /**
             * @brief "39 52 43 60 50 65" 같은 정수 여섯 개(HP 공격 방어 특공 특방 스피드 순)를 읽습니다.
             * @details 적었으면 여섯 개가 모두 @p minValue 이상의 정수여야 합니다. 모자라거나 깨진 값은 오류이고 표를 바꾸지 않습니다.
             *          @p bRequired 가 아니면 적지 않아도 되고 그때는 Def 의 초기값입니다.
             */
            [[nodiscard]] static bool readSixInts( const XMLNode& node, const utf8* pName, bool bRequired, int32 minValue, string_view sourceName,
                                                   int32 ( &outArrValue )[kMonsterStatCount] )
            {
                const string_view text = node.getAttributeText( pName );
                if ( text.empty() )
                {
                    if ( bRequired )
                        SW_LOG_ERROR( "%#: <%#> needs %#=\"HP Attack Defense SpecialAttack SpecialDefense Speed\"", sourceName, node.getName(), pName );
                    return bRequired == false;
                }
                int32 arrValue[kMonsterStatCount] = {};
                int32 count                       = 0;
                bool  bValid                      = true;
                StringUtil::forEachToken( text, ",; \t", [&]( string_view token )
                {
                    int32      value  = 0;
                    const bool bRoom  = count < kMonsterStatCount;
                    const bool bValue = StringUtil::parseInt( token, value ) && value >= minValue;
                    if ( bRoom == false || bValue == false )
                        bValid = false;
                    else
                        arrValue[count] = value;
                    ++count;
                } );
                if ( bValid == false || count != kMonsterStatCount )
                {
                    SW_LOG_ERROR( "%#: <%# %#=\"%#\"> needs six integers of %# or more", sourceName, node.getName(), pName, text, minValue );
                    return false;
                }
                std::copy( arrValue, arrValue + kMonsterStatCount, outArrValue );
                return true;
            }

            /**
             * @brief 지역 · 시간대에 맞는 조우표의 가중치 있는 슬롯을 차례로 @p visit 에 넘깁니다. @p visit 이 true 를 반환하면 멈춥니다.
             * @details 후보를 목록에 모으지 않으므로 조우마다 할당이 생기지 않는다. 호출자가 같은 순서로 두 번 훑는다(합을 구하고, 고른다).
             */
            template <typename TVisit>
            static void forEachCandidate( const vector<MonsterEncounterDef>& listTable, const hashed_string& area, const hashed_string& timeOfDay, TVisit&& visit )
            {
                for ( const MonsterEncounterDef& table : listTable )
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
            }
        };
    } // namespace

    SW_LOG_CALLER( "MonsterCollectorCatalog" );

    bool MonsterSpeciesDef::hasType( const hashed_string& type ) const
    {
        return VectorUtil::contains( _listType, type );
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
            if ( StringUtil::equals( text, string_view( getEnumName( status ) ) ) )
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

    bool MonsterCollectorCatalog::rollEncounter( const hashed_string& area, const hashed_string& timeOfDay, GameRandom& random, hashed_string& outSpeciesID,
                                                 int32& outLevel ) const
    {
        using Internal    = MonsterCollectorCatalogInternal;
        int32 totalWeight = 0;
        Internal::forEachCandidate( _encounterCatalog.getAll(), area, timeOfDay, [&totalWeight]( const MonsterEncounterSlot& slot )
        {
            totalWeight += slot._weight;
            return false;
        } );
        if ( totalWeight <= 0 )
            return false;

        int32                       pick    = random.nextInt( 0, totalWeight - 1 );
        const MonsterEncounterSlot* pPicked = nullptr;
        Internal::forEachCandidate( _encounterCatalog.getAll(), area, timeOfDay, [&pick, &pPicked]( const MonsterEncounterSlot& slot )
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
        outSpeciesID = pPicked->_speciesID;
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
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) )
                ++loadedCount;
        }

        // 다른 정의를 가리키는 id 는 모두 읽은 뒤에 확인한다(앞에 적힌 종이 뒤에 적힌 종으로 진화할 수 있다).
        for ( const MonsterSpeciesDef& species : _speciesCatalog.getAll() )
        {
            for ( const MonsterLearnEntry& learn : species._listLearn )
            {
                if ( _moveCatalog.find( learn._moveID ) == nullptr )
                    SW_LOG_ERROR( "%#: species '%#' learns unknown move '%#'", sourceName, species._id.c_str(), learn._moveID.c_str() );
            }
            for ( const MonsterEvolutionDef& evolution : species._listEvolution )
            {
                if ( _speciesCatalog.find( evolution._targetID ) == nullptr )
                    SW_LOG_ERROR( "%#: species '%#' evolves into unknown '%#'", sourceName, species._id.c_str(), evolution._targetID.c_str() );
            }
        }
        for ( const MonsterEncounterDef& table : _encounterCatalog.getAll() )
        {
            for ( const MonsterEncounterSlot& slot : table._listSlot )
            {
                if ( _speciesCatalog.find( slot._speciesID ) == nullptr )
                    SW_LOG_ERROR( "%#: encounter '%#' names unknown species '%#'", sourceName, table._id.c_str(), slot._speciesID.c_str() );
            }
        }
        return loadedCount;
    }

    bool MonsterCollectorCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( GameDataXML::isElement( name, "StatusImmunity" ) )
        {
            StatusImmunity    immunity;
            const string_view status = node.getAttributeText( "status" );
            if ( parseStatus( status, immunity._status ) == false )
            {
                SW_LOG_ERROR( "%#: <StatusImmunity> needs a known status, got '%#'", sourceName, status );
                return false;
            }
            GameDataXML::appendNameList( node.getAttributeText( "types" ), ",; \t", immunity._listType );
            _listStatusImmunity.push_back( immunity );
            return true;
        }
        if ( GameDataXML::isElement( name, "Move" ) )
            return loadMove( node, sourceName );
        if ( GameDataXML::isElement( name, "Species" ) )
            return loadSpecies( node, sourceName );
        if ( GameDataXML::isElement( name, "Nature" ) )
            return GameDataXML::readInto( node, sourceName, _natureCatalog ); // up 과 down 이 같으면(적지 않으면 둘 다 Attack) 보정이 없는 성격이다
        if ( GameDataXML::isElement( name, "Weather" ) )
        {
            MonsterWeatherDef def;
            if ( GameDataXML::readFields( node, def, sourceName, { "immune" } ) == false )
                return false;
            GameDataXML::appendNameList( node.getAttributeText( "immune" ), ",; \t", def._listChipImmuneType );
            (void)_weatherCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
            return true;
        }
        if ( GameDataXML::isElement( name, "Encounter" ) )
            return loadEncounter( node, sourceName );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool MonsterCollectorCatalog::loadMove( const XMLNode& node, string_view sourceName )
    {
        MonsterMoveDef def;
        if ( GameDataXML::readFields( node, def, sourceName ) == false )
            return false;
        // 상태이상을 거는 기술은 확률을 적는다(변화 기술도 100 을 적는다) — 분류로 기본값을 고르지 않는다.
        const bool bStatusWithoutChance = def._status != MonsterStatus::None && def._statusChance == 0;
        if ( bStatusWithoutChance )
        {
            SW_LOG_ERROR( "%#: move '%#' sets status %# but needs statusChance=\"1..100\"", sourceName, def._id.c_str(), getEnumName( def._status ) );
            return false;
        }
        if ( def._statStages != 0 && def._changedStat == MonsterStat::Hp )
        {
            SW_LOG_ERROR( "%#: move '%#' cannot change HP by stages", sourceName, def._id.c_str() );
            return false;
        }
        (void)_moveCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    bool MonsterCollectorCatalog::loadSpecies( const XMLNode& node, string_view sourceName )
    {
        using Internal = MonsterCollectorCatalogInternal;
        MonsterSpeciesDef def;
        bool              bValid = GameDataXML::readFields( node, def, sourceName, { "types", "stats", "evYield" } );
        GameDataXML::appendNameList( node.getAttributeText( "types" ), ",; \t", def._listType );
        if ( def._listType.empty() || def._listType.size() > 2 )
        {
            SW_LOG_ERROR( "%#: species '%#' has %# types - needs 1 or 2", sourceName, def._id.c_str(), static_cast<uint32>( def._listType.size() ) );
            bValid = false;
        }
        bValid = Internal::readSixInts( node, "stats", true, 1, sourceName, def._arrBaseStat ) && bValid;
        bValid = Internal::readSixInts( node, "evYield", false, 0, sourceName, def._arrEvYield ) && bValid;
        for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Learn", true ) )
                bValid = GameDataXML::readInto( child, sourceName, def._listLearn ) && bValid;
            else if ( StringUtil::equals( child.getName(), "Evolve", true ) )
                bValid = GameDataXML::readInto( child, sourceName, def._listEvolution ) && bValid;
            else
            {
                SW_LOG_ERROR( "%#: species '%#' has an unknown child <%#>", sourceName, def._id.c_str(), child.getName() );
                bValid = false;
            }
        }
        if ( bValid == false )
            return false;
        // 레벨 순서가 흐트러져 있어도 "그 레벨까지 배운 마지막 네 개"가 맞게 나오도록 정렬한다. 같은 레벨은 적힌 순서를 지킨다.
        std::stable_sort( def._listLearn.begin(), def._listLearn.end(), []( const MonsterLearnEntry& lhs, const MonsterLearnEntry& rhs )
        { return lhs._level < rhs._level; } );
        (void)_speciesCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    bool MonsterCollectorCatalog::loadEncounter( const XMLNode& node, string_view sourceName )
    {
        MonsterEncounterDef def;
        bool                bValid = GameDataXML::readFields( node, def, sourceName, { "time" } );
        GameDataXML::appendNameList( node.getAttributeText( "time" ), ",; \t", def._listTime );
        for ( XMLNode child = node.findChild( "Slot" ); child; child = child.findNextSibling( "Slot" ) )
        {
            bValid = GameDataXML::readInto( child, sourceName, def._listSlot ) && bValid;
        }
        if ( bValid )
            (void)_encounterCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }
} // namespace sw
