#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationEvaluator.h"

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

#include "Engine/Automation/Scenario/AutomationCaptureUtil.h"
#include "Engine/Automation/Scenario/AutomationRunnerParams.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetWriteAccess.h"
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
#include "Engine/Renderer/Debug/RenderDebugRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/GenericWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    AutomationEvaluator::AutomationEvaluator( shared_ptr<AutomationLogPolicy> pLogPolicy )
        : _logCapture{}
        , _pLogPolicy{ std::move( pLogPolicy ) }
        , _listFailure{}
        , _listUpdatedGolden{}
        , _listReferencedGolden{}
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
        , _stored{}
    {
        SW_ASSERT( _pLogPolicy != nullptr );
    }

    AutomationEvaluator::~AutomationEvaluator()
    {
        _pLogPolicy->clear();
    }

    void AutomationEvaluator::runExpect( IAutomationStepContext& context, const AutomationStep& step )
    {
        bool   bMatched = false;
        string message;
        if ( compareWatch( context, step, bMatched, message ) == false )
            context.recordFailure( step, message );
        else if ( bMatched == false )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "Expect %#", message );
            context.recordFailure( step, text.view() );
        }
    }

    bool AutomationEvaluator::readWatchValue( const IAutomationStepContext& context, const AutomationStep& step, AutomationWatchValue& outValue, string& outMessage ) const
    {
        const string&                      watchName     = *step.findAttribute( "watch" );
        const AutomationWatchRegistration* pRegistration = AutomationWatchRegistry::find( watchName );
        // 모듈을 언로드했다 로드하면 감시 값이 빠질 수 있다. 시작 때 있었어도 다시 찾는다.
        if ( pRegistration == nullptr )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "%# is no longer registered", watchName );
            outMessage = string( text.view() );
            return false;
        }
        const string*        pTarget = step.findAttribute( "target" );
        const string*        pKey    = step.findAttribute( "key" );
        AutomationWatchQuery query   = AutomationWatchQuery::make( context.findActiveObjectManager(), pTarget != nullptr ? string_view{ *pTarget } : string_view{},
                                                                 pKey != nullptr ? string_view{ *pKey } : string_view{} );
          query._pLogCounter = &_logCapture; // 로그 수 감시 값(`Log.Count`)이 이 실행의 줄을 센다
        StringBuilder<constant::buffer::kMax256> subject;
        subject.append( string_view{ watchName } );
        if ( pTarget != nullptr && pKey != nullptr )
            subject.appendFormat( "(%#, %#)", *pTarget, *pKey );
        else if ( pTarget != nullptr )
            subject.appendFormat( "(%#)", *pTarget );
        else if ( pKey != nullptr )
            subject.appendFormat( "(%#)", *pKey );
        if ( AutomationWatchRegistry::read( *pRegistration, query, outValue ) == false )
        {
            const string reason = outValue._bText && outValue._text.empty() == false ? outValue._text : string( "no target in the scene" );
            subject.appendFormat( " has no value (%#)", reason );
            outMessage = string( subject.view() );
            return false;
        }
        outMessage = string( subject.view() );
        return true;
    }

    bool AutomationEvaluator::compareWatch( const IAutomationStepContext& context, const AutomationStep& step, bool& outMatched, string& outMessage ) const
    {
        using Internal = AutomationStepArgsUtil;
        AutomationWatchValue value{};
        outMatched = false;
        if ( readWatchValue( context, step, value, outMessage ) == false )
            return false;
        const string                             valueText = Internal::makeValueText( value );
        const string                             subject   = outMessage;
        bool                                     bOk       = true;
        string                                   rule;
        StringBuilder<constant::buffer::kMax256> text;
        const bool                               bNumberRule = step.findAttribute( "is" ) == nullptr && step.findAttribute( "sameAs" ) == nullptr && step.findAttribute( "differsFrom" ) == nullptr;
        if ( bNumberRule && value._bText )
        {
            text.appendFormat( "%# is text ('%#') - compare it with is=\"…\"", subject, value._text );
            outMessage = string( text.view() );
            return false;
        }
        const string* pSameAs      = step.findAttribute( "sameAs" );
        const string* pDiffersFrom = step.findAttribute( "differsFrom" );
        if ( pSameAs != nullptr || pDiffersFrom != nullptr )
        {
            // 기억한 값과는 글 그대로 비교한다 — 64 비트 해시 같은 값이 수 비교(float32)에서 뭉개지지 않는다.
            const string& name    = pSameAs != nullptr ? *pSameAs : *pDiffersFrom;
            const string* pStored = _stored.find( name );
            if ( pStored == nullptr )
            {
                text.appendFormat( "%#: nothing was stored as '%#' (add <Store as=\"%#\"> before this step)", subject, name, name );
                outMessage = string( text.view() );
                return false;
            }
            bool bSame = valueText == *pStored;
            // 허용 폭을 주면 수로 견준다 — 지난 프레임 포즈 같은 실수는 같은 상태여도 끝자리가 다르다(글 비교는 해시처럼 끝자리까지 같아야 하는 값에 쓴다).
            if ( step.findAttribute( "tolerance" ) != nullptr )
            {
                float32 tolerance  = Internal::kWatchNearTolerance;
                float32 remembered = 0.0f;
                if ( Internal::readFloat( step, "tolerance", tolerance ).has_value() == false )
                    return false; // 검사가 본다 — 단계가 바뀌었다(핫 리로드)
                if ( value._bText || StringUtil::parseFloat( string_view{ *pStored }, remembered ) == false )
                {
                    text.appendFormat( "%#: tolerance compares numbers, but '%#' ('%#') or the value (%#) is not a number", subject, name, *pStored, valueText );
                    outMessage = string( text.view() );
                    return false;
                }
                bSame = MathUtil::abs( value._number - static_cast<float64>( remembered ) ) <= static_cast<float64>( tolerance );
            }
            bOk        = pSameAs != nullptr ? bSame : bSame == false;
            outMatched = bOk;
            text.appendFormat( "%#%#%# ('%#'), got %#", subject, pSameAs != nullptr ? " same as " : " differs from ", name, *pStored, valueText );
            outMessage = string( text.view() );
            return true;
        }
        if ( const string* pOppositeOf = step.findAttribute( "oppositeOf" ); pOppositeOf != nullptr )
        {
            // 기억한 수의 부호를 뒤집은 값과 허용 폭 안이다(미러가 좌우 본의 X 를 뒤집었는지). 수끼리 보므로 글 비교(sameAs)의 자릿수 차이에 지지 않는다.
            const string* pStored    = _stored.find( *pOppositeOf );
            float32       remembered = 0.0f;
            if ( pStored == nullptr || StringUtil::parseFloat( string_view{ *pStored }, remembered ) == false )
            {
                text.appendFormat( "%#: no number was remembered as '%#' (add <Remember as=\"%#\"> on a number watch before this step)", subject, *pOppositeOf, *pOppositeOf );
                outMessage = string( text.view() );
                return false;
            }
            float32 tolerance = Internal::kWatchNearTolerance;
            if ( Internal::readFloat( step, "tolerance", tolerance ).has_value() == false )
                return false; // 검사가 본다 — 단계가 바뀌었다(핫 리로드)
            bOk        = MathUtil::abs( value._number + static_cast<float64>( remembered ) ) <= static_cast<float64>( tolerance );
            outMatched = bOk;
            text.appendFormat( "%# opposite of %# ('%#'), got %#", subject, *pOppositeOf, *pStored, valueText );
            outMessage = string( text.view() );
            return true;
        }
        if ( const string* pIncreaseFrom = step.findAttribute( "increaseFrom" ); pIncreaseFrom != nullptr )
        {
            // 기억한 수에서 increaseBy 이상 늘었다(그린 프레임 수 같은 누적 값 — "앞으로 N 장 더" 를 고정 대기 대신 센다).
            const string*        pStored       = _stored.find( *pIncreaseFrom );
            const expected<bool> increaseMatch = Internal::matchesIncrease( step, value._number, pStored );
            if ( increaseMatch.has_value() == false )
            {
                text.appendFormat( "%# (increaseFrom '%#'): %#", subject, *pIncreaseFrom, AutomationStep::describeReason( increaseMatch.error() ) );
                outMessage = string( text.view() );
                return false;
            }
            bOk        = *increaseMatch;
            outMatched = bOk;
            text.appendFormat( "%# increase by %# from %# ('%#'), got %#", subject, *step.findAttribute( "increaseBy" ), *pIncreaseFrom, *pStored, valueText );
            outMessage = string( text.view() );
            return true;
        }
        if ( step.findAttribute( "is" ) != nullptr )
        {
            bOk = valueText == *step.findAttribute( "is" );
            StringBuilder<constant::buffer::kMax256> ruleText;
            ruleText.appendFormat( "is '%#'", *step.findAttribute( "is" ) );
            rule = string( ruleText.view() );
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
        text.appendFormat( "%# %#, got %#", subject, rule, valueText );
        outMessage = string( text.view() );
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

    void AutomationEvaluator::sampleWatches( IAutomationStepContext& context )
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

    void AutomationEvaluator::runExpectVisited( IAutomationStepContext& context, const AutomationStep& step )
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
        size_t                                   wantIndex = 0;
        StringBuilder<constant::buffer::kMax256> seen;
        for ( size_t index = first; pWatch != nullptr && index < pWatch->_listValue.size(); ++index )
        {
            seen.appendFormat( "%#%#@%#", seen.size() == 0 ? "" : " > ", pWatch->_listValue[index], pWatch->_listFrame[index] );
            if ( wantIndex < listWant.size() && pWatch->_listValue[index] == listWant[wantIndex] )
                ++wantIndex;
        }
        if ( wantIndex == listWant.size() )
            return;
        StringBuilder<constant::buffer::kMax256> text;
        text.append( watch );
        if ( target.empty() == false || key.empty() == false )
            text.appendFormat( "(%#%#%#)", target, target.empty() || key.empty() ? "" : ", ", key );
        text.append( " never went through '" );
        for ( size_t index = 0; index < listWant.size(); ++index )
        {
            text.appendFormat( "%#%#", index == 0 ? "" : " > ", listWant[index] );
        }
        text.appendFormat( "' since frame %# (missing '%#'), saw: %#", since, listWant[wantIndex], seen.size() == 0 ? string_view{ "no value" } : seen.view() );
        context.recordFailure( step, text.view() );
    }

    void AutomationEvaluator::runRecord( IAutomationStepContext& context, const AutomationStep& step )
    {
        AutomationWatchValue value{};
        string               message;
        if ( readWatchValue( context, step, value, message ) == false )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "Record %#", message );
            context.recordFailure( step, text.view() );
            return;
        }
        StringBuilder<constant::buffer::kMax256> line;
        line.appendFormat( "frame %# %# = %#", context.getFrameIndex(), message, AutomationStepArgsUtil::makeValueText( value ) );
        addRecordLine( string( line.view() ) );
    }

    void AutomationEvaluator::runStore( IAutomationStepContext& context, const AutomationStep& step )
    {
        AutomationWatchValue value{};
        string               message;
        if ( readWatchValue( context, step, value, message ) == false )
        {
            StringBuilder<constant::buffer::kMax256> failure;
            failure.appendFormat( "Store %#", message );
            context.recordFailure( step, failure.view() );
            return;
        }
        const string name = step.readValidatedProperties<AutomationStoreParams>()._as;
        string       text = AutomationStepArgsUtil::makeValueText( value );
        SW_LOG_INFO_ID( "Automation.FrameStore", "[Scenario] frame %# store %# = %# (%#)", context.getFrameIndex(), name.c_str(), text.c_str(), message.c_str() );
        _stored.set( name, std::move( text ) );
    }

    void AutomationEvaluator::runExpectLog( IAutomationStepContext& context, const AutomationStep& step )
    {
        const AutomationExpectLogParams          params = step.readValidatedProperties<AutomationExpectLogParams>();
        StringBuilder<constant::buffer::kMax256> targetBuilder;
        targetBuilder.append( string_view{ params._id } );
        if ( params._contains.empty() == false )
            targetBuilder.appendFormat( "' containing '%#", params._contains );
        const string target     = string( targetBuilder.view() );
        const uint32 matchCount = _logCapture.countLinesWithID( params._id, params._contains, params._since );
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

    void AutomationEvaluator::deferExpectImage( IAutomationStepContext& context, const AutomationStep& step )
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
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "the RHI backend %# is not in the cook table - no golden name for it", AutomationStepArgsUtil::formatNumber( backendValue ) );
            context.recordFailure( step, text.view() );
            return;
        }
        _listDeferredImage.push_back( DeferredImageCheck{ &step, string( *backendTag ) } );
    }

    void AutomationEvaluator::deferExpectImagesDiffer( const AutomationStep& step )
    {
        _listDeferredImagesDiffer.push_back( &step );
    }

    void AutomationEvaluator::evaluateImagesDiffer( IAutomationStepContext& context, const AutomationStep& step )
    {
        const AutomationExpectImagesDifferParams params = step.readValidatedProperties<AutomationExpectImagesDifferParams>();
        if ( _goldenPolicy.has_value() == false )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "could not compare the images: %#", AutomationStep::describeReason( _goldenPolicy.error() ) );
            context.recordFailure( step, text.view() );
            return;
        }
        AutomationImageRegion region{};
        if ( params._region.empty() == false && AutomationImage::tryParseRegion( params._region, region ) == false )
        {
            // 검사 뒤 단계가 바뀌었다(핫 리로드)
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "could not compare the images: the region is not x0,y0,x1,y1 - %#", params._region );
            context.recordFailure( step, text.view() );
            return;
        }
        const auto loadReduced = [&]( const string& file )
        { return AutomationImage::loadPpm( context.resolveOutputPath( file ) ).and_then( [&]( const AutomationImage& image )
          { return AutomationImage::reduce( image, region, params._bFullResolution ? image._width : AutomationImage::kGoldenMaxWidth ); } ); };
        const expected<AutomationImage>           reducedFirst  = loadReduced( params._first );
        const expected<AutomationImage>           reducedSecond = reducedFirst.has_value() ? loadReduced( params._second ) : reducedFirst;
        const expected<AutomationImageDifference> compared =
            reducedSecond.and_then( [&]( const AutomationImage& second )
        { return AutomationImage::compare( *reducedFirst, second, _goldenPolicy->_measure ); } );
        if ( compared.has_value() == false )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "could not compare the images: %#", AutomationStep::describeReason( compared.error() ) );
            context.recordFailure( step, text.view() );
            return;
        }
        const AutomationImageDifference&         difference = *compared;
        StringBuilder<constant::buffer::kMax256> line;
        line.appendFormat( "%# vs %#: %#%% differing (at least %#%%)", params._first, params._second, AutomationStepArgsUtil::formatNumber( difference._differingFraction * 100.0 ),
                           AutomationStepArgsUtil::formatNumber( static_cast<float64>( params._minDiffering ) * 100.0 ) );
        SW_LOG_INFO_ID( "Automation.ImagesDiffer", "[Scenario] image %#", line.c_str() );
        _listMetricLine.emplace_back( line.view() );
        if ( difference._differingFraction < static_cast<float64>( params._minDiffering ) )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "the field did not change the picture - %#", line.view() );
            context.recordFailure( step, text.view() );
        }
        if ( difference._differingFraction > static_cast<float64>( params._maxDiffering ) )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "the field changed the picture more than allowed (at most %#%%) - %#", AutomationStepArgsUtil::formatNumber( static_cast<float64>( params._maxDiffering ) * 100.0 ),
                               line.view() );
            context.recordFailure( step, text.view() );
        }
    }

    void AutomationEvaluator::deferExpectAtMost( const AutomationStep& step, string description, float64 limit, shared_ptr<const AutomationDeferredValue> pValue )
    {
        SW_ASSERT( pValue != nullptr );
        _listDeferredAtMost.push_back( DeferredAtMostCheck{ &step, std::move( pValue ), std::move( description ), limit } );
    }

    void AutomationEvaluator::evaluateDeferred( IAutomationStepContext& context )
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
            float64                                  value = 0.0;
            const AutomationDeferredValue::State     state = check._pValue->findValue( value );
            StringBuilder<constant::buffer::kMax256> text;
            if ( state == AutomationDeferredValue::State::Pending )
                text.appendFormat( "%#: the value was never measured (background work did not finish)", check._description );
            else if ( state == AutomationDeferredValue::State::Missing )
                text.appendFormat( "%#: the value could not be measured", check._description );
            else if ( value > check._limit )
                text.appendFormat( "%#, got %#", check._description, AutomationStepArgsUtil::formatNumber( value ) );
            if ( text.size() != 0 )
                context.recordFailure( *check._pStep, text.view() );
        }
        _listDeferredAtMost.clear();
    }

    void AutomationEvaluator::evaluateImage( IAutomationStepContext& context, const AutomationStep& step, string_view backendTag )
    {
        const expected<AutomationStepArgsUtil::ExpectImageArgs> parsedArgs = AutomationStepArgsUtil::parseExpectImage( step );
        if ( parsedArgs.has_value() == false )
        {
            context.recordFailure( step, AutomationStep::describeReason( parsedArgs.error() ) ); // 미룰 때 읽혔다 — 그 뒤 단계가 바뀌었다(핫 리로드)
            return;
        }
        if ( _goldenPolicy.has_value() == false )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "could not judge the image: %#", AutomationStep::describeReason( _goldenPolicy.error() ) );
            context.recordFailure( step, text.view() );
            return;
        }
        if ( _goldenWriteMode.has_value() == false )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "could not judge the image: %#", AutomationStep::describeReason( _goldenWriteMode.error() ) );
            context.recordFailure( step, text.view() );
            return;
        }
        const AutomationGoldenPolicy&                  policy = *_goldenPolicy;
        const AutomationStepArgsUtil::ExpectImageArgs& args   = *parsedArgs;
        const AutomationGoldenLimits                   limits = args.resolveLimits( policy._limits );
        const string                                   file   = step.readValidatedProperties<AutomationExpectImageParams>()._file;
        const expected<AutomationImage>                loaded =
            AutomationImage::loadPpm( context.resolveOutputPath( file ) ).and_then( [&]( const AutomationImage& image )
        { return AutomationImage::reduce( image, args._region, AutomationImage::kGoldenMaxWidth ); } );
        if ( loaded.has_value() == false )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "could not read the image: %#", AutomationStep::describeReason( loaded.error() ) );
            context.recordFailure( step, text.view() );
            return;
        }
        const AutomationImage& reduced  = *loaded;
        const string           goldenID = AutomationGoldenUtil::makeGoldenID( _goldenFolderID, _goldenPrefix, args._golden, backendTag, AutomationGoldenUtil::getPlatformTag() );
        // 골든은 처음 가리킨 단언의 것이다 — 같은 골든을 허용치를 바꿔 다시 대조하는 단언(업스케일한 장을 배율 1 골든에 견주기)은 대조만 하고 쓰지 않는다.
        // 기록 실행에서 뒤 단언이 잘린 그림으로 원본 골든을 덮었다(temporalupscale_native). 영역이 다르면 골든과 같은 화면이 아니라 견줄 수 없다.
        const auto owner       = std::find_if( _listReferencedGolden.begin(), _listReferencedGolden.end(), [&]( const ReferencedGolden& referenced )
              { return referenced._goldenID == goldenID; } );
        const bool bOwnsGolden = owner == _listReferencedGolden.end();
        if ( bOwnsGolden )
            _listReferencedGolden.push_back( ReferencedGolden{ goldenID, args._region } );
        else if ( owner->_region._x0 != args._region._x0 || owner->_region._y0 != args._region._y0 || owner->_region._x1 != args._region._x1 || owner->_region._y1 != args._region._y1 )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "golden %# is the region of an earlier assertion - a second assertion on it must use the same region", goldenID );
            context.recordFailure( step, text.view() );
            return;
        }
        const GoldenWriteMode                     recording = bOwnsGolden ? *_goldenWriteMode : GoldenWriteMode::Off;
        vector<uint8>                             listBytes;
        const bool                                bReadGolden = AssetIO::hasAsset( AssetID{ goldenID } ) && AssetIO::readBinaryAsset( AssetID{ goldenID }, listBytes );
        const expected<AutomationImage>           golden      = bReadGolden ? AutomationImage::parsePpm( listBytes.data(), listBytes.size() )
                                                                            : expected<AutomationImage>( unexpected( Error{ ErrorKind::NotFound, "no golden yet", goldenID } ) );
        const bool                                bHasGolden  = golden.has_value();
        const expected<AutomationImageDifference> compared =
            golden.and_then( [&]( const AutomationImage& goldenImage )
        { return AutomationImage::compare( reduced, goldenImage, policy._measure ); } );
        const bool                               bCompared  = compared.has_value();
        const AutomationImageDifference          difference = bCompared ? *compared : AutomationImageDifference{};
        const GoldenResult                       evaluation = bCompared ? AutomationGoldenUtil::evaluate( difference, limits, policy._nearLimitRatio ) : GoldenResult::Fail;
        StringBuilder<constant::buffer::kMax512> lineBuilder;
        if ( bCompared )
        {
            // 판정 이름은 switch 로 — 판정을 더하고 이름을 빠뜨리면 컴파일이 멈춘다(`-Wswitch`).
            const auto getResultName = []( GoldenResult result ) -> const utf8*
            {
                switch ( result )
                {
                    case GoldenResult::Pass:
                        return "pass";
                    case GoldenResult::PassNearLimit:
                        return "pass near limit";
                    case GoldenResult::Fail:
                        return "fail";
                }
                return "";
            };
            lineBuilder.appendFormat( "%# vs %#: %#%% differing (at most %#%%), mean distance %# (at most %#), largest deep region %# px (at most %#) - %#", file, goldenID,
                                      AutomationStepArgsUtil::formatNumber( difference._differingFraction * 100.0 ), AutomationStepArgsUtil::formatNumber( limits._maxDifferingFraction * 100.0 ),
                                      AutomationStepArgsUtil::formatNumber( difference._meanDistance ), AutomationStepArgsUtil::formatNumber( limits._maxMeanDistance ),
                                      difference._largestDeepRegionPixels, limits._maxDeepRegionPixels, getResultName( evaluation ) );
        }
        const string line = string( lineBuilder.view() );
        if ( bCompared )
        {
            SW_LOG_INFO_ID( "Automation.Image", "[Scenario] image %#", line.c_str() );
            _listMetricLine.push_back( line );
            // 모두 쓰는 기록 실행은 앞 골든과의 차이라 같은 골든의 실행 사이 흔들림이 아니다 — 있는 골든을 덮지 않는 실행만 모은다.
            if ( *_goldenWriteMode != GoldenWriteMode::All )
                _listGoldenSample.push_back( AutomationGoldenSample{ goldenID, difference, limits } );
        }
        switch ( AutomationGoldenUtil::decide( bHasGolden, bCompared, evaluation, recording ) )
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
            case GoldenAction::WriteMissing:
            {
                // 처음 쓴 골든은 아직 사람이 보지 않았다 — 지고 보고의 `updatedGoldens` 에 남아, 그림을 보고 커밋한 뒤 기록 없이 다시 돌려 통과해야 한다.
                writeGolden( context, step, reduced, goldenID, backendTag, AutomationGoldenUtil::describeWriteReason( bHasGolden, bCompared, evaluation ), nullptr, limits );
                StringBuilder<constant::buffer::kMax256> text;
                text.appendFormat( "no golden %# yet - recorded it now; review the image, commit it and run again without recording", goldenID );
                addFailure( step, text.view(), AutomationFailureKind::MissingBaseline );
                return;
            }
            case GoldenAction::FailMissing:
            {
                StringBuilder<constant::buffer::kMax256> text;
                text.appendFormat( "no golden %# yet - run with -scenario-record-missing-golden and review the image", goldenID );
                addFailure( step, text.view(), AutomationFailureKind::MissingBaseline );
                return;
            }
            case GoldenAction::FailUnreadable:
            {
                context.recordFailure( step, AutomationStep::describeReason( compared.error() ) );
                return;
            }
            case GoldenAction::Fail:
            {
                StringBuilder<constant::buffer::kMax256> text;
                text.appendFormat( "differs from its golden: %#", line );
                addFailure( step, text.view(), AutomationFailureKind::GoldenMismatch );
                // 그림이 어긋난 자리에서 GPU 정점을 CPU 정점과 견준다(결과는 로그) — 깨진 정점은 그 메시가 사라질 때까지만 남는다.
                if ( RenderDebugRegistry* const pRenderDebug = engine::areEngineServicesBound() ? engine::findSubsystem<RenderDebugRegistry>() : nullptr )
                    pRenderDebug->requestMeshVertexCheck();
                return;
            }
        }
    }

    void AutomationEvaluator::writeGolden( IAutomationStepContext& context, const AutomationStep& step, const AutomationImage& image, const string& goldenID, string_view backendTag, const utf8* pReason,
                                           const AutomationImageDifference* pPrevious, const AutomationGoldenLimits& limits )
    {
        // 골든 쓰기는 이 모드에서만 연다(`AutomationScratch` 의 쓰기 문 — 평소에는 리소스 루트에 쓰지 못한다). 쓴 파일은 보고에 남아 리뷰한다.
        if ( _recordingCommit.empty() )
        {
            const expected<string> commit = AutomationGoldenUtil::findRecordingCommit();
            if ( commit.has_value() == false )
            {
                StringBuilder<constant::buffer::kMax256> text;
                text.appendFormat( "could not write the golden %#: %#", goldenID, AutomationStep::describeReason( commit.error() ) );
                context.recordFailure( step, text.view() );
                return;
            }
            _recordingCommit = *commit;
        }
        AutomationGoldenMeta meta;
        meta._backendTag              = string( backendTag );
        const string_view platformTag = AutomationGoldenUtil::getPlatformTag();
        meta._platform                = platformTag.empty() ? string( "windows" ) : string( platformTag.substr( 1 ) ); // 앞의 점을 뺀다
        meta._commit                  = _recordingCommit;
        meta._recordedUnixMs          = WallClock::nowUnixMilliseconds().count();
        meta._reason                  = pReason;
        meta._bMeasured               = pPrevious != nullptr;
        meta._difference              = pPrevious != nullptr ? *pPrevious : AutomationImageDifference{};
        meta._limits                  = limits;
        meta._width                   = image._width;
        meta._height                  = image._height;
        vector<uint8> listEncoded;
        AutomationImage::encodePpm( image, listEncoded );
        const optional<DiskPath> imagePath = engine::getSubsystem<AssetPaths>().toDiskPath( goldenID );
        const optional<DiskPath> metaPath  = engine::getSubsystem<AssetPaths>().toDiskPath( AutomationGoldenUtil::makeMetaID( goldenID ) );
        bool                     bWritten  = imagePath.has_value() && metaPath.has_value();
        if ( bWritten )
        {
            const AbsolutePath writableImage = AssetWriteAccess::toWritablePath( *imagePath );
            bWritten                         = FileUtil::writeFile( writableImage, listEncoded.data(), listEncoded.size() ) &&
                       FileUtil::writeTextFile( AssetWriteAccess::toWritablePath( *metaPath ), AutomationGoldenUtil::makeMetaText( meta ) );
        }
        if ( bWritten == false )
        {
            StringBuilder<constant::buffer::kMax256> text;
            text.appendFormat( "could not write the golden %#", goldenID );
            context.recordFailure( step, text.view() );
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
                StringBuilder<constant::buffer::kMax256> line;
                line.appendFormat( "Missing localized text for key '%#'", key );
                const bool bAlreadySeen = _logCapture.containsLine( line.view() ); // 시나리오 구간에 남은 경고 줄은 위에서 이미 셌다
                // 시나리오 앞에 남은 같은 사건(`LocalizationManager` 의 `Localization.MissingKey` 줄)이다 — 같은 id 로 판정한다
                const AutomationLogResult result = _pLogPolicy->classify( LogLevel::Warning, "Localization.MissingKey", line.view() );
                if ( bAlreadySeen || result == AutomationLogResult::Declared || result == AutomationLogResult::Allowed )
                    continue;
                ++evaluation._notAllowedCount;
                StringBuilder<constant::buffer::kMax256> unhandled;
                unhandled.appendFormat( "[Warning] [LocalizationManager] (id=Localization.MissingKey) %# (before the scenario started)", line.view() );
                evaluation._listUnhandled.emplace_back( unhandled.view() );
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
            StringBuilder<constant::buffer::kMax256> failure;
            failure.appendFormat( "unhandled log: %#", line );
            _listFailure.emplace_back( failure.view() );
        }
        StringBuilder<constant::buffer::kMax512> summary;
        summary.appendFormat( "%# unhandled Warning/Error log line(s), %#"
                              " without a log id - fix the cause, declare the line with <ExpectLog>, or allow its log id in this scenario with <AllowLog id=\"...\" reason=\"...\"/>"
                              " (a line without a log id needs SW_LOG_WARNING_ID / SW_LOG_ERROR_ID first)",
                              static_cast<uint32>( _listUnhandledLog.size() ), evaluation._missingIDCount );
        return string( summary.view() );
    }

    void AutomationEvaluator::addFailure( const AutomationStep& step, string_view message, AutomationFailureKind kind )
    {
        if ( _listFailure.empty() )
            _firstFailureKind = kind;
        StringBuilder<constant::buffer::kMax256> lineBuilder;
        lineBuilder.appendFormat( "%#: %#", step.describe(), message );
        string line = string( lineBuilder.view() );
        SW_LOG_WARNING_ID( "Automation.EvaluationWarning", "[Scenario] %#", line.c_str() );
        _listFailure.push_back( std::move( line ) );
    }

    void AutomationEvaluator::prepare( const AutomationScenario& scenario )
    {
        _pLogPolicy->prepare( scenario );
        prepareWatches( scenario );
        _stored.clear();
        _listUpdatedGolden.clear();
        _listReferencedGolden.clear();
        _listNearLimitGolden.clear();
        _listGoldenSample.clear();
        _recordingCommit.clear();
        _goldenPolicy = ConfigManager::readConfig( AutomationGoldenPolicy::kConfigFile ); // 이미지 단언이 없는 시나리오는 오류를 쓰지 않는다 — 단언이 그 이유로 진다
        StringBuilder<constant::buffer::kMax1024> goldenFolder;
        goldenFolder.appendFormat( "%#/%#", FileUtil::getDirectoryPart( scenario.getSourcePath() ), kAutomationGoldenFolder );
        _goldenFolderID  = string( goldenFolder.view() );
        _goldenPrefix    = scenario.getName();
        _goldenWriteMode = AutomationGoldenUtil::readWriteMode(); // 두 깃발을 함께 줬으면 이미지 단언마다 그 이유로 진다(정책 파일과 같은 길)
        _logCapture.start();
    }
} // namespace sw
