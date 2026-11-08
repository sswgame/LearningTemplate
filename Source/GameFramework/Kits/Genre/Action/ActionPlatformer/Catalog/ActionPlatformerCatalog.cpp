#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

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

    optional<uint32> ActionPatternDef::findStateIndex( const hashed_string& stateID ) const
    {
        for ( size_t index = 0; index < _listState.size(); ++index )
        {
            if ( _listState[index]._id == stateID )
                return static_cast<uint32>( index );
        }
        return nullopt;
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
        using Element                          = XMLCatalogElement<ActionPlatformerCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&ActionPlatformerCatalog::loadStage>( "Stage" ),
            Element::of<&ActionPlatformerCatalog::loadCombo>( "Combo" ),
            Element::of<&ActionPlatformerCatalog::loadPattern>( "Pattern" ),
            Element::of<&ActionPlatformerCatalog::loadGrading>( "Grading" ),
            Element::of<&ActionPlatformerCatalog::_bodySettings>( "Body" ),
            Element::of<&ActionPlatformerCatalog::_parryRules>( "Parry" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        // 등급표는 점수를 등급으로 바꾸는 유일한 근거다 — 코드가 대신 정한 표를 쓰지 않는다.
        if ( _grading._listGrade.empty() )
        {
            SW_LOG_ERROR_ID( "ActionPlatformerCatalog.NeedsGradingLeastOne", "%#: needs <Grading> with at least one <Grade id=\"..\" min=\"..\"/>", sourceName );
            return 0;
        }
        return result._loadedCount;
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
        if ( pattern._start.empty() == false && pattern.findStateIndex( pattern._start ).has_value() == false )
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
                if ( target.empty() == false && pattern.findStateIndex( target ).has_value() == false )
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
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<ActionPlatformerCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( ActionPlatformerCatalog, ActionPlatformerCatalog, SW_KIT_API, ActionPlatformerCatalog::kAssetType.withLoader( AssetTypeLoader::of<ActionPlatformerCatalog>( MemoryTag::Game ) ) );
} // namespace sw
