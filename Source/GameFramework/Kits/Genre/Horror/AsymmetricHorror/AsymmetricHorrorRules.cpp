#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

#include <optional>

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

    const HorrorScoreRule* AsymmetricHorrorRulesCatalog::findScoreRule( const hashed_string& action ) const
    {
        return VectorUtil::findBy<&HorrorScoreRule::_action>( _listScoreRule, action );
    }

    uint32 AsymmetricHorrorRulesCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        AsymmetricHorrorRules rules;
        if ( XMLFields::readFields( root, rules, sourceName ) )
            _rules = rules;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) == false )
                SW_LOG_ERROR_ID( "AsymmetricHorrorRulesCatalog.NotLoadedSeeAbove", "%#: <%#> was not loaded (see above)", sourceName, name );
        }
        if ( _catalogKiller.isEmpty() )
            SW_LOG_ERROR_ID( "AsymmetricHorrorRulesCatalog.NoKillerRulesNeed", "%#: no <Killer> - the rules need at least one", sourceName );
        return static_cast<uint32>( _catalogKiller.getAll().size() );
    }

    bool AsymmetricHorrorRulesCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( XMLReadUtil::isElement( name, "Generator" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._generator );
        if ( XMLReadUtil::isElement( name, "Heal" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._heal );
        if ( XMLReadUtil::isElement( name, "SkillCheck" ) )
            return loadSkillCheck( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Hook" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._hook );
        if ( XMLReadUtil::isElement( name, "Pallet" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._pallet );
        if ( XMLReadUtil::isElement( name, "Window" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._window );
        if ( XMLReadUtil::isElement( name, "Locker" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._locker );
        if ( XMLReadUtil::isElement( name, "Endgame" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._endgame );
        if ( XMLReadUtil::isElement( name, "Killer" ) )
            return GameDataXML::readInto( node, sourceName, _catalogKiller );
        if ( XMLReadUtil::isElement( name, "Category" ) )
        {
            HorrorScoreCategoryDef category;
            if ( XMLFields::readFields( node, category, sourceName ) == false )
                return false;
            if ( _categoryCap.findValue( category._id ).has_value() )
            {
                SW_LOG_ERROR_ID( "AsymmetricHorrorRulesCatalog.DuplicateCategory", "%#: category '%#' is defined twice", sourceName, category._id.c_str() );
                return false;
            }
            _categoryCap.setValue( category._id, category._cap );
            return true;
        }
        if ( XMLReadUtil::isElement( name, "Score" ) )
        {
            HorrorScoreRule rule;
            if ( XMLFields::readFields( node, rule, sourceName ) == false )
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
        SW_LOG_ERROR_ID( "AsymmetricHorrorRulesCatalog.UnknownElementSkipped", "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool AsymmetricHorrorRulesCatalog::loadSkillCheck( const XMLNode& node, string_view sourceName )
    {
        HorrorSkillCheckDef skillCheck;
        bool                bValid = XMLFields::readFields( node, skillCheck, sourceName );
        // 창의 판정 폭(early · late · score)은 기반 TimingJudge 가 읽고, 등급마다 더하는 진행량(bonus)은 여기서 읽는다.
        for ( XMLNode window = node.findChild( "Window" ); window; window = window.findNextSibling( "Window" ) )
        {
            const std::optional<string> grade = window.readRequiredAttribute<string>( "grade" );
            std::optional<float32>      bonus;
            const bool                  bBonus = window.readOptionalAttribute( "bonus", bonus );
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
        if ( _judge.loadFromNode( node, sourceName, isBonus ) == false )
            return false;
        _rules._skillCheck = skillCheck;
        return true;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<AsymmetricHorrorRulesCatalog>::acquire`). 파일 규칙이 없는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( AsymmetricHorrorRulesCatalog, AsymmetricHorrorRulesCatalog, SW_KIT_API, AssetTypeDef::makeCacheOnly( "AsymmetricHorrorRulesCatalog", AssetTypeLoader::of<AsymmetricHorrorRulesCatalog>( MemoryTag::Game ) ) );
} // namespace sw
