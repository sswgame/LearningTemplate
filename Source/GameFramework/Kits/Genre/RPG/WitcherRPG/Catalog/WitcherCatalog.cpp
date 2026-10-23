#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/WitcherRPG/Catalog/WitcherCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    namespace
    {
        /** @brief 연금술 물건 하나를 읽습니다. 오일은 칼에 바른 뒤 적중 횟수(`hits`)가 1 이상이어야 합니다. */
        [[nodiscard]] bool readAlchemyItem( const XMLNode& node, string_view sourceName, WitcherAlchemyDef& outItem )
        {
            if ( GameDataXML::readFields( node, outItem, sourceName ) == false )
                return false;
            if ( outItem._kind == WitcherAlchemyKind::Oil && outItem._hits < 1 )
            {
                SW_LOG_ERROR( "%#: oil '%#' needs hits=\"...\" of 1 or more", sourceName, outItem._id.c_str() );
                return false;
            }
            return true;
        }
    } // namespace

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

    hashed_string WitcherCatalog::getSkillColor( const hashed_string& skillID ) const
    {
        const WitcherSkillColorDef* pColor = _skillColorCatalog.find( skillID );
        return pColor != nullptr ? pColor->_color : hashed_string{};
    }

    uint32 WitcherCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) )
                ++loadedCount;
        }
        for ( const WitcherMonsterDef& monster : _monsterCatalog.getAll() )
        {
            for ( const WitcherWeakness& weakness : monster._listWeakness )
            {
                const bool bSign = weakness._kind == WitcherWeaknessKind::Sign;
                if ( bSign ? _signCatalog.find( weakness._id ) == nullptr : _alchemyCatalog.find( weakness._id ) == nullptr )
                    SW_LOG_ERROR( "%#: monster '%#' names an unknown weakness '%#'", sourceName, monster._id.c_str(), weakness._id.c_str() );
            }
        }
        return loadedCount;
    }

    bool WitcherCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( GameDataXML::isElement( name, "Alchemy" ) )
        {
            WitcherAlchemyRuleDef rule;
            if ( GameDataXML::readSetting( node, sourceName, rule, { "alcohol" } ) == false )
                return false;
            GameDataXML::parseNameList( node.getAttributeText( "alcohol" ), ",; ", rule._listAlcohol );
            _alchemy = rule;
            return true;
        }
        if ( GameDataXML::isElement( name, "Combat" ) )
            return GameDataXML::readSetting( node, sourceName, _combat );
        if ( GameDataXML::isElement( name, "SlotGroup" ) )
        {
            WitcherSlotGroupDef group;
            if ( GameDataXML::readFields( node, group, sourceName ) == false )
                return false;
            _listSlotGroup.push_back( group );
            return true;
        }
        if ( GameDataXML::isElement( name, "SkillColor" ) )
            return GameDataXML::readInto( node, sourceName, _skillColorCatalog );
        if ( GameDataXML::isElement( name, "Monster" ) )
            return loadMonster( node, sourceName );
        if ( GameDataXML::isElement( name, "Item" ) )
        {
            WitcherAlchemyDef item;
            if ( readAlchemyItem( node, sourceName, item ) == false )
                return false;
            (void)_alchemyCatalog.add( item ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
            return true;
        }
        if ( GameDataXML::isElement( name, "Sign" ) )
            return GameDataXML::readInto( node, sourceName, _signCatalog );
        if ( GameDataXML::isElement( name, "Mutagen" ) )
            return GameDataXML::readInto( node, sourceName, _mutagenCatalog );
        if ( GameDataXML::isElement( name, "Contract" ) )
            return loadContract( node, sourceName );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool WitcherCatalog::loadMonster( const XMLNode& node, string_view sourceName )
    {
        WitcherMonsterDef monster;
        bool              bValid = GameDataXML::readFields( node, monster, sourceName, { "elements" } );
        GameDataXML::parseNameList( node.getAttributeText( "elements" ), ",; ", monster._listElement );
        if ( monster._listElement.empty() && monster._category.empty() == false )
            monster._listElement.push_back( monster._category ); // 방어 속성을 적지 않으면 분류 하나다(`_listElement` 의 정의)
        for ( XMLNode child = node.findChild( "Weakness" ); child; child = child.findNextSibling( "Weakness" ) )
        {
            WitcherWeakness weakness;
            if ( GameDataXML::readFields( child, weakness, sourceName ) == false )
            {
                bValid = false;
                continue;
            }
            if ( weakness._knowledge > monster._maxKnowledge )
            {
                SW_LOG_ERROR( "%#: monster '%#' weakness '%#' needs knowledge %# above maxKnowledge %#", sourceName, monster._id.c_str(),
                              weakness._id.c_str(), weakness._knowledge, monster._maxKnowledge );
                bValid = false;
                continue;
            }
            monster._listWeakness.push_back( weakness );
        }
        if ( bValid )
            (void)_monsterCatalog.add( monster ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool WitcherCatalog::loadContract( const XMLNode& node, string_view sourceName )
    {
        WitcherContractDef contract;
        bool               bValid = GameDataXML::readFields( node, contract, sourceName );
        for ( XMLNode stepNode = node.findChild( "Step" ); stepNode; stepNode = stepNode.findNextSibling( "Step" ) )
        {
            WitcherContractStepDef step;
            bValid = GameDataXML::readFields( stepNode, step, sourceName ) && bValid;
            for ( XMLNode clueNode = stepNode.findChild( "Clue" ); clueNode; clueNode = clueNode.findNextSibling( "Clue" ) )
            {
                WitcherClueDef clue;
                if ( GameDataXML::readFields( clueNode, clue, sourceName ) )
                    step._listClue.push_back( clue );
                else
                    bValid = false;
            }
            contract._listStep.push_back( step );
        }
        if ( bValid )
            (void)_contractCatalog.add( contract ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }
} // namespace sw
