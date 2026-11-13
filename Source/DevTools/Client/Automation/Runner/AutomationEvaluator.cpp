#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationEvaluator.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include "DevTools/Client/Automation/Runner/AutomationExitCode.h"
#include "DevTools/Client/Automation/Runner/AutomationGoldenUtil.h"
#include "DevTools/Client/Automation/Runner/AutomationImage.h"
#include "DevTools/Client/Automation/Runner/AutomationRunTitle.h"
#include "DevTools/Client/Automation/Runner/AutomationStepArgs.h"
#include "DevTools/Client/Automation/Scenario/AutomationEnvironmentSteps.h"
#include "DevTools/Client/Automation/Scenario/AutomationStepValidation.h"
#include "DevTools/Client/Automation/Scenario/AutomationWindowSteps.h"

#include "Engine/Automation/AutomationCaptureUtil.h"
#include "Engine/Automation/Scenario/AutomationRunnerParams.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    AutomationEvaluator::AutomationEvaluator( shared_ptr<AutomationLogPolicy> pLogPolicy )
        : _logCapture{}
        , _pLogPolicy{ std::move( pLogPolicy ) }
        , _listFailure{}
        , _listUpdatedGolden{}
        , _listNearLimitGolden{}
        , _listGoldenSample{}
        , _goldenPolicy{ unexpected( Error{ ErrorKind::NotFound, "the golden policy is read when a scenario starts" } ) }
        , _goldenFolderID{}
        , _goldenPrefix{}
        , _listUnhandledLog{}
        , _listAllowedLogID{}
        , _listMetricLine{}
        , _listRecordLine{}
        , _listWatch{}
        , _listRemembered{}
    {
        SW_ASSERT( _pLogPolicy != nullptr );
    }

    AutomationEvaluator::~AutomationEvaluator()
    {
        _pLogPolicy->clear();
    }

    void AutomationEvaluator::runExpect( AutomationStepContext& context, const AutomationStep& step )
    {
        bool   bMatched = false;
        string message;
        if ( compareWatch( context, step, bMatched, message ) == false )
            context.recordFailure( step, message );
        else if ( bMatched == false )
            context.recordFailure( step, "Expect " + message );
    }

    bool AutomationEvaluator::readWatchValue( const AutomationStepContext& context, const AutomationStep& step, AutomationWatchValue& outValue, string& outMessage ) const
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

    bool AutomationEvaluator::compareWatch( const AutomationStepContext& context, const AutomationStep& step, bool& outMatched, string& outMessage ) const
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
            const expected<Internal::NumberComparison> comparison = Internal::parseNumberComparison( step, Internal::kWatchNearTolerance );
            if ( comparison.has_value() == false )
            {
                outMessage = AutomationStep::describeReason( comparison.error() ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                return false;
            }
            bOk = Internal::matchesNumber( *comparison, value._number, rule );
        }
        outMatched = bOk;
        outMessage = subject + " " + rule + ", got " + valueText;
        return true;
    }

    void AutomationEvaluator::addRecordLine( string line )
    {
        SW_LOG_INFO_ID( "Automation.Record", "[Scenario] record %#", line.c_str() );
        _listRecordLine.push_back( std::move( line ) );
    }

    void AutomationEvaluator::prepareWatches( const AutomationScenario& scenario )
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

    void AutomationEvaluator::sampleWatches( AutomationStepContext& context )
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

    void AutomationEvaluator::runExpectVisited( AutomationStepContext& context, const AutomationStep& step )
    {
        const AutomationExpectVisitedParams params = step.readValidatedProperties<AutomationExpectVisitedParams>();
        const string_view                   watch  = params._watch;
        const string_view                   target = params._target;
        const string_view                   key    = params._key;
        const WatchHistory*                 pWatch = nullptr;
        for ( const WatchHistory& history : _listWatch )
        {
            const bool bSame = history._watch == watch && history._target == target && history._key == key;
            if ( bSame )
                pWatch = &history;
        }
        const uint32   since = params._since; // 적지 않으면 처음부터 본다
        vector<string> listWant;
        if ( step.findAttribute( "is" ) != nullptr )
        {
            listWant.push_back( params._is );
        }
        else
        {
            // `sequence` 는 쉼표로 가른 값 목록이다(앞뒤 공백을 떼고 빈 항목은 버린다).
            for ( const string_view value : StringUtil::split( params._sequence, ",", { ._bTrim = true, ._bDropEmpty = true } ) )
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

    void AutomationEvaluator::runRecord( AutomationStepContext& context, const AutomationStep& step )
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

    void AutomationEvaluator::runRemember( AutomationStepContext& context, const AutomationStep& step )
    {
        AutomationWatchValue value{};
        string               message;
        if ( readWatchValue( context, step, value, message ) == false )
        {
            context.recordFailure( step, "Remember " + message );
            return;
        }
        const string name = step.readValidatedProperties<AutomationRememberParams>()._as;
        string       text = AutomationStepArgsUtil::makeValueText( value );
        SW_LOG_INFO_ID( "Automation.FrameRemember", "[Scenario] frame %# remember %# = %# (%#)", context.getFrameIndex(), name.c_str(), text.c_str(), message.c_str() );
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

    void AutomationEvaluator::runExpectLog( AutomationStepContext& context, const AutomationStep& step )
    {
        const AutomationExpectLogParams params     = step.readValidatedProperties<AutomationExpectLogParams>();
        const string                    target     = params._contains.empty() ? params._id : string( params._id + "' containing '" + params._contains );
        const uint32                    matchCount = _logCapture.countLinesWithID( params._id, params._contains, params._since );
        if ( params._count != kAutomationNoCount )
        {
            if ( matchCount != params._count )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "ExpectLog id '%#' count == %#, got %#", target, params._count, matchCount );
                context.recordFailure( step, textBuilder.view() );
            }
            return;
        }
        if ( matchCount < params._atLeast )
        {
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "ExpectLog id '%#' count >= %#, got %#", target, params._atLeast, matchCount );
            context.recordFailure( step, textBuilder.view() );
        }
    }

    void AutomationEvaluator::deferExpectImage( AutomationStepContext& context, const AutomationStep& step )
    {
        const expected<AutomationStepArgsUtil::ExpectImageArgs> args = AutomationStepArgsUtil::parseExpectImage( step );
        if ( args.has_value() == false )
        {
            context.recordFailure( step, AutomationStep::describeReason( args.error() ) ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
            return;
        }
        // 골든은 백엔드마다 따로다(래스터 · 정밀도 차가 백엔드마다 다르다). 시나리오 안에서 백엔드를 바꿀 수 있어 단언하는 때의 백엔드를 쓴다.
        // 백엔드는 감시 값 `RHI.Backend`(App 의 교체기가 올린다)로 읽는다 — 판정이 RHI 를 직접 알지 않는다. 없으면(RHI 없는 호스트) 이미지 단언을 할 수 없다.
        const AutomationWatchRegistration* pBackendWatch = AutomationWatchRegistry::find( "RHI.Backend" );
        float64                            backendValue  = -1.0;
        if ( pBackendWatch == nullptr || pBackendWatch->_pFunction == nullptr || pBackendWatch->_pFunction( nullptr, backendValue ) == false )
        {
            context.recordFailure( step, "the RHI backend is unknown (watch RHI.Backend) - an image assertion needs a rendering host" );
            return;
        }
        // 골든 꼬리는 쿠킹 표의 명령줄 이름이다(`AutomationGoldenUtil::findBackendTag`) — 손으로 적은 표가 WebGPU 를 빠뜨려 그 골든을 뜰 수 없었다.
        const optional<string_view> backendTag = AutomationGoldenUtil::findBackendTag( static_cast<uint32>( backendValue ) );
        if ( backendTag.has_value() == false )
        {
            context.recordFailure( step, "the RHI backend " + AutomationStepArgsUtil::formatNumber( backendValue ) + " is not in the cook table - no golden name for it" );
            return;
        }
        _listDeferredImage.push_back( DeferredImageCheck{ &step, string( *backendTag ) } );
    }

    void AutomationEvaluator::deferExpectImagesDiffer( const AutomationStep& step )
    {
        _listDeferredImagesDiffer.push_back( &step );
    }

    void AutomationEvaluator::evaluateImagesDiffer( AutomationStepContext& context, const AutomationStep& step )
    {
        const AutomationExpectImagesDifferParams params = step.readValidatedProperties<AutomationExpectImagesDifferParams>();
        if ( _goldenPolicy.has_value() == false )
        {
            context.recordFailure( step, "could not compare the images: " + AutomationStep::describeReason( _goldenPolicy.error() ) );
            return;
        }
        AutomationImageRegion region{};
        if ( params._region.empty() == false && AutomationImage::tryParseRegion( params._region, region ) == false )
        {
            context.recordFailure( step, "could not compare the images: the region is not x0,y0,x1,y1 - " + params._region ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
            return;
        }
        const auto loadReduced = [&]( const string& file )
        { return AutomationImage::loadPpm( context.resolveOutputPath( file ) ).and_then( [&]( const AutomationImage& image )
          { return AutomationImage::reduce( image, region ); } ); };
        const expected<AutomationImage>           reducedFirst  = loadReduced( params._first );
        const expected<AutomationImage>           reducedSecond = reducedFirst.has_value() ? loadReduced( params._second ) : reducedFirst;
        const expected<AutomationImageDifference> compared =
            reducedSecond.and_then( [&]( const AutomationImage& second )
        { return AutomationImage::compare( *reducedFirst, second, _goldenPolicy->_measure ); } );
        if ( compared.has_value() == false )
        {
            context.recordFailure( step, "could not compare the images: " + AutomationStep::describeReason( compared.error() ) );
            return;
        }
        const AutomationImageDifference& difference = *compared;
        string                           line       = params._first + " vs " + params._second + ": " + AutomationStepArgsUtil::formatNumber( difference._differingFraction * 100.0 ) + "% differing (at least " +
                      AutomationStepArgsUtil::formatNumber( static_cast<float64>( params._minDiffering ) * 100.0 ) + "%)";
        SW_LOG_INFO_ID( "Automation.ImagesDiffer", "[Scenario] image %#", line.c_str() );
        _listMetricLine.push_back( line );
        if ( difference._differingFraction < static_cast<float64>( params._minDiffering ) )
            context.recordFailure( step, "the field did not change the picture - " + line );
        if ( difference._differingFraction > static_cast<float64>( params._maxDiffering ) )
            context.recordFailure( step, "the field changed the picture more than allowed (at most " + AutomationStepArgsUtil::formatNumber( static_cast<float64>( params._maxDiffering ) * 100.0 ) + "%) - " + line );
    }

    void AutomationEvaluator::deferExpectAtMost( const AutomationStep& step, string description, float64 limit, shared_ptr<const AutomationDeferredValue> pValue )
    {
        SW_ASSERT( pValue != nullptr );
        _listDeferredAtMost.push_back( DeferredAtMostCheck{ &step, std::move( pValue ), std::move( description ), limit } );
    }

    void AutomationEvaluator::evaluateDeferred( AutomationStepContext& context )
    {
        for ( const DeferredImageCheck& check : _listDeferredImage )
        {
            evaluateImage( context, *check._pStep, check._backendTag );
        }
        _listDeferredImage.clear();

        for ( const AutomationStep* const pStep : _listDeferredImagesDiffer )
        {
            evaluateImagesDiffer( context, *pStep );
        }
        _listDeferredImagesDiffer.clear();

        for ( const DeferredAtMostCheck& check : _listDeferredAtMost )
        {
            float64                              value = 0.0;
            const AutomationDeferredValue::State state = check._pValue->findValue( value );
            if ( state == AutomationDeferredValue::State::Pending )
                context.recordFailure( *check._pStep, check._description + ": the value was never measured (background work did not finish)" );
            else if ( state == AutomationDeferredValue::State::Missing )
                context.recordFailure( *check._pStep, check._description + ": the value could not be measured" );
            else if ( value > check._limit )
                context.recordFailure( *check._pStep, check._description + ", got " + AutomationStepArgsUtil::formatNumber( value ) );
        }
        _listDeferredAtMost.clear();
    }

    void AutomationEvaluator::evaluateImage( AutomationStepContext& context, const AutomationStep& step, string_view backendTag )
    {
        const expected<AutomationStepArgsUtil::ExpectImageArgs> parsedArgs = AutomationStepArgsUtil::parseExpectImage( step );
        if ( parsedArgs.has_value() == false )
        {
            context.recordFailure( step, AutomationStep::describeReason( parsedArgs.error() ) ); // 미룰 때 읽혔다 — 그 뒤 단계가 바뀌었다(핫 리로드)
            return;
        }
        if ( _goldenPolicy.has_value() == false )
        {
            context.recordFailure( step, "could not judge the image: " + AutomationStep::describeReason( _goldenPolicy.error() ) );
            return;
        }
        const AutomationGoldenPolicy&                  policy = *_goldenPolicy;
        const AutomationStepArgsUtil::ExpectImageArgs& args   = *parsedArgs;
        const AutomationGoldenLimits                   limits = args.resolveLimits( policy._limits );
        const string                                   file   = step.readValidatedProperties<AutomationExpectImageParams>()._file;
        const expected<AutomationImage>                loaded =
            AutomationImage::loadPpm( context.resolveOutputPath( file ) ).and_then( [&]( const AutomationImage& image )
        { return AutomationImage::reduce( image, args._region ); } );
        if ( loaded.has_value() == false )
        {
            context.recordFailure( step, "could not read the image: " + AutomationStep::describeReason( loaded.error() ) );
            return;
        }
        const AutomationImage&                    reduced  = *loaded;
        const string                              goldenID = AutomationGoldenUtil::makeGoldenID( _goldenFolderID, _goldenPrefix, args._golden, backendTag, AutomationGoldenUtil::getPlatformTag() );
        vector<uint8>                             listBytes;
        const bool                                bReadGolden = AssetIO::hasAsset( AssetID{ goldenID } ) && AssetIO::readBinaryAsset( AssetID{ goldenID }, listBytes );
        const expected<AutomationImage>           golden      = bReadGolden ? AutomationImage::parsePpm( listBytes.data(), listBytes.size() )
                                                                            : expected<AutomationImage>( unexpected( Error{ ErrorKind::NotFound, "no golden yet", goldenID } ) );
        const bool                                bHasGolden  = golden.has_value();
        const expected<AutomationImageDifference> compared =
            golden.and_then( [&]( const AutomationImage& goldenImage )
        { return AutomationImage::compare( reduced, goldenImage, policy._measure ); } );
        const bool                      bCompared  = compared.has_value();
        const AutomationImageDifference difference = bCompared ? *compared : AutomationImageDifference{};
        const GoldenResult              evaluation = bCompared ? AutomationGoldenUtil::judge( difference, limits, policy._nearLimitRatio ) : GoldenResult::Fail;
        string                          line;
        if ( bCompared )
        {
            static constexpr const utf8* kArrResultName[] = { "pass", "pass near limit", "fail" };
            line                                          = file + " vs " + goldenID + ": " + AutomationStepArgsUtil::formatNumber( difference._differingFraction * 100.0 ) + "% differing (at most " +
                   AutomationStepArgsUtil::formatNumber( limits._maxDifferingFraction * 100.0 ) + "%), mean distance " + AutomationStepArgsUtil::formatNumber( difference._meanDistance ) + " (at most " +
                   AutomationStepArgsUtil::formatNumber( limits._maxMeanDistance ) + "), largest deep region " + to_string( difference._largestDeepRegionPixels ) + " px (at most " +
                   to_string( limits._maxDeepRegionPixels ) + ") - " + kArrResultName[static_cast<uint32>( evaluation )];
            SW_LOG_INFO_ID( "Automation.Image", "[Scenario] image %#", line.c_str() );
            _listMetricLine.push_back( line );
            // 기록 모드는 앞 골든과의 차이라 같은 골든의 실행 사이 흔들림이 아니다 — 일반 실행만 모은다.
            if ( _bUpdateGolden == false )
                _listGoldenSample.push_back( AutomationGoldenSample{ goldenID, difference, limits } );
        }
        switch ( AutomationGoldenUtil::decide( bHasGolden, bCompared, evaluation, _bUpdateGolden ) )
        {
            case GoldenAction::Keep:
            {
                return;
            }
            case GoldenAction::KeepNearLimit:
            {
                // 실행기 줄(`[Scenario]`)이라 로그 게이트가 세지 않는다(`AutomationLogPolicy::isExemptMessage`) — 판정 대상의 결함이 아니라 골든의 여유 문제라 시나리오를
                // 지게 하지 않는다. 경고 id 로 묶음 끝 로그 분류에, 보고의 `nearLimitGoldens[]` 에 남겨 기록 모드로 다시 뜨게 한다.
                SW_LOG_WARNING_ID( "Automation.GoldenNearLimit", "[Scenario] golden near its limit - record it again with -scenario-update-golden: %#", line.c_str() );
                _listNearLimitGolden.push_back( line );
                return;
            }
            case GoldenAction::Write:
            {
                writeGolden( context, step, reduced, goldenID, backendTag, AutomationGoldenUtil::describeWriteReason( bHasGolden, bCompared, evaluation ), bCompared ? &difference : nullptr, limits );
                return;
            }
            case GoldenAction::FailMissing:
            {
                context.recordFailure( step, "no golden " + goldenID + " yet - run with -scenario-update-golden and review the image" );
                return;
            }
            case GoldenAction::FailUnreadable:
            {
                context.recordFailure( step, AutomationStep::describeReason( compared.error() ) );
                return;
            }
            case GoldenAction::Fail:
            {
                context.recordFailure( step, "differs from its golden: " + line );
                return;
            }
        }
    }

    void AutomationEvaluator::writeGolden( AutomationStepContext& context, const AutomationStep& step, const AutomationImage& image, const string& goldenID, string_view backendTag, const utf8* pReason,
                                           const AutomationImageDifference* pPrevious, const AutomationGoldenLimits& limits )
    {
        // 골든 쓰기는 이 모드에서만 연다(`AutomationScratch` 의 쓰기 문 — 평소에는 리소스 루트에 쓰지 못한다). 쓴 파일은 보고에 남아 리뷰한다.
        if ( _recordingCommit.empty() )
        {
            const expected<string> commit = AutomationGoldenUtil::findRecordingCommit();
            if ( commit.has_value() == false )
            {
                context.recordFailure( step, "could not write the golden " + goldenID + ": " + AutomationStep::describeReason( commit.error() ) );
                return;
            }
            _recordingCommit = *commit;
        }
        AutomationGoldenMeta meta;
        meta._backendTag     = string( backendTag );
        meta._platform       = AutomationGoldenUtil::getPlatformTag().empty() ? "windows" : "linux";
        meta._commit         = _recordingCommit;
        meta._recordedUnixMs = WallClock::nowUnixMilliseconds().count();
        meta._reason         = pReason;
        meta._bMeasured      = pPrevious != nullptr;
        meta._difference     = pPrevious != nullptr ? *pPrevious : AutomationImageDifference{};
        meta._limits         = limits;
        meta._width          = image._width;
        meta._height         = image._height;
        vector<uint8> listEncoded;
        AutomationImage::encodePpm( image, listEncoded );
        const optional<AbsolutePath> imagePath = engine::getSubsystem<AssetPaths>().toDiskPath( goldenID );
        const optional<AbsolutePath> metaPath  = engine::getSubsystem<AssetPaths>().toDiskPath( AutomationGoldenUtil::makeMetaID( goldenID ) );
        const bool                   bWritten  = imagePath.has_value() && metaPath.has_value() && FileUtil::ensureParentDirectoryExists( imagePath->str() ) &&
                              FileUtil::writeFile( imagePath->str(), listEncoded.data(), listEncoded.size() ) && FileUtil::writeTextFile( metaPath->str(), AutomationGoldenUtil::makeMetaText( meta ) );
        if ( bWritten == false )
        {
            context.recordFailure( step, "could not write the golden " + goldenID );
            return;
        }
        SW_LOG_INFO_ID( "Automation.Golden", "[Scenario] golden written (%#) %#", pReason, goldenID.c_str() );
        _listUpdatedGolden.push_back( goldenID );
    }

    optional<string> AutomationEvaluator::applyLogCheck( AutomationResult result )
    {
        AutomationLogCheckResult evaluation = _logCapture.evaluateLines( *_pLogPolicy );
        if ( engine::areEngineServicesBound() )
        {
            for ( const string& key : engine::getSubsystem<LocalizationManager>().getMissingKeys() )
            {
                const string line         = "Missing localized text for key '" + key + "'";
                const bool   bAlreadySeen = _logCapture.containsLine( line ); // 시나리오 구간에 남은 경고 줄은 위에서 이미 셌다
                // 시나리오 앞에 남은 같은 사건(`LocalizationManager` 의 `Localization.MissingKey` 줄)이다 — 같은 id 로 판정한다
                const AutomationLogResult result = _pLogPolicy->classify( LogLevel::Warning, "Localization.MissingKey", line );
                if ( bAlreadySeen || result == AutomationLogResult::Declared || result == AutomationLogResult::Allowed )
                    continue;
                ++evaluation._notAllowedCount;
                evaluation._listUnhandled.push_back( "[Warning] [LocalizationManager] (id=Localization.MissingKey) " + line + " (before the scenario started)" );
            }
        }
        _listAllowedLogID = std::move( evaluation._listAllowedID );
        if ( evaluation.hasUnhandled() == false )
            return nullopt;
        _listUnhandledLog = std::move( evaluation._listUnhandled );
        // 이미 진 실행은 결과를 바꾸지 않는다(원인은 단언 · 시한이다). 줄은 보고의 `unhandledLogs` 에만 남겨, 진 보고도 경고 · 오류 줄을 숨기지 않게 한다.
        if ( result != AutomationResult::Passed )
            return nullopt;
        for ( const string& line : _listUnhandledLog )
        {
            _listFailure.push_back( "unhandled log: " + line );
        }
        return to_string( _listUnhandledLog.size() ) + " unhandled Warning/Error log line(s), " + to_string( evaluation._missingIDCount ) +
               " without a log id - fix the cause, declare the line with <ExpectLog>, or allow its log id in this scenario with <AllowLog id=\"...\" reason=\"...\"/>"
               " (a line without a log id needs SW_LOG_WARNING_ID / SW_LOG_ERROR_ID first)";
    }

    void AutomationEvaluator::addFailure( const AutomationStep& step, string_view message )
    {
        string line = step.describe() + ": " + string( message );
        SW_LOG_WARNING_ID( "Automation.JudgeWarning", "[Scenario] %#", line.c_str() );
        _listFailure.push_back( std::move( line ) );
    }

    void AutomationEvaluator::prepare( const AutomationScenario& scenario )
    {
        _pLogPolicy->prepare( scenario );
        prepareWatches( scenario );
        _listRemembered.clear();
        _listUpdatedGolden.clear();
        _listNearLimitGolden.clear();
        _listGoldenSample.clear();
        _recordingCommit.clear();
        _goldenPolicy   = AutomationGoldenUtil::loadPolicy(); // 이미지 단언이 없는 시나리오는 오류를 쓰지 않는다 — 단언이 그 이유로 진다
        _goldenFolderID = FileUtil::getDirectoryPart( scenario.getSourcePath() ) + "/" + kAutomationGoldenFolder;
        _goldenPrefix   = scenario.getName();
        _bUpdateGolden  = false;
        if ( engine::areEngineServicesBound() )
            (void)engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::SCENARIO_UPDATE_GOLDEN, _bUpdateGolden );
        _logCapture.start();
    }
} // namespace sw
