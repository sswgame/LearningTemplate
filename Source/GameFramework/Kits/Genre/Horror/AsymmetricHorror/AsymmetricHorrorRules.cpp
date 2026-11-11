#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Expected.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    AsymmetricHorrorRulesCatalog::AsymmetricHorrorRulesCatalog()
        : _rules{}
        , _judge{}
        , _catalogKiller{}
        , _listScoreRule{}
        , _categoryCap{}
    {
    }

    const AsymmetricHorrorScoreRule* AsymmetricHorrorRulesCatalog::findScoreRule( const hashed_string& action ) const
    {
        return VectorUtil::findBy<&AsymmetricHorrorScoreRule::_action>( _listScoreRule, action );
    }

    uint32 AsymmetricHorrorRulesCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        AsymmetricHorrorRules rules;
        if ( XMLFields::readProperties( root, rules, sourceName ) )
            _rules = rules;
        // 살인마가 없으면 판을 열 수 없다 — 세는 줄은 살인마뿐이고 없으면 읽기 실패다.
        using Catalog                          = AsymmetricHorrorRulesCatalog;
        using Rules                            = AsymmetricHorrorRules;
        using Element                          = XMLCatalogElement<Catalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&Catalog::_rules, &Rules::_generator>( "Generator" ).uncounted(),
            Element::of<&Catalog::_rules, &Rules::_heal>( "Heal" ).uncounted(),
            Element::of<&Catalog::loadSkillCheck>( "SkillCheck" ).uncounted(),
            Element::of<&Catalog::_rules, &Rules::_hook>( "Hook" ).uncounted(),
            Element::of<&Catalog::_rules, &Rules::_pallet>( "Pallet" ).uncounted(),
            Element::of<&Catalog::_rules, &Rules::_window>( "Window" ).uncounted(),
            Element::of<&Catalog::_rules, &Rules::_locker>( "Locker" ).uncounted(),
            Element::of<&Catalog::_rules, &Rules::_endgame>( "Endgame" ).uncounted(),
            Element::of<&Catalog::_catalogKiller>( "Killer" ),
            Element::of<&Catalog::loadCategory>( "Category" ).uncounted(),
            Element::of<&Catalog::loadScore>( "Score" ).uncounted(),
        };
        return XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool AsymmetricHorrorRulesCatalog::loadCategory( const XMLNode& node, string_view sourceName )
    {
        AsymmetricHorrorScoreCategoryDef category;
        if ( XMLFields::readProperties( node, category, sourceName ) == false )
            return false;
        if ( _categoryCap.findValue( category._id ).has_value() )
        {
            SW_LOG_ERROR_ID( "AsymmetricHorrorRulesCatalog.DuplicateCategory", "%#: category '%#' is defined twice", sourceName, category._id.c_str() );
            return false;
        }
        _categoryCap.setValue( category._id, category._cap );
        return true;
    }

    bool AsymmetricHorrorRulesCatalog::loadScore( const XMLNode& node, string_view sourceName )
    {
        AsymmetricHorrorScoreRule rule;
        if ( XMLFields::readProperties( node, rule, sourceName ) == false )
            return false;
        // 찾기는 앞의 규칙을 돌려주므로, 겹친 뒤 규칙은 말없이 쓰이지 않는다.
        if ( findScoreRule( rule._action ) != nullptr )
        {
            SW_LOG_ERROR_ID( "AsymmetricHorrorRulesCatalog.DuplicateScoreRule", "%#: score of action '%#' is defined twice", sourceName, rule._action.c_str() );
            return false;
        }
        _listScoreRule.push_back( rule );
        return true;
    }

    bool AsymmetricHorrorRulesCatalog::loadSkillCheck( const XMLNode& node, string_view sourceName )
    {
        AsymmetricHorrorSkillCheckDef skillCheck;
        bool                          bValid = XMLFields::readProperties( node, skillCheck, sourceName );
        // 창의 판정 폭(early · late · score)은 기반 TimingJudge 가 읽고, 등급마다 더하는 진행량(bonus)은 여기서 읽는다.
        for ( XMLNode window = node.findChild( "Window" ); window; window = window.findNextSibling( "Window" ) )
        {
            const auto        grade = window.readRequiredAttribute<string>( "grade" );
            optional<float32> bonus;
            const bool        bBonus = window.readOptionalAttribute( "bonus", bonus );
            if ( grade.has_value() == false || bBonus == false )
            {
                bValid = false;
                continue;
            }
            if ( bonus.has_value() )
                skillCheck._bonus.setValue( hashed_string( grade->c_str() ), *bonus );
        }
        if ( bValid == false )
            return false;
        // `bonus` 는 위에서 읽었다. 판정기가 모르는 이름으로 거절하면 `<SkillCheck>` 가 통째로 빠진다.
        const auto isBonus = []( const utf8* pName )
        { return StringUtil::equals( string_view( pName ), string_view( "bonus" ) ); };
        if ( _judge.loadFromXML( node, sourceName, isBonus ) == false )
            return false;
        _rules._skillCheck = skillCheck;
        return true;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<AsymmetricHorrorRulesCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( AsymmetricHorrorRulesCatalog, AsymmetricHorrorRulesCatalog, SW_KIT_API, AsymmetricHorrorRulesCatalog::kAssetType.withLoader( AssetTypeLoader::of<AsymmetricHorrorRulesCatalog>( MemoryTag::Game ) ) );
} // namespace sw
