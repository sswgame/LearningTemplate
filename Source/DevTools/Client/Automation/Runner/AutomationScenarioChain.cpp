#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationScenarioChain.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/EnumName.h"

namespace sw
{
    namespace
    {
        struct AutomationScenarioChainInternal
        {
            /**
             * @brief 진행 중인 단계가 없을 때 돌려주는 빈 경로입니다.
             * @details 이미지 적재 때 선다 — 함수 안 정적 변수면 처음 부른 때(기준선 뒤)에 Debug STL 프록시를 잡아 종료 누수 보고에 남는다.
             */
            static inline const string s_emptyPath{};
        };
    } // namespace

    AutomationScenarioChain::AutomationScenarioChain()
        : _listPath{}
        , _stageIndex{ 0 }
        , _skippedStageCount{ 0 }
        , _failedStageCount{ 0 }
        , _skippedResultCount{ 0 }
        , _firstNonPassResult{ AutomationResult::Passed }
        , _finalResult{ AutomationResult::Running }
        , _bKeepGoing{ SW_FALSE }
        , _bBegun{ SW_FALSE }
        , _bFinished{ SW_FALSE }
    {
    }

    vector<string> AutomationScenarioChain::splitList( string_view text )
    {
        vector<string> listPath;
        for ( const string_view path : StringUtil::split( text, ",", { ._bTrim = true, ._bDropEmpty = true } ) )
        {
            listPath.emplace_back( path );
        }
        return listPath;
    }

    string AutomationScenarioChain::makeStageReportPath( string_view reportPath, size_t stageIndex, size_t stageCount )
    {
        if ( reportPath.empty() || stageCount <= 1 )
            return string( reportPath );
        // 확장자 앞에 단계 번호를 넣는다(폴더 이름의 점은 FileUtil 이 확장자로 읽지 않는다).
        return FileUtil::removeExtension( reportPath ) + "." + to_string( stageIndex + 1 ) + FileUtil::getExtension( reportPath );
    }

    void AutomationScenarioChain::begin( vector<string> listPath, bool bKeepGoing )
    {
        *this        = AutomationScenarioChain{};
        _listPath    = std::move( listPath );
        _bKeepGoing  = bKeepGoing;
        _bBegun      = SW_TRUE;
        _bFinished   = _listPath.empty();
        _finalResult = _listPath.empty() ? AutomationResult::Passed : AutomationResult::Running;
    }

    const string& AutomationScenarioChain::getCurrentPath() const
    {
        return _stageIndex < _listPath.size() && _bFinished == false ? _listPath[_stageIndex] : AutomationScenarioChainInternal::s_emptyPath;
    }

    bool AutomationScenarioChain::onStageEnded( AutomationResult result )
    {
        if ( isActive() == false )
            return false;
        const bool bHasNext = _stageIndex + 1 < _listPath.size();
        if ( _bKeepGoing && result == AutomationResult::Skipped )
        {
            ++_skippedResultCount; // 이 단계만 못 돈 것이다 — 실패가 아니고 뒤 단계는 돈다
        }
        else if ( result != AutomationResult::Passed )
        {
            ++_failedStageCount;
            if ( _firstNonPassResult == AutomationResult::Passed )
                _firstNonPassResult = result;
            if ( _bKeepGoing == false || bHasNext == false )
            {
                _bFinished         = SW_TRUE;
                _finalResult       = _firstNonPassResult;
                _skippedStageCount = _listPath.size() - _stageIndex - 1;
                return false;
            }
        }
        if ( bHasNext )
        {
            ++_stageIndex;
            return true;
        }
        _bFinished = SW_TRUE;
        // 실패가 없고 모든 단계가 건너뜀이면 체인도 건너뜀이다(돌린 것이 하나도 없다).
        _finalResult = _firstNonPassResult != AutomationResult::Passed ? _firstNonPassResult
                     : _skippedResultCount == _listPath.size()         ? AutomationResult::Skipped
                                                                       : AutomationResult::Passed;
        return false;
    }

    string AutomationScenarioChain::describeStageEnd( size_t stageIndex, AutomationResult result, string_view scenarioName ) const
    {
        string text = "chain stage " + to_string( stageIndex + 1 ) + "/" + to_string( _listPath.size() ) + " " + getEnumName( result ) + " (" +
                      string( scenarioName ) + ")";
        if ( result != AutomationResult::Passed && _skippedStageCount > 0 )
        {
            StringBuilder<constant::buffer::kMax256> textPart;
            textPart.appendFormat( " - %# later stage(s) skipped", _skippedStageCount );
            text += textPart.c_str();
        }
        return text;
    }
} // namespace sw
