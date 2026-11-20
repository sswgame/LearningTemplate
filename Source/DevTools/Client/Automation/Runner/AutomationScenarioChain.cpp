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
        , _listFailedStageLine{}
        , _listRepeatCount{}
        , _repeatedFailureLine{}
        , _repeatedFailureLimit{ 0 }
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
        StringBuilder<constant::buffer::kMax1024> path;
        path.appendFormat( "%#.%#%#", FileUtil::removeExtension( reportPath ), static_cast<uint64>( stageIndex + 1 ), FileUtil::getExtension( reportPath ) );
        return string( path.view() );
    }

    void AutomationScenarioChain::begin( vector<string> listPath, bool bKeepGoing, uint32 repeatedFailureLimit )
    {
        SW_ASSERT( repeatedFailureLimit > 0 ); // 정책 읽기(`AutomationRunPolicy::parse`)가 1 이상을 보장한다
        *this                 = AutomationScenarioChain{};
        _listPath             = std::move( listPath );
        _repeatedFailureLimit = repeatedFailureLimit;
        _bKeepGoing           = bKeepGoing;
        _bBegun               = SW_TRUE;
        _bFinished            = _listPath.empty();
        _finalResult          = _listPath.empty() ? AutomationResult::Passed : AutomationResult::Running;
    }

    const string& AutomationScenarioChain::getCurrentPath() const
    {
        return _stageIndex < _listPath.size() && _bFinished == false ? _listPath[_stageIndex] : AutomationScenarioChainInternal::s_emptyPath;
    }

    bool AutomationScenarioChain::onStageEnded( const AutomationRunResult& runResult, string_view scenarioName )
    {
        if ( isActive() == false )
            return false;
        const AutomationResult result = runResult.getResult();
        if ( runResult.isFailure() )
        {
            StringBuilder<constant::buffer::kMax256> line;
            line.appendFormat( "%#/%# %#: %#", static_cast<uint64>( _stageIndex + 1 ), static_cast<uint64>( _listPath.size() ), scenarioName, runResult.describe() );
            _listFailedStageLine.emplace_back( line.view() );
        }
        const bool bHasNext      = _stageIndex + 1 < _listPath.size();
        const bool bRepeatedStop = runResult.isFailure() && countRepeat( runResult );
        if ( _bKeepGoing && result == AutomationResult::Skipped )
        {
            ++_skippedResultCount; // 이 단계만 못 돈 것이다 — 실패가 아니고 뒤 단계는 돈다
        }
        else if ( result != AutomationResult::Passed )
        {
            ++_failedStageCount;
            if ( _firstNonPassResult == AutomationResult::Passed )
                _firstNonPassResult = result;
            if ( _bKeepGoing == false || bHasNext == false || bRepeatedStop )
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

    bool AutomationScenarioChain::countRepeat( const AutomationRunResult& runResult )
    {
        if ( runResult.getRepeatKey().empty() )
            return false;
        StringBuilder<constant::buffer::kMax256> keyBuilder;
        keyBuilder.appendFormat( "%# %#", getEnumName( runResult.getFailureKind() ), runResult.getRepeatKey() );
        const string key    = string( keyBuilder.view() );
        RepeatCount* pFound = nullptr;
        for ( RepeatCount& repeat : _listRepeatCount )
        {
            if ( repeat._key == key )
                pFound = &repeat;
        }
        if ( pFound == nullptr )
        {
            _listRepeatCount.push_back( RepeatCount{ key, 0 } );
            pFound = &_listRepeatCount.back();
        }
        ++pFound->_count;
        if ( pFound->_count < _repeatedFailureLimit )
            return false;
        StringBuilder<constant::buffer::kMax256> line;
        line.appendFormat( "%# - %# x%#", getEnumName( AutomationFailureKind::RepeatedFailure ), key, pFound->_count );
        _repeatedFailureLine = string( line.view() );
        line.append( " - chain stopped" );
        _listFailedStageLine.emplace_back( line.view() );
        return true;
    }

    string AutomationScenarioChain::describeStageEnd( size_t stageIndex, const AutomationRunResult& runResult, string_view scenarioName ) const
    {
        const AutomationResult                   result = runResult.getResult();
        StringBuilder<constant::buffer::kMax256> text;
        text.appendFormat( "chain stage %#/%# %# (%#)", static_cast<uint64>( stageIndex + 1 ), static_cast<uint64>( _listPath.size() ), getEnumName( result ), scenarioName );
        // 진 단계 · 건너뛴 단계는 판정 값 한 줄이 까닭이다 — 끝 단계 이름이 까닭으로 남는 길이 없다.
        if ( const string line = runResult.describe(); line.empty() == false )
            text.appendFormat( ": %#", line );
        if ( result != AutomationResult::Passed && _skippedStageCount > 0 )
            text.appendFormat( " - %# later stage(s) skipped", _skippedStageCount );
        if ( result != AutomationResult::Passed && _repeatedFailureLine.empty() == false && _bFinished == SW_TRUE )
            text.appendFormat( " - %#", _repeatedFailureLine );
        return string( text.view() );
    }
} // namespace sw
