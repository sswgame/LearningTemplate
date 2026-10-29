#include "pch.h"

#include "Engine/Automation/Runner/AutomationJudge.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Automation/AutomationCaptureUtil.h"
#include "Engine/Automation/AutomationRunTitle.h"
#include "Engine/Automation/Runner/AutomationExitCode.h"
#include "Engine/Automation/Runner/AutomationImageMetric.h"
#include "Engine/Automation/Runner/AutomationStepArgs.h"
#include "Engine/Automation/Scenario/AutomationEnvironmentSteps.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Scenario/AutomationStepValidation.h"
#include "Engine/Automation/Scenario/AutomationWindowSteps.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    AutomationJudge::AutomationJudge()
        : _logCapture{}
        , _logPolicy{}
        , _listFailure{}
        , _listUpdatedGolden{}
        , _goldenFolderID{}
        , _goldenPrefix{}
        , _listUnhandledLog{}
        , _listAllowedLogID{}
        , _listMetricLine{}
        , _listRecordLine{}
        , _listWatch{}
        , _listRemembered{}
    {
    }

    void AutomationJudge::runExpect( AutomationStepContext& context, const AutomationStep& step )
    {
        bool   bMatched = false;
        string message;
        if ( compareWatch( context, step, bMatched, message ) == false )
            context.recordFailure( step, message );
        else if ( bMatched == false )
            context.recordFailure( step, "Expect " + message );
    }

    bool AutomationJudge::readWatchValue( const AutomationStepContext& context, const AutomationStep& step, AutomationWatchValue& outValue, string& outMessage ) const
    {
        const string&                      watchName     = *step.findAttribute( "watch" );
        const AutomationWatchRegistration* pRegistration = AutomationWatchRegistry::find( watchName );
        // 모듈을 언로드했다 로드하면 감시 값이 빠질 수 있다. 시작 때 있었어도 다시 찾는다.
        if ( pRegistration == nullptr )
        {
            outMessage = watchName + " is no longer registered";
            return false;
        }
        const string*              pTarget = step.findAttribute( "target" );
        const string*              pKey    = step.findAttribute( "key" );
        const AutomationWatchQuery query   = AutomationWatchQuery::make( context.findActiveObjectManager(), pTarget != nullptr ? string_view{ *pTarget } : string_view{},
                                                                       pKey != nullptr ? string_view{ *pKey } : string_view{} );
          string subject = watchName;
        if ( pTarget != nullptr )
            subject += "(" + *pTarget + ( pKey != nullptr ? ", " + *pKey : string() ) + ")";
        else if ( pKey != nullptr )
            subject += "(" + *pKey + ")";
        if ( AutomationWatchRegistry::read( *pRegistration, query, outValue ) == false )
        {
            const string reason = outValue._bText && outValue._text.empty() == false ? outValue._text : string( "no target in the scene" );
            outMessage          = subject + " has no value (" + reason + ")";
            return false;
        }
        outMessage = subject;
        return true;
    }

    bool AutomationJudge::compareWatch( const AutomationStepContext& context, const AutomationStep& step, bool& outMatched, string& outMessage ) const
    {
        using Internal = AutomationStepArgsUtil;
        AutomationWatchValue value{};
        outMatched = false;
        if ( readWatchValue( context, step, value, outMessage ) == false )
            return false;
        const string valueText = Internal::makeValueText( value );
        const string subject   = outMessage;
        bool         bOk       = true;
        string       rule;
        const bool   bNumberRule = step.findAttribute( "is" ) == nullptr && step.findAttribute( "sameAs" ) == nullptr && step.findAttribute( "differsFrom" ) == nullptr;
        if ( bNumberRule && value._bText )
        {
            outMessage = subject + " is text ('" + value._text + "') - compare it with is=\"…\"";
            return false;
        }
        const string* pSameAs      = step.findAttribute( "sameAs" );
        const string* pDiffersFrom = step.findAttribute( "differsFrom" );
        if ( pSameAs != nullptr || pDiffersFrom != nullptr )
        {
            // 기억한 값과는 글 그대로 비교한다 — 64 비트 해시 같은 값이 수 비교(float32)에서 뭉개지지 않는다.
            const string& name        = pSameAs != nullptr ? *pSameAs : *pDiffersFrom;
            const string* pRemembered = nullptr;
            for ( const RememberedValue& remembered : _listRemembered )
            {
                if ( remembered._name == name )
                    pRemembered = &remembered._text;
            }
            if ( pRemembered == nullptr )
            {
                outMessage = subject + ": nothing was remembered as '" + name + "' (add <Remember as=\"" + name + "\"> before this step)";
                return false;
            }
            bOk        = pSameAs != nullptr ? valueText == *pRemembered : valueText != *pRemembered;
            outMatched = bOk;
            outMessage = subject + ( pSameAs != nullptr ? " same as " : " differs from " ) + name + " ('" + *pRemembered + "'), got " + valueText;
            return true;
        }
        if ( step.findAttribute( "is" ) != nullptr )
        {
            bOk  = valueText == *step.findAttribute( "is" );
            rule = "is '" + *step.findAttribute( "is" ) + "'";
        }
        else
        {
            Internal::NumberComparison comparison;
            string                     error;
            if ( Internal::parseNumberComparison( step, Internal::kWatchNearTolerance, comparison, error ) == false )
            {
                outMessage = error; // 검사 뒤 단계가 바뀌었다(핫 리로드)
                return false;
            }
            bOk = Internal::matchesNumber( comparison, value._number, rule );
        }
        outMatched = bOk;
        outMessage = subject + " " + rule + ", got " + valueText;
        return true;
    }

    void AutomationJudge::addRecordLine( string line )
    {
        SW_LOG_INFO( "[Scenario] record %#", line.c_str() );
        _listRecordLine.push_back( std::move( line ) );
    }

    void AutomationJudge::prepareWatches( const AutomationScenario& scenario )
    {
        _listWatch.clear();
        for ( const AutomationStep& step : scenario.getSteps() )
        {
            if ( string_view{ step._kind } != "ExpectVisited" )
                continue;
            const string* pTarget = step.findAttribute( "target" );
            const string* pKey    = step.findAttribute( "key" );
            WatchHistory  watch{};
            watch._watch  = *step.findAttribute( "watch" );
            watch._target = pTarget != nullptr ? *pTarget : string();
            watch._key    = pKey != nullptr ? *pKey : string();
            bool bKnown   = false;
            for ( const WatchHistory& existing : _listWatch )
            {
                bKnown = bKnown || ( existing._watch == watch._watch && existing._target == watch._target && existing._key == watch._key );
            }
            if ( bKnown == false )
                _listWatch.push_back( std::move( watch ) );
        }
    }

    void AutomationJudge::sampleWatches( AutomationStepContext& context )
    {
        for ( WatchHistory& watch : _listWatch )
        {
            AutomationStep step{};
            step._kind = "ExpectVisited";
            step._listAttribute.push_back( AutomationAttribute{ "watch", watch._watch } );
            if ( watch._target.empty() == false )
                step._listAttribute.push_back( AutomationAttribute{ "target", watch._target } );
            if ( watch._key.empty() == false )
                step._listAttribute.push_back( AutomationAttribute{ "key", watch._key } );
            AutomationWatchValue value{};
            string               message;
            if ( readWatchValue( context, step, value, message ) == false )
                continue;
            string text = AutomationStepArgsUtil::makeValueText( value );
            if ( watch._listValue.empty() == false && watch._listValue.back() == text )
                continue;
            watch._listValue.push_back( std::move( text ) );
            watch._listFrame.push_back( context.getFrameIndex() );
        }
    }

    void AutomationJudge::runExpectVisited( AutomationStepContext& context, const AutomationStep& step )
    {
        const string*       pTarget = step.findAttribute( "target" );
        const string*       pKey    = step.findAttribute( "key" );
        const string_view   watch   = *step.findAttribute( "watch" );
        const string_view   target  = pTarget != nullptr ? string_view{ *pTarget } : string_view{};
        const string_view   key     = pKey != nullptr ? string_view{ *pKey } : string_view{};
        const WatchHistory* pWatch  = nullptr;
        for ( const WatchHistory& history : _listWatch )
        {
            const bool bSame = history._watch == watch && history._target == target && history._key == key;
            if ( bSame )
                pWatch = &history;
        }
        string error;
        uint32 since = 0; // 적지 않으면 처음부터 본다
        if ( AutomationStepArgsUtil::readUint( step, "since", since, error ) == false )
        {
            context.recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
            return;
        }
        const string*  pIs = step.findAttribute( "is" );
        vector<string> listWant;
        if ( pIs != nullptr )
        {
            listWant.push_back( *pIs );
        }
        else
        {
            // `sequence` 는 쉼표로 가른 값 목록이다(앞뒤 공백을 떼고 빈 항목은 버린다).
            for ( const string_view value : StringUtil::split( *step.findAttribute( "sequence" ), ",", { ._bTrim = true, ._bDropEmpty = true } ) )
            {
                listWant.emplace_back( value );
            }
        }
        // `since` 프레임에 이미 들어 있던 값(그 앞의 마지막 바뀜)도 거친 것으로 본다.
        size_t first = 0;
        for ( size_t index = 0; pWatch != nullptr && index < pWatch->_listFrame.size(); ++index )
        {
            if ( pWatch->_listFrame[index] <= since )
                first = index;
        }
        size_t wantIndex = 0;
        string seen;
        for ( size_t index = first; pWatch != nullptr && index < pWatch->_listValue.size(); ++index )
        {
            seen += seen.empty() ? "" : " > ";
            seen += pWatch->_listValue[index] + "@" + to_string( pWatch->_listFrame[index] );
            if ( wantIndex < listWant.size() && pWatch->_listValue[index] == listWant[wantIndex] )
                ++wantIndex;
        }
        if ( wantIndex == listWant.size() )
            return;
        string want;
        for ( const string& value : listWant )
        {
            want += want.empty() ? string( value ) : " > " + value;
        }
        string subject = string( watch );
        if ( target.empty() == false || key.empty() == false )
            subject += "(" + string( target ) + ( target.empty() || key.empty() ? "" : ", " ) + string( key ) + ")";
        context.recordFailure( step, subject + " never went through '" + want + "' since frame " + to_string( since ) + " (missing '" + listWant[wantIndex] +
                                         "'), saw: " + ( seen.empty() ? string( "no value" ) : seen ) );
    }

    void AutomationJudge::runRecord( AutomationStepContext& context, const AutomationStep& step )
    {
        AutomationWatchValue value{};
        string               message;
        if ( readWatchValue( context, step, value, message ) == false )
        {
            context.recordFailure( step, "Record " + message );
            return;
        }
        addRecordLine( "frame " + to_string( context.getFrameIndex() ) + " " + message + " = " + AutomationStepArgsUtil::makeValueText( value ) );
    }

    void AutomationJudge::runRemember( AutomationStepContext& context, const AutomationStep& step )
    {
        AutomationWatchValue value{};
        string               message;
        if ( readWatchValue( context, step, value, message ) == false )
        {
            context.recordFailure( step, "Remember " + message );
            return;
        }
        const string& name = *step.findAttribute( "as" );
        string        text = AutomationStepArgsUtil::makeValueText( value );
        SW_LOG_INFO( "[Scenario] frame %# remember %# = %# (%#)", context.getFrameIndex(), name.c_str(), text.c_str(), message.c_str() );
        for ( RememberedValue& remembered : _listRemembered )
        {
            if ( remembered._name == name )
            {
                remembered._text = std::move( text );
                return;
            }
        }
        _listRemembered.push_back( RememberedValue{ name, std::move( text ) } );
    }

    void AutomationJudge::runExpectLog( AutomationStepContext& context, const AutomationStep& step )
    {
        const string*                         pID     = step.findAttribute( "id" );
        const string&                         needle  = pID != nullptr ? *pID : *step.findAttribute( "contains" ); // 검사 함수가 둘 중 하나를 요구했다
        const utf8*                           pTarget = pID != nullptr ? "ExpectLog id '" : "ExpectLog '";
        string                                error;
        AutomationStepArgsUtil::ExpectLogArgs args;
        if ( AutomationStepArgsUtil::parseExpectLog( step, args, error ) == false )
        {
            context.recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
            return;
        }
        const uint32 matchCount = pID != nullptr ? _logCapture.countLinesWithID( needle, args._since ) : _logCapture.countMatchingLines( needle, args._since );
        if ( args._bExactCount )
        {
            if ( matchCount != args._expected )
                context.recordFailure( step, formatToString( "%#%#' count == %#, got %#", string( pTarget ), needle, args._expected, matchCount ) );
            return;
        }
        if ( matchCount < args._expected )
            context.recordFailure( step, formatToString( "%#%#' count >= %#, got %#", string( pTarget ), needle, args._expected, matchCount ) );
    }

    void AutomationJudge::runExpectImage( AutomationStepContext& context, const AutomationStep& step )
    {
        string                                  error;
        AutomationStepArgsUtil::ExpectImageArgs args;
        if ( AutomationStepArgsUtil::parseExpectImage( step, args, error ) == false )
        {
            context.recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
            return;
        }
        const string&   file = *step.findAttribute( "file" );
        AutomationImage image;
        AutomationImage reduced;
        if ( AutomationImageMetric::loadPpm( context.resolveOutputPath( file ), image, error ) == false || AutomationImageMetric::reduce( image, args._region, reduced, error ) == false )
        {
            context.recordFailure( step, "could not read the image: " + error );
            return;
        }
        // 골든은 백엔드마다 따로다(래스터 · 정밀도 차가 백엔드마다 다르다). 시나리오 안에서 백엔드를 바꿀 수 있어 단언하는 때의 백엔드를 쓴다.
        static constexpr const utf8* kArrBackendTag[] = { "dx11", "dx12", "vk", "gl" };
        // 백엔드는 감시 값 `RHI.Backend`(App 의 교체기가 올린다)로 읽는다 — 판정이 RHI 를 직접 알지 않는다. 없으면(RHI 없는 호스트) 이미지 단언을 할 수 없다.
        const AutomationWatchRegistration* pBackendWatch = AutomationWatchRegistry::find( "RHI.Backend" );
        float64                            backendValue  = -1.0;
        if ( pBackendWatch == nullptr || pBackendWatch->_pFunction == nullptr || pBackendWatch->_pFunction( nullptr, backendValue ) == false )
        {
            context.recordFailure( step, "the RHI backend is unknown (watch RHI.Backend) - an image assertion needs a rendering host" );
            return;
        }
        const uint32    backendIndex = static_cast<uint32>( backendValue );
        const utf8*     pBackendTag  = backendIndex < std::size( kArrBackendTag ) ? kArrBackendTag[backendIndex] : "unknown";
        const string    goldenID     = _goldenFolderID + "/" + _goldenPrefix + "." + args._golden + "." + pBackendTag + ".ppm";
        vector<uint8>   listBytes;
        AutomationImage golden;
        const bool      bHasGolden = AssetIO::hasAsset( AssetID{ goldenID } ) && AssetIO::readBinaryAsset( AssetID{ goldenID }, listBytes ) &&
                                AutomationImageMetric::parsePpm( listBytes.data(), listBytes.size(), golden, error );
        AutomationImageDifference difference;
        const bool                bCompared = bHasGolden && AutomationImageMetric::compare( reduced, golden, difference, error );
        const bool                bMatches  = bCompared && difference._differingFraction <= args._tolerance;
        if ( bCompared )
        {
            string line = file + " vs " + goldenID + ": " + AutomationStepArgsUtil::formatNumber( difference._differingFraction * 100.0 ) + "% differing (tolerance " +
                          AutomationStepArgsUtil::formatNumber( args._tolerance * 100.0 ) + "%), mean distance " + AutomationStepArgsUtil::formatNumber( difference._meanDistance );
            SW_LOG_INFO( "[Scenario] image %#", line.c_str() );
            _listMetricLine.push_back( std::move( line ) );
        }
        if ( bMatches )
            return;
        if ( _bUpdateGolden )
        {
            // 골든 쓰기는 이 모드에서만 연다(`AutomationScratch` 의 쓰기 문 — 평소에는 리소스 루트에 쓰지 못한다). 쓴 파일은 보고에 남아 리뷰한다.
            vector<uint8> listEncoded;
            AutomationImageMetric::encodePpm( reduced, listEncoded );
            const string path = AssetPaths::resolveWritePath( goldenID );
            if ( path.empty() || FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( path ) ) == false || FileUtil::writeFile( path, listEncoded.data(), listEncoded.size() ) == false )
            {
                context.recordFailure( step, "could not write the golden " + goldenID );
                return;
            }
            SW_LOG_INFO( "[Scenario] golden %# %#", bHasGolden ? "updated" : "recorded", goldenID.c_str() );
            _listUpdatedGolden.push_back( goldenID );
            return;
        }
        if ( bHasGolden == false )
        {
            context.recordFailure( step, "no golden " + goldenID + " yet - run with -scenario-update-golden and review the image" );
            return;
        }
        if ( bCompared == false )
        {
            context.recordFailure( step, error );
            return;
        }
        context.recordFailure( step, file + " differs from " + goldenID + ": " + AutomationStepArgsUtil::formatNumber( difference._differingFraction * 100.0 ) +
                                         "% of pixels differ (tolerance " + AutomationStepArgsUtil::formatNumber( args._tolerance * 100.0 ) + "%)" );
    }

    void AutomationJudge::applyLogGate( AutomationOutcome& inOutResult, string& outReason )
    {
        AutomationLogJudgement judgement = _logCapture.judgeLines( _logPolicy );
        if ( engine::areEngineServicesBound() )
        {
            for ( const string& key : engine::getSubsystem<LocalizationManager>().getMissingKeys() )
            {
                const string line         = "Missing localized text for key '" + key + "'";
                const bool   bAlreadySeen = _logCapture.containsLine( line ); // 시나리오 구간에 남은 경고 줄은 위에서 이미 셌다
                if ( bAlreadySeen || _logPolicy.isDeclared( line ) )
                    continue;
                ++judgement._missingIDCount; // 이 줄은 합성이라 id 가 없다 — `<ExpectLog>` 로만 가릴 수 있다
                judgement._listUnhandled.push_back( "[Warning] [LocalizationManager] (no log id) " + line + " (before the scenario started)" );
            }
        }
        _listAllowedLogID = std::move( judgement._listAllowedID );
        if ( judgement.hasUnhandled() == false )
            return;
        _listUnhandledLog = std::move( judgement._listUnhandled );
        // 이미 진 실행은 결과를 바꾸지 않는다(원인은 단언 · 시한이다). 줄은 보고의 `unhandledLogs` 에만 남겨, 진 보고도 경고 · 오류 줄을 숨기지 않게 한다.
        if ( inOutResult != AutomationOutcome::Passed )
            return;
        for ( const string& line : _listUnhandledLog )
        {
            _listFailure.push_back( "unhandled log: " + line );
        }
        inOutResult = AutomationOutcome::UnhandledLog;
        outReason   = to_string( _listUnhandledLog.size() ) + " unhandled Warning/Error log line(s), " + to_string( judgement._missingIDCount ) +
                    " without a log id - fix the cause, declare the line with <ExpectLog>, or allow its log id in this scenario with <AllowLog id=\"...\" reason=\"...\"/>"
                    " (a line without a log id needs SW_LOG_WARNING_ID / SW_LOG_ERROR_ID first)";
    }

    void AutomationJudge::addFailure( const AutomationStep& step, string_view message )
    {
        string line = step.describe() + ": " + string( message );
        SW_LOG_WARNING( "[Scenario] %#", line.c_str() );
        _listFailure.push_back( std::move( line ) );
    }

    bool AutomationJudge::prepare( const AutomationScenario& scenario, string& outError )
    {
        (void)outError; // 읽기는 시나리오 파일을 읽을 때 끝났다 — 허용은 그 파일의 `<AllowLog>` 다
        _logPolicy.clear();
        for ( const AutomationAttribute& allowed : scenario.getTestOptions()._listAllowedLog )
        {
            _logPolicy.allowID( allowed._name );
        }
        for ( const AutomationStep& step : scenario.getSteps() )
        {
            if ( step._kind != "ExpectLog" )
                continue;
            AutomationStepArgsUtil::ExpectLogArgs args;
            string                                argsError;
            const bool                            bParsed = AutomationStepArgsUtil::parseExpectLog( step, args, argsError );
            const bool                            bAbsent = bParsed && args._bExactCount && args._expected == 0;
            if ( bAbsent )
                continue; // count="0" 은 "이 줄이 없어야 한다" 는 단언이지 선언이 아니다 — 그 줄이 나오면 단언과 로그 게이트가 모두 진다
            if ( const string* pID = step.findAttribute( "id" ); pID != nullptr )
                _logPolicy.declareExpectedID( *pID );
            else
                _logPolicy.declareExpected( *step.findAttribute( "contains" ) ); // 검사 함수가 id · contains 중 하나를 요구했다
        }
        prepareWatches( scenario );
        _listRemembered.clear();
        _listUpdatedGolden.clear();
        _goldenFolderID = FileUtil::getDirectoryPart( scenario.getSourcePath() ) + "/golden";
        _goldenPrefix   = scenario.getName();
        _bUpdateGolden  = false;
        if ( engine::areEngineServicesBound() )
            (void)engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::SCENARIO_UPDATE_GOLDEN, _bUpdateGolden );
        _logCapture.start();
        return true;
    }
} // namespace sw
