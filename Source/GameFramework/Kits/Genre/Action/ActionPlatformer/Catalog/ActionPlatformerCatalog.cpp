#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "ActionPlatformerCatalog" );

    namespace
    {
        struct ActionPlatformerCatalogInternal
        {
            static constexpr const utf8* kNameSeparators = ",; \t";

            static bool isGradeHigher( const ActionGradeDef& lhs, const ActionGradeDef& rhs ) { return lhs._minScore > rhs._minScore; }
        };
    } // namespace

    int32 ActionPatternDef::findStateIndex( const hashed_string& stateID ) const
    {
        for ( size_t index = 0; index < _listState.size(); ++index )
        {
            if ( _listState[index]._id == stateID )
                return static_cast<int32>( index );
        }
        return -1;
    }

    ActionPlatformerCatalog::ActionPlatformerCatalog()
        : _stageCatalog{}
        , _comboCatalog{}
        , _patternCatalog{}
        , _grading{}
        , _bodySettings{}
        , _parryRules{}
    {
    }

    uint32 ActionPlatformerCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) )
                ++loadedCount;
        }
        // 등급표는 점수를 등급으로 바꾸는 유일한 근거다 — 코드가 대신 정한 표를 쓰지 않는다.
        if ( _grading._listGrade.empty() )
        {
            SW_LOG_ERROR( "%#: needs <Grading> with at least one <Grade id=\"..\" min=\"..\"/>", sourceName );
            return 0;
        }
        return loadedCount;
    }

    bool ActionPlatformerCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( XMLReadUtil::isElement( name, "Stage" ) )
            return loadStage( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Combo" ) )
            return loadCombo( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Pattern" ) )
            return loadPattern( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Grading" ) )
            return loadGrading( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Body" ) )
            return GameDataXML::readSetting( node, sourceName, _bodySettings );
        if ( XMLReadUtil::isElement( name, "Parry" ) )
            return GameDataXML::readSetting( node, sourceName, _parryRules );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool ActionPlatformerCatalog::loadGrading( const XMLNode& node, string_view sourceName )
    {
        ActionGradingRules grading;
        bool               bValid = GameDataXML::readFields( node, grading, sourceName );
        for ( XMLNode child = node.findChild( "Grade" ); child; child = child.findNextSibling( "Grade" ) )
        {
            ActionGradeDef grade;
            if ( GameDataXML::readFields( child, grade, sourceName ) )
                grading._listGrade.push_back( grade );
            else
                bValid = false;
        }
        if ( grading._listGrade.empty() )
        {
            SW_LOG_ERROR( "%#: <Grading> needs at least one <Grade>", sourceName );
            bValid = false;
        }
        if ( grading._timeWeight + grading._hitWeight + grading._collectWeight <= 0.0f )
        {
            SW_LOG_ERROR( "%#: <Grading> weights (time, hits, collect) are all zero", sourceName );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        std::stable_sort( grading._listGrade.begin(), grading._listGrade.end(), ActionPlatformerCatalogInternal::isGradeHigher );
        _grading = grading;
        return true;
    }

    bool ActionPlatformerCatalog::loadStage( const XMLNode& node, string_view sourceName )
    {
        using Internal = ActionPlatformerCatalogInternal;
        ActionStageDef stage;
        if ( GameDataXML::readFields( node, stage, sourceName, { "checkpoints", "secrets" } ) == false )
            return false;
        XMLReadUtil::parseNameList( node.getAttributeText( "checkpoints" ), Internal::kNameSeparators, stage._listCheckpoint );
        XMLReadUtil::parseNameList( node.getAttributeText( "secrets" ), Internal::kNameSeparators, stage._listSecret );
        (void)_stageCatalog.add( stage ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    bool ActionPlatformerCatalog::loadCombo( const XMLNode& node, string_view sourceName )
    {
        using Internal = ActionPlatformerCatalogInternal;
        ActionComboDef combo;
        if ( GameDataXML::readFields( node, combo, sourceName, { "moves" } ) == false )
            return false;
        XMLReadUtil::parseNameList( node.getAttributeText( "moves" ), Internal::kNameSeparators, combo._listMove );
        if ( combo._listMove.empty() )
        {
            SW_LOG_ERROR( "%#: <Combo id=\"%#\"> needs moves=\"...\" with at least one move", sourceName, combo._id.c_str() );
            return false;
        }
        (void)_comboCatalog.add( combo );
        return true;
    }

    bool ActionPlatformerCatalog::loadPattern( const XMLNode& node, string_view sourceName )
    {
        ActionPatternDef pattern;
        bool             bValid = GameDataXML::readFields( node, pattern, sourceName );
        for ( XMLNode child = node.findChild( "State" ); child; child = child.findNextSibling( "State" ) )
        {
            ActionPatternStateDef state;
            if ( GameDataXML::readFields( child, state, sourceName ) )
                pattern._listState.push_back( state );
            else
                bValid = false;
        }
        if ( pattern._listState.empty() )
        {
            SW_LOG_ERROR( "%#: <Pattern id=\"%#\"> has no <State>", sourceName, pattern._id.c_str() );
            bValid = false;
        }
        if ( pattern._start.empty() == false && pattern.findStateIndex( pattern._start ) < 0 )
        {
            SW_LOG_ERROR( "%#: <Pattern id=\"%#\"> starts at an unknown state '%#'", sourceName, pattern._id.c_str(), pattern._start.c_str() );
            bValid = false;
        }
        // 이어지는 상태 이름이 틀리면 그 패턴을 넣지 않는다(테스트와 에디터가 바로 볼 수 있다).
        for ( const ActionPatternStateDef& state : pattern._listState )
        {
            const hashed_string arrTarget[] = { state._next, state._onNear, state._onHit };
            for ( const hashed_string& target : arrTarget )
            {
                if ( target.empty() == false && pattern.findStateIndex( target ) < 0 )
                {
                    SW_LOG_ERROR( "%#: pattern '%#' state '%#' goes to an unknown state '%#'", sourceName, pattern._id.c_str(), state._id.c_str(), target.c_str() );
                    bValid = false;
                }
            }
        }
        if ( bValid == false )
            return false;
        (void)_patternCatalog.add( pattern );
        return true;
    }
} // namespace sw
