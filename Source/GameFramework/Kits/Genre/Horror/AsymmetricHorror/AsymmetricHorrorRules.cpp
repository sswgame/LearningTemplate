#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include <optional>

namespace sw
{
    namespace
    {
        struct AsymmetricHorrorRulesInternal
        {
            /** @brief `scales` 가 있는 설정 원소를 읽습니다. `scales="1,1.7,2.1"` 는 적었으면 모두 0 이상의 수여야 합니다(적지 않으면 빈 목록). */
            template <typename TDef>
            [[nodiscard]] static bool readScaledSetting( const XMLNode& node, string_view sourceName, TDef& outSetting )
            {
                TDef def;
                bool bValid = GameDataXML::readFields( node, def, sourceName, { "scales" } );
                StringUtil::forEachToken( node.getAttributeText( "scales" ), ",; ", [&]( string_view token )
                {
                    float32    scale  = 0.0f;
                    const bool bScale = GameDataXML::parseNumber( token, scale ) && scale >= 0.0f;
                    if ( bScale )
                        def._listScale.push_back( scale );
                    else
                    {
                        SW_LOG_ERROR( "%#: <%# scales> has '%#' - needs numbers of 0 or more", sourceName, node.getName(), token );
                        bValid = false;
                    }
                } );
                if ( bValid )
                    outSetting = def;
                return bValid;
            }
        };
    } // namespace

    SW_LOG_CALLER( "AsymmetricHorrorRulesCatalog" );

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
        for ( const HorrorScoreRule& rule : _listScoreRule )
        {
            if ( rule._action == action )
                return &rule;
        }
        return nullptr;
    }

    uint32 AsymmetricHorrorRulesCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        AsymmetricHorrorRules rules;
        if ( GameDataXML::readFields( root, rules, sourceName ) )
            _rules = rules;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) == false )
                SW_LOG_ERROR( "%#: <%#> was not loaded (see above)", sourceName, name );
        }
        if ( _catalogKiller.isEmpty() )
            SW_LOG_ERROR( "%#: no <Killer> - the rules need at least one", sourceName );
        return static_cast<uint32>( _catalogKiller.getAll().size() );
    }

    bool AsymmetricHorrorRulesCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        using Internal = AsymmetricHorrorRulesInternal;
        const auto is  = [name]( const utf8* pName )
        { return StringUtil::equals( name, string_view( pName ), true ); };
        if ( is( "Generator" ) )
            return Internal::readScaledSetting( node, sourceName, _rules._generator );
        if ( is( "Heal" ) )
            return Internal::readScaledSetting( node, sourceName, _rules._heal );
        if ( is( "SkillCheck" ) )
            return loadSkillCheck( node, sourceName );
        if ( is( "Hook" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._hook );
        if ( is( "Pallet" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._pallet );
        if ( is( "Window" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._window );
        if ( is( "Locker" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._locker );
        if ( is( "Endgame" ) )
            return GameDataXML::readSetting( node, sourceName, _rules._endgame );
        if ( is( "Killer" ) )
        {
            HorrorKillerDef killer;
            if ( GameDataXML::readFields( node, killer, sourceName ) == false )
                return false;
            (void)_catalogKiller.add( killer ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
            return true;
        }
        if ( is( "Category" ) )
        {
            HorrorScoreCategoryDef category;
            if ( GameDataXML::readFields( node, category, sourceName ) == false )
                return false;
            _categoryCap.setValue( category._id, category._cap );
            return true;
        }
        if ( is( "Score" ) )
        {
            HorrorScoreRule rule;
            if ( GameDataXML::readFields( node, rule, sourceName ) == false )
                return false;
            _listScoreRule.push_back( rule );
            return true;
        }
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool AsymmetricHorrorRulesCatalog::loadSkillCheck( const XMLNode& node, string_view sourceName )
    {
        HorrorSkillCheckDef skillCheck;
        bool                bValid = GameDataXML::readFields( node, skillCheck, sourceName );
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
        if ( _judge.loadFromNode( node, sourceName ) == false )
            return false;
        _rules._skillCheck = skillCheck;
        return true;
    }
} // namespace sw
