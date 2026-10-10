#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureLifeCatalog.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "CreatureLifeCatalog" );

    namespace
    {
        struct CreatureLifeCatalogInternal
        {
            /** @brief 패턴 기호 하나의 뜻입니다. */
            struct SymbolKey
            {
                hashed_string _object{};
                utf8          _symbol{ 0 };
            };

            static bool contains( const vector<hashed_string>& listId, const hashed_string& id )
            {
                for ( const hashed_string& entry : listId )
                {
                    if ( entry == id )
                        return true;
                }
                return false;
            }

            /** @brief "a,b c" 목록을 id 목록으로 읽습니다. */
            static void parseIdList( string_view text, vector<hashed_string>& outListId )
            {
                outListId.clear();
                GameDataXML::forEachToken( text, ",;| ", [&]( string_view token )
                { outListId.push_back( hashed_string( token ) ); } );
            }

            /** @brief 오브젝트 이름입니다 — `empty` 는 빈 칸(빈 id)입니다. */
            static hashed_string parseObject( string_view text )
            {
                const hashed_string object( text );
                return object == hashed_string( "empty" ) ? hashed_string{} : object;
            }

            /** @brief `<Key>` · `<Row>` 로 패턴을 읽습니다. 행 길이가 다르거나 모르는 기호면 false 입니다. */
            [[nodiscard]] static bool parsePattern( const XMLNode& node, string_view sourceName, const utf8* pHabitatId, HabitatDef& outHabitat )
            {
                vector<SymbolKey> listKey;
                for ( XMLNode keyNode = node.findChild( "Key" ); keyNode; keyNode = keyNode.findNextSibling( "Key" ) )
                {
                    const string_view symbol = keyNode.getAttributeText( "symbol" );
                    if ( symbol.size() != 1 || symbol[0] == '.' )
                    {
                        SW_LOG_WARNING( "%#: habitat '%#' has a key whose symbol is not one character (or is '.')", sourceName, pHabitatId );
                        return false;
                    }
                    listKey.push_back( SymbolKey{ parseObject( keyNode.getAttributeText( "object" ) ), symbol[0] } );
                }
                outHabitat._listCell.clear();
                outHabitat._width  = 0;
                outHabitat._height = 0;
                for ( XMLNode rowNode = node.findChild( "Row" ); rowNode; rowNode = rowNode.findNextSibling( "Row" ) )
                {
                    const string_view cells = rowNode.getAttributeText( "cells" );
                    if ( outHabitat._height == 0 )
                        outHabitat._width = static_cast<int32>( cells.size() );
                    if ( cells.empty() || static_cast<int32>( cells.size() ) != outHabitat._width )
                    {
                        SW_LOG_WARNING( "%#: habitat '%#' has rows of different length", sourceName, pHabitatId );
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
                            bool bKnown = false;
                            for ( const SymbolKey& key : listKey )
                            {
                                if ( key._symbol == symbol )
                                {
                                    cell._object = key._object;
                                    bKnown       = true;
                                    break;
                                }
                            }
                            if ( bKnown == false )
                            {
                                SW_LOG_WARNING( "%#: habitat '%#' uses an unknown symbol in row '%#'", sourceName, pHabitatId, cells );
                                return false;
                            }
                        }
                        outHabitat._listCell.push_back( cell );
                    }
                    ++outHabitat._height;
                }
                if ( outHabitat._height == 0 || outHabitat.countClaimedCells() == 0 )
                {
                    SW_LOG_WARNING( "%#: habitat '%#' has no cells to match", sourceName, pHabitatId );
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
        for ( const CreatureTileRule& rule : _listRule )
        {
            if ( rule._from == object )
                return &rule;
        }
        return nullptr;
    }

    bool CreatureSpeciesDef::likesHabitat( const hashed_string& habitatId ) const { return CreatureLifeCatalogInternal::contains( _listHabitat, habitatId ); }

    bool CreatureSpeciesDef::likesFood( const hashed_string& itemId ) const { return CreatureLifeCatalogInternal::contains( _listFood, itemId ); }

    bool CreatureSpeciesDef::likesGift( const hashed_string& itemId ) const { return CreatureLifeCatalogInternal::contains( _listGift, itemId ); }

    bool CreatureSpeciesDef::hasAbility( const hashed_string& abilityId ) const { return CreatureLifeCatalogInternal::contains( _listAbility, abilityId ); }

    bool CreatureSpeciesDef::offersRequest( const hashed_string& questId ) const { return CreatureLifeCatalogInternal::contains( _listRequest, questId ); }

    bool CreatureSpeciesDef::comesIn( DayPhase phase, const hashed_string& weatherId ) const
    {
        ScheduleConditionContext context;
        context._phase   = phase;
        context._weather = weatherId;
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
        if ( habitat._width <= 0 || habitat._height <= 0 || static_cast<int32>( habitat._listCell.size() ) != habitat._width * habitat._height )
        {
            SW_LOG_WARNING( "habitat '%#' has a malformed pattern - skipped", habitat._id.c_str() );
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
        std::stable_sort( _listHabitatOrder.begin(), _listHabitatOrder.end(), [this]( int32 lhs, int32 rhs )
        {
            return _habitatCatalog.getAt( static_cast<size_t>( lhs ) ).countClaimedCells() > _habitatCatalog.getAt( static_cast<size_t>( rhs ) ).countClaimedCells();
        } );
    }

    uint32 CreatureLifeCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Habitat" ); node; node = node.findNextSibling( "Habitat" ) )
        {
            const utf8* pId = GameDataXML::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            HabitatDef habitat;
            habitat._id       = hashed_string( pId );
            const utf8* pName = node.findAttribute( "name" );
            habitat._name     = pName != nullptr ? pName : pId;
            habitat._capacity = MathUtil::max( 1, node.getAttributeInt( "capacity", habitat._capacity ) );
            if ( CreatureLifeCatalogInternal::parsePattern( node, sourceName, pId, habitat ) == false )
                continue;
            (void)_habitatCatalog.add( habitat );
            ++loadedCount;
        }
        rebuildHabitatOrder();

        for ( XMLNode node = root.findChild( "Ability" ); node; node = node.findNextSibling( "Ability" ) )
        {
            const utf8* pId = GameDataXML::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            CreatureAbilityDef ability;
            ability._id         = hashed_string( pId );
            ability._usesPerDay = MathUtil::max( 1, node.getAttributeInt( "uses", ability._usesPerDay ) );
            for ( XMLNode ruleNode = node.findChild( "Rule" ); ruleNode; ruleNode = ruleNode.findNextSibling( "Rule" ) )
            {
                CreatureTileRule rule;
                rule._from         = CreatureLifeCatalogInternal::parseObject( ruleNode.getAttributeText( "from" ) );
                rule._to           = CreatureLifeCatalogInternal::parseObject( ruleNode.getAttributeText( "to" ) );
                const utf8* pYield = ruleNode.findAttribute( "yield" );
                rule._yieldItem    = pYield != nullptr ? hashed_string( pYield ) : hashed_string{};
                rule._yieldCount   = MathUtil::max( 1, ruleNode.getAttributeInt( "count", rule._yieldCount ) );
                if ( rule._from == rule._to )
                {
                    SW_LOG_WARNING( "%#: ability '%#' has a rule that changes nothing", sourceName, pId );
                    continue;
                }
                ability._listRule.push_back( rule );
            }
            if ( ability._listRule.empty() )
            {
                SW_LOG_WARNING( "%#: ability '%#' has no <Rule> - skipped", sourceName, pId );
                continue;
            }
            (void)_abilityCatalog.add( ability );
            ++loadedCount;
        }

        for ( XMLNode node = root.findChild( "Species" ); node; node = node.findNextSibling( "Species" ) )
        {
            const utf8* pId = GameDataXML::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            CreatureSpeciesDef species;
            species._id       = hashed_string( pId );
            const utf8* pName = node.findAttribute( "name" );
            species._name     = pName != nullptr ? pName : pId;
            species._chance   = MathUtil::clamp( node.getAttributeFloat( "chance", species._chance ), 0.0f, 1.0f );
            CreatureLifeCatalogInternal::parseIdList( node.getAttributeText( "habitats" ), species._listHabitat );
            CreatureLifeCatalogInternal::parseIdList( node.getAttributeText( "foods" ), species._listFood );
            CreatureLifeCatalogInternal::parseIdList( node.getAttributeText( "gifts" ), species._listGift );
            ScheduleCondition::parseNameList( node.getAttributeText( "weathers" ), species._visitCondition._listWeather );
            CreatureLifeCatalogInternal::parseIdList( node.getAttributeText( "abilities" ), species._listAbility );
            CreatureLifeCatalogInternal::parseIdList( node.getAttributeText( "requests" ), species._listRequest );
            species._visitCondition._phaseMask = ScheduleCondition::parsePhaseMask( node.getAttributeText( "phases" ), sourceName, pId );
            if ( species._listHabitat.empty() )
                SW_LOG_WARNING( "%#: species '%#' likes no habitat - it will never come", sourceName, pId );
            for ( const hashed_string& abilityId : species._listAbility )
            {
                if ( _abilityCatalog.find( abilityId ) == nullptr )
                    SW_LOG_WARNING( "%#: species '%#' has an unknown ability '%#'", sourceName, pId, abilityId.c_str() );
            }
            (void)_speciesCatalog.add( species );
            ++loadedCount;
        }

        const XMLNode appealNode = root.findChild( "Appeal" );
        if ( appealNode )
        {
            _appeal._diversityWeight  = appealNode.getAttributeFloat( "diversity", _appeal._diversityWeight );
            _appeal._creatureWeight   = appealNode.getAttributeFloat( "creature", _appeal._creatureWeight );
            _appeal._friendshipWeight = appealNode.getAttributeFloat( "friendship", _appeal._friendshipWeight );
            _appeal._listTier.clear();
            for ( XMLNode tierNode = appealNode.findChild( "Tier" ); tierNode; tierNode = tierNode.findNextSibling( "Tier" ) )
            {
                const utf8* pTierName = tierNode.findAttribute( "name" );
                if ( pTierName != nullptr )
                    _appeal._listTier.push_back( TownAppealTier{ hashed_string( pTierName ), tierNode.getAttributeFloat( "min", 0.0f ) } );
            }
            std::stable_sort( _appeal._listTier.begin(), _appeal._listTier.end(),
                              []( const TownAppealTier& lhs, const TownAppealTier& rhs )
            { return lhs._minScore < rhs._minScore; } );
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Habitat> / <Ability> / <Species> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
