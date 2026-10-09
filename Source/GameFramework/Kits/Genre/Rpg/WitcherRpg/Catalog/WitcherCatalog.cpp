#include "pch.h"

#include "GameFramework/Kits/Genre/Rpg/WitcherRpg/Catalog/WitcherCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

namespace sw
{
    namespace
    {
        struct WitcherCatalogInternal
        {
            static hashed_string readName( const XmlNode& node, const utf8* pAttribute )
            {
                const utf8* pText = node.findAttribute( pAttribute );
                return pText != nullptr && pText[0] != '\0' ? hashed_string( pText ) : hashed_string{};
            }

            static bool isNamed( string_view name, const utf8* pExpected ) { return StringUtil::equals( name, string_view( pExpected ), true ); }

            static void readNameList( string_view text, vector<hashed_string>& outListName )
            {
                outListName.clear();
                GameDataXml::forEachToken( text, ",; ", [&]( string_view token )
                { outListName.push_back( hashed_string( token ) ); } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "WitcherCatalog" );

    WitcherCatalog::WitcherCatalog()
        : _monsterCatalog{}
        , _alchemyCatalog{}
        , _signCatalog{}
        , _mutagenCatalog{}
        , _contractCatalog{}
        , _skillColorCatalog{}
        , _listSlotGroup{}
        , _combat{}
        , _alchemy{}
    {
    }

    hashed_string WitcherCatalog::getSkillColor( const hashed_string& skillId ) const
    {
        const WitcherSkillColorDef* pColor = _skillColorCatalog.find( skillId );
        return pColor != nullptr ? pColor->_color : hashed_string{};
    }

    uint32 WitcherCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( WitcherCatalogInternal::isNamed( name, "Alchemy" ) )
            {
                _alchemy._maxToxicity   = MathUtil::max( 1.0f, node.getAttributeFloat( "maxToxicity", _alchemy._maxToxicity ) );
                _alchemy._toxicityDecay = MathUtil::max( 0.0f, node.getAttributeFloat( "toxicityDecay", _alchemy._toxicityDecay ) );
                WitcherCatalogInternal::readNameList( node.getAttributeText( "alcohol" ), _alchemy._listAlcohol );
                continue;
            }
            if ( WitcherCatalogInternal::isNamed( name, "Combat" ) )
            {
                WitcherCombatSettings& combat = _combat;
                const hashed_string    stat   = WitcherCatalogInternal::readName( node, "intensityStat" );
                if ( stat.empty() == false )
                    combat._intensityStat = stat;
                combat._stamina               = MathUtil::max( 1.0f, node.getAttributeFloat( "stamina", combat._stamina ) );
                combat._staminaRegen          = MathUtil::max( 0.0f, node.getAttributeFloat( "regen", combat._staminaRegen ) );
                combat._staminaDelay          = MathUtil::max( 0.0f, node.getAttributeFloat( "delay", combat._staminaDelay ) );
                combat._fastCost              = MathUtil::max( 0.0f, node.getAttributeFloat( "fast", combat._fastCost ) );
                combat._strongCost            = MathUtil::max( 0.0f, node.getAttributeFloat( "strong", combat._strongCost ) );
                combat._dodgeCost             = MathUtil::max( 0.0f, node.getAttributeFloat( "dodge", combat._dodgeCost ) );
                combat._rollCost              = MathUtil::max( 0.0f, node.getAttributeFloat( "roll", combat._rollCost ) );
                combat._adrenalineMax         = MathUtil::max( 0.0f, node.getAttributeFloat( "adrenalineMax", combat._adrenalineMax ) );
                combat._adrenalinePerHit      = MathUtil::max( 0.0f, node.getAttributeFloat( "adrenalinePerHit", combat._adrenalinePerHit ) );
                combat._adrenalineLossOnHit   = MathUtil::max( 0.0f, node.getAttributeFloat( "adrenalineLoss", combat._adrenalineLossOnHit ) );
                combat._adrenalineDamageBonus = MathUtil::max( 0.0f, node.getAttributeFloat( "adrenalineBonus", combat._adrenalineDamageBonus ) );
                continue;
            }
            if ( WitcherCatalogInternal::isNamed( name, "SlotGroup" ) )
            {
                WitcherSlotGroupDef group;
                group._requiredLevel = MathUtil::max( 0, node.getAttributeInt( "level", group._requiredLevel ) );
                group._slotCount     = MathUtil::clamp( node.getAttributeInt( "slots", group._slotCount ), 1, 16 );
                _listSlotGroup.push_back( group );
                ++loadedCount;
                continue;
            }
            if ( WitcherCatalogInternal::isNamed( name, "SkillColor" ) )
            {
                WitcherSkillColorDef color;
                color._id    = WitcherCatalogInternal::readName( node, "skill" );
                color._color = WitcherCatalogInternal::readName( node, "color" );
                if ( color._id.empty() )
                    SW_LOG_WARNING( "%#: <SkillColor> without a skill - skipped", sourceName );
                else
                    (void)_skillColorCatalog.add( color );
                continue;
            }

            const bool bKnown = WitcherCatalogInternal::isNamed( name, "Monster" ) || WitcherCatalogInternal::isNamed( name, "Item" ) ||
                                WitcherCatalogInternal::isNamed( name, "Sign" ) || WitcherCatalogInternal::isNamed( name, "Mutagen" ) ||
                                WitcherCatalogInternal::isNamed( name, "Contract" );
            if ( bKnown == false )
            {
                SW_LOG_WARNING( "%#: unknown element <%#> - skipped", sourceName, name );
                continue;
            }
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            const hashed_string id( pId );
            ++loadedCount;
            if ( WitcherCatalogInternal::isNamed( name, "Monster" ) )
            {
                loadMonster( node, id, sourceName );
            }
            else if ( WitcherCatalogInternal::isNamed( name, "Item" ) )
            {
                WitcherAlchemyDef item;
                item._id               = id;
                item._element          = WitcherCatalogInternal::readName( node, "element" );
                const string_view kind = node.getAttributeText( "kind" );
                if ( WitcherCatalogInternal::isNamed( kind, "Decoction" ) )
                    item._kind = WitcherAlchemyKind::Decoction;
                else if ( WitcherCatalogInternal::isNamed( kind, "Oil" ) )
                    item._kind = WitcherAlchemyKind::Oil;
                else if ( WitcherCatalogInternal::isNamed( kind, "Bomb" ) )
                    item._kind = WitcherAlchemyKind::Bomb;
                else if ( kind.empty() == false && WitcherCatalogInternal::isNamed( kind, "Potion" ) == false )
                    SW_LOG_WARNING( "%#: '%#' has an unknown kind '%#' - read as a potion", sourceName, pId, kind );
                item._toxicity = MathUtil::max( 0.0f, node.getAttributeFloat( "toxicity", item._toxicity ) );
                item._duration = MathUtil::max( 0.0f, node.getAttributeFloat( "duration", item._duration ) );
                item._charges  = MathUtil::max( 1, node.getAttributeInt( "charges", item._charges ) );
                item._hits     = MathUtil::max( 0, node.getAttributeInt( "hits", item._kind == WitcherAlchemyKind::Oil ? 20 : 0 ) );
                (void)_alchemyCatalog.add( item );
            }
            else if ( WitcherCatalogInternal::isNamed( name, "Sign" ) )
            {
                WitcherSignDef sign;
                sign._id            = id;
                sign._element       = WitcherCatalogInternal::readName( node, "element" );
                sign._altSkill      = WitcherCatalogInternal::readName( node, "altSkill" );
                sign._cost          = MathUtil::max( 0.0f, node.getAttributeFloat( "cost", sign._cost ) );
                sign._altCost       = MathUtil::max( 0.0f, node.getAttributeFloat( "altCost", sign._altCost ) );
                sign._basePower     = MathUtil::max( 0.0f, node.getAttributeFloat( "power", sign._basePower ) );
                sign._altPowerScale = MathUtil::max( 0.0f, node.getAttributeFloat( "altPowerScale", sign._altPowerScale ) );
                (void)_signCatalog.add( sign );
            }
            else if ( WitcherCatalogInternal::isNamed( name, "Mutagen" ) )
            {
                WitcherMutagenDef mutagen;
                mutagen._id         = id;
                mutagen._color      = WitcherCatalogInternal::readName( node, "color" );
                mutagen._stat       = WitcherCatalogInternal::readName( node, "stat" );
                mutagen._value      = node.getAttributeFloat( "value", 0.0f );
                mutagen._matchValue = node.getAttributeFloat( "matchValue", 0.0f );
                (void)_mutagenCatalog.add( mutagen );
            }
            else
            {
                loadContract( node, id );
            }
        }
        for ( const WitcherMonsterDef& monster : _monsterCatalog.getAll() )
        {
            for ( const WitcherWeakness& weakness : monster._listWeakness )
            {
                const bool bSign = weakness._kind == WitcherWeaknessKind::Sign;
                if ( bSign ? _signCatalog.find( weakness._id ) == nullptr : _alchemyCatalog.find( weakness._id ) == nullptr )
                    SW_LOG_WARNING( "%#: monster '%#' names an unknown weakness '%#'", sourceName, monster._id.c_str(), weakness._id.c_str() );
            }
        }
        return loadedCount;
    }

    void WitcherCatalog::loadMonster( const XmlNode& node, const hashed_string& id, string_view sourceName )
    {
        WitcherMonsterDef monster;
        monster._id       = id;
        monster._category = WitcherCatalogInternal::readName( node, "category" );
        WitcherCatalogInternal::readNameList( node.getAttributeText( "elements" ), monster._listElement );
        if ( monster._listElement.empty() && monster._category.empty() == false )
            monster._listElement.push_back( monster._category );
        monster._maxKnowledge     = MathUtil::max( 1, node.getAttributeInt( "maxKnowledge", monster._maxKnowledge ) );
        monster._readLevel        = MathUtil::clamp( node.getAttributeInt( "readLevel", monster._maxKnowledge ), 0, monster._maxKnowledge );
        monster._killsPerLevel    = MathUtil::max( 1, node.getAttributeInt( "killsPerLevel", monster._killsPerLevel ) );
        monster._killCap          = MathUtil::clamp( node.getAttributeInt( "killCap", monster._killCap ), 0, monster._maxKnowledge );
        monster._investigateLevel = MathUtil::clamp( node.getAttributeInt( "investigateLevel", monster._investigateLevel ), 0, monster._maxKnowledge );
        for ( XmlNode child = node.findChild( "Weakness" ); child; child = child.findNextSibling( "Weakness" ) )
        {
            WitcherWeakness weakness;
            weakness._id           = WitcherCatalogInternal::readName( child, "id" );
            weakness._knowledge    = MathUtil::clamp( child.getAttributeInt( "knowledge", weakness._knowledge ), 0, monster._maxKnowledge );
            const string_view kind = child.getAttributeText( "kind" );
            if ( WitcherCatalogInternal::isNamed( kind, "Bomb" ) )
                weakness._kind = WitcherWeaknessKind::Bomb;
            else if ( WitcherCatalogInternal::isNamed( kind, "Sign" ) )
                weakness._kind = WitcherWeaknessKind::Sign;
            if ( weakness._id.empty() )
            {
                SW_LOG_WARNING( "%#: monster '%#' has a weakness without an id - skipped", sourceName, id.c_str() );
                continue;
            }
            monster._listWeakness.push_back( weakness );
        }
        (void)_monsterCatalog.add( monster );
    }

    void WitcherCatalog::loadContract( const XmlNode& node, const hashed_string& id )
    {
        WitcherContractDef contract;
        contract._id            = id;
        contract._questId       = WitcherCatalogInternal::readName( node, "quest" );
        contract._reward        = MathUtil::max( 0, node.getAttributeInt( "reward", contract._reward ) );
        contract._limitRatio    = MathUtil::max( 1.0f, node.getAttributeFloat( "limit", contract._limitRatio ) );
        contract._angerMax      = MathUtil::max( 0.01f, node.getAttributeFloat( "angerMax", contract._angerMax ) );
        contract._angerScale    = MathUtil::max( 0.0f, node.getAttributeFloat( "angerScale", contract._angerScale ) );
        contract._angerPerRound = MathUtil::max( 0.0f, node.getAttributeFloat( "angerPerRound", contract._angerPerRound ) );
        for ( XmlNode stepNode = node.findChild( "Step" ); stepNode; stepNode = stepNode.findNextSibling( "Step" ) )
        {
            WitcherContractStepDef step;
            step._id = WitcherCatalogInternal::readName( stepNode, "id" );
            for ( XmlNode clueNode = stepNode.findChild( "Clue" ); clueNode; clueNode = clueNode.findNextSibling( "Clue" ) )
            {
                WitcherClueDef clue;
                clue._id       = WitcherCatalogInternal::readName( clueNode, "id" );
                clue._position = float3{ clueNode.getAttributeFloat( "x", 0.0f ), clueNode.getAttributeFloat( "y", 0.0f ), clueNode.getAttributeFloat( "z", 0.0f ) };
                clue._radius   = MathUtil::max( 0.0f, clueNode.getAttributeFloat( "radius", clue._radius ) );
                clue._order    = MathUtil::max( 0, clueNode.getAttributeInt( "order", clue._order ) );
                if ( clue._id.empty() == false )
                    step._listClue.push_back( clue );
            }
            contract._listStep.push_back( step );
        }
        (void)_contractCatalog.add( contract );
    }
} // namespace sw
