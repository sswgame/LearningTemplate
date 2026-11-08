#include "pch.h"

#include "Engine/Automation/Runner/AutomationLogPolicy.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Automation/Runner/AutomationRunnerParams.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"

namespace sw
{
    void AutomationLogPolicy::prepare( const AutomationScenario& scenario )
    {
        // 목록은 잠금 밖에서 짓고 바꿔 넣기만 잠근다 — 단계 읽기가 로그를 남겨도 이 잠금을 쥔 채 로거에 들어가지 않는다.
        vector<string> listAllowedID;
        for ( const AutomationAttribute& allowed : scenario.getTestOptions()._listAllowedLog )
        {
            if ( allowed._name.empty() == false )
                listAllowedID.push_back( allowed._name );
        }
        vector<string> listExpectedID;
        for ( const AutomationStep& step : scenario.getSteps() )
        {
            if ( step._kind != "ExpectLog" )
                continue;
            AutomationExpectLogParams params;
            string                    readError;
            const bool                bAbsent = step.readFields( params, readError ) && params._count == 0;
            if ( bAbsent )
                continue;                                   // count="0" 은 단언이지 선언이 아니다
            const string& id = *step.findAttribute( "id" ); // 검사 함수가 id 를 요구했다
            if ( id.empty() == false )
                listExpectedID.push_back( id );
        }
        ScopedLock<mutex> lock{ _mutex };
        _listExpectedID = std::move( listExpectedID );
        _listAllowedID  = std::move( listAllowedID );
    }

    void AutomationLogPolicy::clear()
    {
        ScopedLock<mutex> lock{ _mutex };
        _listExpectedID.clear();
        _listAllowedID.clear();
    }

    bool AutomationLogPolicy::isExemptMessage( string_view message )
    {
        return StringUtil::startsWith( message, kRunnerPrefix );
    }

    bool AutomationLogPolicy::isDeclaredIDLocked( string_view id ) const
    {
        return id.empty() == false && VectorUtil::containsIf( _listExpectedID, [&]( const string& expected )
        { return string_view( expected ) == id; } );
    }

    AutomationLogResult AutomationLogPolicy::classify( LogLevel level, string_view id, string_view message ) const
    {
        if ( isGatedLevel( level ) == false || isExemptMessage( message ) )
            return AutomationLogResult::NotGated;
        ScopedLock<mutex> lock{ _mutex };
        // 선언은 id 로만 한다(문장은 다른 줄까지 가리거나 문구를 고치면 낡는다).
        if ( isDeclaredIDLocked( id ) )
            return AutomationLogResult::Declared;
        if ( id.empty() )
            return AutomationLogResult::MissingID;
        return VectorUtil::contains( _listAllowedID, string( id ) ) ? AutomationLogResult::Allowed : AutomationLogResult::NotAllowed;
    }

    void AutomationLogPolicy::judge( LogLevel level, string_view id, string_view message, AutomationLogJudgement& inoutJudgement ) const
    {
        const AutomationLogResult result = classify( level, id, message );
        if ( result == AutomationLogResult::Allowed )
        {
            const string idText{ id };
            if ( VectorUtil::contains( inoutJudgement._listAllowedID, idText ) == false )
                inoutJudgement._listAllowedID.push_back( idText );
            return;
        }
        if ( result != AutomationLogResult::MissingID && result != AutomationLogResult::NotAllowed )
            return;
        const bool bMissingID = result == AutomationLogResult::MissingID;
        if ( bMissingID )
            ++inoutJudgement._missingIDCount;
        else
            ++inoutJudgement._notAllowedCount;
        const string idText = bMissingID ? string( "(no log id)" ) : "(id=" + string( id ) + ")";
        inoutJudgement._listUnhandled.push_back( string( level == LogLevel::Error ? "[Error] " : "[Warning] " ) + idText + " " + string( message ) );
    }
} // namespace sw
