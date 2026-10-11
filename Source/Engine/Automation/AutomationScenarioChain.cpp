#include "pch.h"

#include "Engine/Automation/AutomationScenarioChain.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    vector<string> AutomationScenarioChain::splitList( string_view text )
    {
        vector<string> listPath;
        size_t         begin = 0;
        while ( begin <= text.size() )
        {
            size_t end = text.find( ',', begin );
            if ( end == string_view::npos )
                end = text.size();
            string_view part = text.substr( begin, end - begin );
            while ( part.empty() == false && ( part.front() == ' ' || part.front() == '	' ) )
            {
                part.remove_prefix( 1 );
            }
            while ( part.empty() == false && ( part.back() == ' ' || part.back() == '	' ) )
            {
                part.remove_suffix( 1 );
            }
            if ( part.empty() == false )
                listPath.emplace_back( part );
            begin = end + 1;
        }
        return listPath;
    }

    string AutomationScenarioChain::makeStageReportPath( string_view reportPath, size_t stageIndex, size_t stageCount )
    {
        if ( reportPath.empty() || stageCount <= 1 )
            return string( reportPath );
        // 폴더 이름의 점이 확장자로 읽히지 않게 마지막 구분자 뒤에서만 확장자를 찾는다.
        const size_t nameBegin = reportPath.find_last_of( "/\\" );
        const size_t dot       = reportPath.find_last_of( '.' );
        const string suffix    = "." + to_string( stageIndex + 1 );
        if ( dot == string_view::npos || ( nameBegin != string_view::npos && dot < nameBegin ) )
            return string( reportPath ) + suffix;
        return string( reportPath.substr( 0, dot ) ) + suffix + string( reportPath.substr( dot ) );
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
        static const string s_empty;
        return _stageIndex < _listPath.size() && _bFinished == false ? _listPath[_stageIndex] : s_empty;
    }

    bool AutomationScenarioChain::onStageEnded( AutomationResult result )
    {
        if ( isActive() == false )
            return false;
        const bool bHasNext = _stageIndex + 1 < _listPath.size();
        if ( result != AutomationResult::Passed )
        {
            ++_failedStageCount;
            if ( _firstNonPassResult == AutomationResult::Passed )
                _firstNonPassResult = result;
            // 건너뜀은 이 기계가 못 도는 것이다 — 뒤 단계도 같다. 그 밖의 실패는 `keepGoing` 이면 넘어간다.
            if ( _bKeepGoing == false || result == AutomationResult::Skipped || bHasNext == false )
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
        _bFinished   = true;
        _finalResult = _firstNonPassResult;
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
