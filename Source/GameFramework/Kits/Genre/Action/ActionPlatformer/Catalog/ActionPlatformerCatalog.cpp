#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ActionPlatformerCatalogInternal
        {
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
            SW_LOG_ERROR_ID( "ActionPlatformerCatalog.NeedsGradingLeastOne", "%#: needs <Grading> with at least one <Grade id=\"..\" min=\"..\"/>", sourceName );
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
        SW_LOG_ERROR_ID( "ActionPlatformerCatalog.UnknownElementSkipped", "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool ActionPlatformerCatalog::loadGrading( const XMLNode& node, string_view sourceName )
    {
        ActionGradingRules grading;
        bool               bValid = XMLFields::readFields( node, grading, sourceName );
        for ( XMLNode child = node.findChild( "Grade" ); child; child = child.findNextSibling( "Grade" ) )
        {
            ActionGradeDef grade;
            if ( XMLFields::readFields( child, grade, sourceName ) )
                grading._listGrade.push_back( grade );
            else
                bValid = false;
        }
        if ( grading._listGrade.empty() )
        {
            SW_LOG_ERROR_ID( "ActionPlatformerCatalog.GradingNeedsLeastOne", "%#: <Grading> needs at least one <Grade>", sourceName );
            bValid = false;
        }
        if ( grading._timeWeight + grading._hitWeight + grading._collectWeight <= 0.0f )
        {
            SW_LOG_ERROR_ID( "ActionPlatformerCatalog.GradingWeightsTimeHits", "%#: <Grading> weights (time, hits, collect) are all zero", sourceName );
            bValid = false;
        }
        if ( bValid == false )
            return false;
        VectorUtil::stableSort( grading._listGrade, ActionPlatformerCatalogInternal::isGradeHigher );
        _grading = grading;
        return true;
    }

    bool ActionPlatformerCatalog::loadStage( const XMLNode& node, string_view sourceName )
    {
        ActionStageDef stage;
        if ( XMLFields::readFields( node, stage, sourceName ) == false )
            return false;
        // 비밀이 겹치면 다 모아도 수집 점수가 만점이 못 되고, 체크포인트가 겹치면 뒤의 것을 영영 지나지 못한다.
        for ( const vector<hashed_string>* pList : { &stage._listCheckpoint, &stage._listSecret } )
        {
            const hashed_string* pDuplicate = VectorUtil::findDuplicate( *pList );
            if ( pDuplicate != nullptr )
            {
                SW_LOG_ERROR_ID( "ActionPlatformerCatalog.StageDuplicateName", "%#: <Stage id=\"%#\"> lists '%#' twice", sourceName, stage._id.c_str(), pDuplicate->c_str() );
                return false;
            }
        }
        (void)_stageCatalog.add( stage );
        return true;
    }

    bool ActionPlatformerCatalog::loadCombo( const XMLNode& node, string_view sourceName )
    {
        ActionComboDef combo;
        if ( XMLFields::readFields( node, combo, sourceName ) == false )
            return false;
        if ( combo._listMove.empty() )
        {
            SW_LOG_ERROR_ID( "ActionPlatformerCatalog.ComboIdNeedsMoves", "%#: <Combo id=\"%#\"> needs moves=\"...\" with at least one move", sourceName, combo._id.c_str() );
            return false;
        }
        (void)_comboCatalog.add( combo );
        return true;
    }

    bool ActionPlatformerCatalog::loadPattern( const XMLNode& node, string_view sourceName )
    {
        ActionPatternDef pattern;
        bool             bValid = XMLFields::readFields( node, pattern, sourceName );
        for ( XMLNode child = node.findChild( "State" ); child; child = child.findNextSibling( "State" ) )
        {
            ActionPatternStateDef state;
            if ( XMLFields::readFields( child, state, sourceName ) )
                pattern._listState.push_back( state );
            else
                bValid = false;
        }
        if ( pattern._listState.empty() )
        {
            SW_LOG_ERROR_ID( "ActionPlatformerCatalog.PatternIdNoState", "%#: <Pattern id=\"%#\"> has no <State>", sourceName, pattern._id.c_str() );
            bValid = false;
        }
        if ( pattern._start.empty() == false && pattern.findStateIndex( pattern._start ) < 0 )
        {
            SW_LOG_ERROR_ID( "ActionPlatformerCatalog.PatternIdStartsUnknown", "%#: <Pattern id=\"%#\"> starts at an unknown state '%#'", sourceName, pattern._id.c_str(), pattern._start.c_str() );
            bValid = false;
        }
        // 같은 id 의 상태는 뒤의 것에 영영 가지 못한다.
        vector<hashed_string> listStateID;
        for ( const ActionPatternStateDef& state : pattern._listState )
        {
            listStateID.push_back( state._id );
        }
        const hashed_string* pDuplicateState = VectorUtil::findDuplicate( listStateID );
        if ( pDuplicateState != nullptr )
        {
            SW_LOG_ERROR_ID( "ActionPlatformerCatalog.PatternDuplicateState", "%#: <Pattern id=\"%#\"> has two states named '%#'", sourceName, pattern._id.c_str(), pDuplicateState->c_str() );
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
                    SW_LOG_ERROR_ID( "ActionPlatformerCatalog.PatternStateGoesUnknown", "%#: pattern '%#' state '%#' goes to an unknown state '%#'", sourceName, pattern._id.c_str(), state._id.c_str(), target.c_str() );
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

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<ActionPlatformerCatalog>::acquire`). 파일 규칙이 없는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( ActionPlatformerCatalog, ActionPlatformerCatalog, SW_KIT_API, AssetTypeDef::makeCacheOnly( "ActionPlatformerCatalog", AssetTypeLoader::of<ActionPlatformerCatalog>( MemoryTag::Game ) ) );
} // namespace sw
