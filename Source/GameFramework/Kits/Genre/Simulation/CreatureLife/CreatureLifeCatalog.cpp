#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureLifeCatalog.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct CreatureLifeCatalogInternal
        {
            /** @brief 패턴 기호 하나의 뜻을 나타냅니다. */
            struct SymbolKey
            {
                hashed_string _object{};
                utf8          _symbol{ 0 };
            };

            /** @brief 오브젝트 이름을 반환합니다. `empty` 는 빈 셀(빈 id)입니다. */
            static hashed_string parseObject( string_view text )
            {
                const hashed_string object( text );
                return object == hashed_string( "empty" ) ? hashed_string{} : object;
            }

            /** @brief `<Key>` 와 `<Row>` 로 패턴을 읽습니다. 행 길이가 다르거나 모르는 기호이면 false 를 반환합니다. */
            [[nodiscard]] static bool parsePattern( const XMLNode& node, string_view sourceName, const utf8* pHabitatID, HabitatDef& outHabitat )
            {
                vector<SymbolKey> listKey;
                for ( XMLNode keyNode = node.findChild( "Key" ); keyNode; keyNode = keyNode.findNextSibling( "Key" ) )
                {
                    const string_view symbol = keyNode.getAttributeText( "symbol" );
                    if ( symbol.size() != 1 || symbol[0] == '.' )
                    {
                        SW_LOG_ERROR_ID( "CreatureLifeCatalog.HabitatKeyWhoseSymbol", "%#: habitat '%#' has a key whose symbol is not one character (or is '.')", sourceName, pHabitatID );
                        return false;
                    }
                    listKey.push_back( SymbolKey{ parseObject( keyNode.getAttributeText( "object" ) ), symbol[0] } );
                }
                outHabitat._listCell.clear();
                int32 width  = 0;
                int32 height = 0;
                for ( XMLNode rowNode = node.findChild( "Row" ); rowNode; rowNode = rowNode.findNextSibling( "Row" ) )
                {
                    const string_view cells = rowNode.getAttributeText( "cells" );
                    if ( height == 0 )
                        width = static_cast<int32>( cells.size() );
                    if ( cells.empty() || static_cast<int32>( cells.size() ) != width )
                    {
                        SW_LOG_ERROR_ID( "CreatureLifeCatalog.HabitatRowsDifferentLength", "%#: habitat '%#' has rows of different length", sourceName, pHabitatID );
                        return false;
                    }
                    for ( const utf8 symbol : cells )
                    {
                        HabitatCell cell;
                        if ( symbol == '.' )
                        {
                            cell._bAny = SW_TRUE;
                        }
                        else
                        {
                            const SymbolKey* pKey = VectorUtil::findBy<&SymbolKey::_symbol>( listKey, symbol );
                            if ( pKey == nullptr )
                            {
                                SW_LOG_ERROR_ID( "CreatureLifeCatalog.HabitatUsesUnknownSymbol", "%#: habitat '%#' uses an unknown symbol in row '%#'", sourceName, pHabitatID, cells );
                                return false;
                            }
                            cell._object = pKey->_object;
                        }
                        outHabitat._listCell.push_back( cell );
                    }
                    ++height;
                }
                outHabitat._topology = GridTopology{ width, height };
                if ( height == 0 || outHabitat.countClaimedCells() == 0 )
                {
                    SW_LOG_ERROR_ID( "CreatureLifeCatalog.HabitatNoCellsMatch", "%#: habitat '%#' has no cells to match", sourceName, pHabitatID );
                    return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    int32 HabitatDef::countClaimedCells() const
    {
        int32 count = 0;
        for ( const HabitatCell& cell : _listCell )
        {
            if ( cell._bAny == SW_FALSE )
                ++count;
        }
        return count;
    }

    const CreatureTileRule* CreatureAbilityDef::findRule( const hashed_string& object ) const
    {
        return VectorUtil::findBy<&CreatureTileRule::_from>( _listRule, object );
    }

    bool CreatureSpeciesDef::isFavoriteHabitat( const hashed_string& habitatID ) const
    {
        return VectorUtil::contains( _listHabitat, habitatID );
    }

    bool CreatureSpeciesDef::isFavoriteFood( const hashed_string& itemID ) const
    {
        return VectorUtil::contains( _listFood, itemID );
    }

    bool CreatureSpeciesDef::isFavoriteGift( const hashed_string& itemID ) const
    {
        return VectorUtil::contains( _listGift, itemID );
    }

    bool CreatureSpeciesDef::hasAbility( const hashed_string& abilityID ) const
    {
        return VectorUtil::contains( _listAbility, abilityID );
    }

    bool CreatureSpeciesDef::canOfferRequest( const hashed_string& questID ) const
    {
        return VectorUtil::contains( _listRequest, questID );
    }

    bool CreatureSpeciesDef::canAppearIn( DayPhase phase, const hashed_string& weatherID ) const
    {
        ScheduleConditionContext context;
        context._phase   = phase;
        context._weather = weatherID;
        return _visitCondition.matches( context );
    }

    CreatureLifeCatalog::CreatureLifeCatalog()
        : _habitatCatalog{}
        , _speciesCatalog{}
        , _abilityCatalog{}
        , _listHabitatOrder{}
        , _appeal{}
    {
    }

    void CreatureLifeCatalog::addHabitat( const HabitatDef& habitat )
    {
        const int32 cellCount         = habitat._topology.getCellCount();
        const bool  bMalformedHabitat = cellCount <= 0 || static_cast<int32>( habitat._listCell.size() ) != cellCount;
        if ( bMalformedHabitat )
        {
            SW_LOG_WARNING_ID( "CreatureLifeCatalog.HabitatMalformedPatternSkipped", "habitat '%#' has a malformed pattern - skipped", habitat._id.c_str() );
            return;
        }
        if ( _habitatCatalog.add( habitat ) >= 0 )
            rebuildHabitatOrder();
    }

    void CreatureLifeCatalog::rebuildHabitatOrder()
    {
        _listHabitatOrder.clear();
        for ( int32 index = 0; index < static_cast<int32>( _habitatCatalog.getCount() ); ++index )
        {
            _listHabitatOrder.push_back( index );
        }
        VectorUtil::stableSort( _listHabitatOrder, [this]( int32 lhs, int32 rhs )
        {
            return _habitatCatalog.getAt( static_cast<size_t>( lhs ) ).countClaimedCells() > _habitatCatalog.getAt( static_cast<size_t>( rhs ) ).countClaimedCells();
        } );
    }

    uint32 CreatureLifeCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        // 종은 능력 · 서식지를 가리키므로 표의 순서대로 읽고, 가리키는 id 는 모두 읽은 뒤 확인한다.
        using Element                          = XMLCatalogElement<CreatureLifeCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&CreatureLifeCatalog::loadHabitat>( "Habitat" ),
            Element::of<&CreatureLifeCatalog::loadAbility>( "Ability" ),
            Element::of<&CreatureLifeCatalog::loadSpecies>( "Species" ),
            Element::of<&CreatureLifeCatalog::loadAppeal>( "Appeal" ).uncounted(), // 틀리면 앞의 단계를 그대로 둔다
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInTableOrder( *this, root, sourceName, kArrElement );
        rebuildHabitatOrder();

        return result._loadedCount;
    }

    bool CreatureLifeCatalog::loadHabitat( const XMLNode& node, string_view sourceName )
    {
        HabitatDef habitat;
        bool       bValid = XMLFields::readFields( node, habitat, sourceName );
        bValid            = bValid && CreatureLifeCatalogInternal::parsePattern( node, sourceName, habitat._id.c_str(), habitat );
        if ( bValid )
            (void)_habitatCatalog.add( habitat ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool CreatureLifeCatalog::loadAbility( const XMLNode& node, string_view sourceName )
    {
        CreatureAbilityDef ability;
        bool               bValid = XMLFields::readFields( node, ability, sourceName );
        for ( XMLNode ruleNode = node.findChild( "Rule" ); ruleNode; ruleNode = ruleNode.findNextSibling( "Rule" ) )
        {
            CreatureTileRule rule;
            if ( XMLFields::readFields( ruleNode, rule, sourceName ) == false )
            {
                bValid = false;
                continue;
            }
            rule._from = CreatureLifeCatalogInternal::parseObject( rule._from.view() );
            rule._to   = CreatureLifeCatalogInternal::parseObject( rule._to.view() );
            if ( rule._from == rule._to )
            {
                SW_LOG_ERROR_ID( "CreatureLifeCatalog.AbilityRuleChangesNothing", "%#: ability '%#' has a rule that changes nothing", sourceName, ability._id.c_str() );
                bValid = false;
                continue;
            }
            ability._listRule.push_back( rule );
        }
        if ( ability._listRule.empty() )
        {
            SW_LOG_ERROR_ID( "CreatureLifeCatalog.AbilityNoRuleSkipped", "%#: ability '%#' has no <Rule> - skipped", sourceName, ability._id.c_str() );
            bValid = false;
        }
        if ( bValid )
            (void)_abilityCatalog.add( ability );
        return bValid;
    }

    bool CreatureLifeCatalog::loadSpecies( const XMLNode& node, string_view sourceName )
    {
        CreatureSpeciesDef species;
        bool               bValid = XMLFields::readFields( node, species, sourceName, { "weathers", "phases" } );
        bValid                    = species._visitCondition.loadFromXML( node, ScheduleConditionSchema{}, sourceName, species._id.view() ) && bValid; // 적은 `weathers` · `phases` 만 절이 된다
        if ( species._listHabitat.empty() )
        {
            SW_LOG_ERROR_ID( "CreatureLifeCatalog.SpeciesLikesNoHabitat", "%#: species '%#' likes no habitat - it would never come", sourceName, species._id.c_str() );
            bValid = false;
        }
        if ( bValid )
            (void)_speciesCatalog.add( species );
        return bValid;
    }

    bool CreatureLifeCatalog::loadAppeal( const XMLNode& node, string_view sourceName )
    {
        TownAppealDef appeal;
        bool          bValid = XMLFields::readFields( node, appeal, sourceName );
        for ( XMLNode tierNode = node.findChild( "Tier" ); tierNode; tierNode = tierNode.findNextSibling( "Tier" ) )
        {
            TownAppealTier tier;
            if ( XMLFields::readFields( tierNode, tier, sourceName ) )
                appeal._listTier.push_back( tier );
            else
                bValid = false;
        }
        if ( appeal._listTier.empty() )
        {
            SW_LOG_ERROR_ID( "CreatureLifeCatalog.AppealNoTier", "%#: <Appeal> has no <Tier>", sourceName );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        VectorUtil::stableSort( appeal._listTier, []( const TownAppealTier& lhs, const TownAppealTier& rhs )
        { return lhs._minScore < rhs._minScore; } );
        _appeal = appeal;
        return true;
    }
} // namespace sw

namespace sw
{
    /** @brief `CreatureLifeCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( Creatures, CreatureLifeCatalog, SW_KIT_API, AssetTypeDef::makeDataFile( CreatureLifeCatalog::kDataKind, AssetPathRule{ "game/", {}, "/data/creatures.xml" }, AssetTypeLoader::of<CreatureLifeCatalog>( MemoryTag::Game ) ) );
} // namespace sw
