#include "pch.h"

#include "Engine/Automation/AutomationScenarioChain.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

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

    vector<string> AutomationScenarioChain::splitList( string_view text )
    {
        vector<string> listPath;
        StringUtil::forEachToken( text, ",",
                                  [&listPath]( string_view token )
        {
            const string_view path = StringUtil::trim( token );
            if ( path.empty() == false )
                listPath.emplace_back( path );
        } );
        return listPath;
    }

    string AutomationScenarioChain::makeStageReportPath( string_view reportPath, size_t stageIndex, size_t stageCount )
    {
        if ( reportPath.empty() || stageCount <= 1 )
            return string( reportPath );
        // 폴더 이름의 점이 확장자로 읽히지 않게 마지막 구분자 뒤에서만 확장자를 찾는다.
        string_view directory;
        string_view fileName;
        FileUtil::splitPath( reportPath, directory, fileName );
        const size_t dot    = fileName.find_last_of( '.' );
        const string suffix = "." + to_string( stageIndex + 1 );
        if ( dot == string_view::npos )
            return string( reportPath ) + suffix;
        const size_t extensionBegin = directory.size() + dot;
        return string( reportPath.substr( 0, extensionBegin ) ) + suffix + string( reportPath.substr( extensionBegin ) );
    }

    void AutomationScenarioChain::begin( vector<string> listPath, bool bKeepGoing )
    {
        *this        = AutomationScenarioChain{};
        _listPath    = std::move( listPath );
        _bKeepGoing  = bKeepGoing;
        _bBegun      = true;
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
                _bFinished         = true;
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
        _bFinished = true;
        // 실패가 없고 모든 단계가 건너뜀이면 체인도 건너뜀이다(돌린 것이 하나도 없다).
        _finalResult = _firstNonPassResult != AutomationResult::Passed ? _firstNonPassResult
                     : _skippedResultCount == _listPath.size()         ? AutomationResult::Skipped
                                                                       : AutomationResult::Passed;
        return false;
    }

    string AutomationScenarioChain::describeStageEnd( size_t stageIndex, AutomationResult result, string_view scenarioName ) const
    {
        string text = "chain stage " + to_string( stageIndex + 1 ) + "/" + to_string( _listPath.size() ) + " " + AutomationRunner::getResultName( result ) + " (" +
                      string( scenarioName ) + ")";
        if ( result != AutomationResult::Passed && _skippedStageCount > 0 )
            text += " - " + to_string( _skippedStageCount ) + " later stage(s) skipped";
        return text;
    }
} // namespace sw
