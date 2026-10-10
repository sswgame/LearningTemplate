#include "pch.h"

#include "Engine/Automation/AutomationRunner.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/AutomationEnvironmentSteps.h"
#include "Engine/Automation/AutomationImageMetric.h"
#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Automation/AutomationWindowSteps.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/Automation/UIAutomationSteps.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    namespace
    {
        struct AutomationRunnerInternal
        {
            /** @brief 시나리오 동안 모으는 로그 줄 상한 — 넘치면 더 모으지 않는다(`ExpectLog` 는 그때까지만 센다). */
            static constexpr size_t kMaxLogLine = 20000;
            /** @brief `<ExpectImage>` 가 스크린샷을 기다리는 최대 프레임 — 렌더 큐 깊이보다 넉넉히. */
            static constexpr uint32 kMaxImageWaitFrames = 30;
            /** @brief `darkFraction` 의 기본 문턱(영역 중앙값에 곱한다)입니다. */
            static constexpr float32 kDefaultDarkRatio = 0.7f;
            /** @brief `CloseWindow` 의 기본 종료 시한(초)입니다. */
            static constexpr float32 kDefaultExitWithinSeconds = 10.0f;

            /** @brief 단계 속성 이름이 @p listAllowed 안에 있는지 봅니다. 아니면 false 와 이유. */
            static bool checkAttributeNames( const AutomationStep& step, std::initializer_list<string_view> listAllowed, string& outError )
            {
                for ( const AutomationAttribute& attribute : step._listAttribute )
                {
                    bool bKnown = false;
                    for ( const string_view allowed : listAllowed )
                    {
                        bKnown = bKnown || string_view{ attribute._name } == allowed;
                    }
                    if ( bKnown == false )
                    {
                        outError = step.describe() + ": unknown attribute '" + attribute._name + "'";
                        return false;
                    }
                }
                return true;
            }

            /** @brief 꼭 있어야 하는 속성입니다. 없으면 nullptr 와 이유. */
            static const string* requireAttribute( const AutomationStep& step, string_view name, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    outError = step.describe() + ": needs " + string( name ) + "=\"…\"";
                return pValue;
            }

            /** @brief 있으면 수로 읽습니다(없으면 @p fallback). 형식이 틀리면 false 와 이유. */
            [[nodiscard]] static bool readFloat( const AutomationStep& step, string_view name, float32 fallback, float32& outValue, string& outError )
            {
                outValue             = fallback;
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    return true;
                if ( StringUtil::parseFloat( string_view{ *pValue }, outValue ) )
                    return true;
                outError = step.describe() + ": " + string( name ) + " must be a number, got '" + *pValue + "'";
                return false;
            }

            /** @brief 있으면 음이 아닌 정수로 읽습니다(없으면 @p fallback). */
            [[nodiscard]] static bool readUint( const AutomationStep& step, string_view name, uint32 fallback, uint32& outValue, string& outError )
            {
                outValue             = fallback;
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    return true;
                int32 value = 0;
                if ( StringUtil::parseInt( string_view{ *pValue }, value ) && value >= 0 )
                {
                    outValue = static_cast<uint32>( value );
                    return true;
                }
                outError = step.describe() + ": " + string( name ) + " must be a non-negative integer, got '" + *pValue + "'";
                return false;
            }

            static bool isInputStepKind( string_view kind )
            {
                return kind == "Press" || kind == "Release" || kind == "Tap" || kind == "MouseDelta" || kind == "MousePosition" || kind == "GamepadAxis" ||
                       kind == "Text";
            }

            /** @brief 값 비교 단계(`Expect`)의 비교 하나를 읽습니다. 정확히 하나여야 합니다. */
            static bool countComparisons( const AutomationStep& step, std::initializer_list<string_view> listName )
            {
                uint32 count = 0;
                for ( const string_view name : listName )
                {
                    count += step.findAttribute( name ) != nullptr ? 1u : 0u;
                }
                return count == 1;
            }

            static string formatNumber( float64 value )
            {
                string text = to_string( value );
                // 끝의 0 을 걷는다(1.000000 → 1)
                while ( text.find( '.' ) != string::npos && ( text.back() == '0' || text.back() == '.' ) )
                {
                    const bool bDot = text.back() == '.';
                    text.pop_back();
                    if ( bDot )
                        break;
                }
                return text;
            }

            static string escapeJson( string_view text )
            {
                string escaped;
                escaped.reserve( text.size() + 8 );
                for ( const utf8 character : text )
                {
                    if ( character == '"' || character == '\\' )
                    {
                        escaped.push_back( '\\' );
                        escaped.push_back( character );
                    }
                    else if ( character == '\n' )
                    {
                        escaped += "\\n";
                    }
                    else if ( static_cast<uint8>( character ) >= 0x20 )
                    {
                        escaped.push_back( character );
                    }
                }
                return escaped;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AutomationRunner::AutomationRunner()
        : _scenario{}
        , _inputScript{}
        , _listFailure{}
        , _listLogLine{}
        , _listPendingScreenshot{}
        , _listMetricLine{}
        , _reportPath{}
        , _outputDirectory{}
        , _finishReason{}
        , _logMutex{}
        , _logListenerHandle{}
        , _pObjectManagerOverride{ nullptr }
        , _exitDeadlineSeconds{ 0.0 }
        , _nextStepIndex{ 0 }
        , _logFrameIndex{ 0 }
        , _frameIndex{ 0 }
        , _waitFrameCount{ 0 }
        , _screenshotRequestedCount{ 0 }
        , _screenshotCompletedCount{ 0 }
        , _imageWaitFrameCount{ 0 }
        , _result{ AutomationResult::Running }
        , _bStarted{ SW_FALSE }
        , _bEnded{ SW_FALSE }
        , _bListeningLog{ SW_FALSE }
        , _reserved{ 0 }
    {
        AutomationEnvironmentSteps::ensureLinked();
        AutomationWindowSteps::ensureLinked();
        UIAutomationSteps::ensureLinked();
    }

    AutomationRunner::~AutomationRunner()
    {
        if ( _bListeningLog == SW_TRUE )
            Logger::removeGlobalListener( _logListenerHandle );
    }

    void AutomationRunner::startFromPath( string_view scenarioPath, string_view reportPath )
    {
        _reportPath = string( reportPath );
        string error;
        if ( _scenario.loadFromPath( scenarioPath, error ) == false )
        {
            SW_LOG_ERROR( "[Scenario] could not load '%#': %#", string( scenarioPath ).c_str(), error.c_str() );
            finish( AutomationResult::LoadError, error );
            return;
        }
        _outputDirectory = "Saved/Automation/" + _scenario.getName();
        SW_LOG_INFO( "[Scenario] %# loaded from %# (%# step(s), fixed delta %#)", _scenario.getName().c_str(), string( scenarioPath ).c_str(),
                     static_cast<uint32>( _scenario.getSteps().size() ), _scenario.getFixedDelta() );
        if ( _scenario.getInputMode() == VirtualInputMode::Mixed )
            SW_LOG_WARNING( "[Scenario] %# uses mixed input - do not touch the keyboard or mouse while it runs", _scenario.getName().c_str() );
    }

    bool AutomationRunner::startFromText( string_view xmlText, string_view reportPath )
    {
        _reportPath = string( reportPath );
        string error;
        if ( _scenario.parse( xmlText, "<text>", error ) == false )
        {
            finish( AutomationResult::LoadError, error );
            return false;
        }
        _outputDirectory = "Saved/Automation/" + _scenario.getName();
        return true;
    }

    GameObjectManager* AutomationRunner::findActiveObjectManager() const
    {
        if ( _pObjectManagerOverride != nullptr )
            return _pObjectManagerOverride;
        if ( engine::areEngineServicesBound() == false )
            return nullptr;
        Scene* pScene = engine::getSceneManager().getActiveScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }

    void AutomationRunner::onFrameBegin( InputManager& input )
    {
        if ( _result != AutomationResult::Running )
            return;
        if ( _bStarted == SW_FALSE )
        {
            // 씬 플레이 중 = 활성 씬이 플레이를 시작했고 로딩 화면이 걷혔다(로딩 화면은 게임 입력을 막는다 — 그동안 넣은 입력은 폰에 닿지 않는다).
            const GameObjectManager* pManager  = findActiveObjectManager();
            const UISystem*          pUISystem = engine::areEngineServicesBound() ? engine::getBoundEngineServices()._pUISystem : nullptr;
            const bool               bLoading  = pUISystem != nullptr && pUISystem->isInitialized() && pUISystem->isLoadingScreenShown();
            const bool               bReady    = _scenario.getStartCondition() == AutomationStartCondition::Immediately || ( pManager != nullptr && pManager->hasBegunPlay() && bLoading == false );
            if ( bReady == false )
            {
                if ( ++_waitFrameCount > _scenario.getStartTimeoutFrames() )
                    finish( AutomationResult::TimedOut, "the start condition never became true" );
                return;
            }
            string error;
            if ( prepare( error ) == false )
            {
                finish( AutomationResult::LoadError, error );
                return;
            }
            _bStarted = SW_TRUE;
            input.attachVirtualInput( &_inputScript, _scenario.getInputMode() );
            SW_LOG_INFO( "[Scenario] %# started after %# frame(s)", _scenario.getName().c_str(), _waitFrameCount );
        }
        _logFrameIndex.store( _frameIndex, std::memory_order_relaxed );
        // 행동 층 단계(등록표의 `_bBeforeInput`)는 그 프레임의 입력 재생 · 게임 틱 전에 넣는다.
        const vector<AutomationStep>& listStep = _scenario.getSteps();
        for ( size_t stepIndex = _nextStepIndex; stepIndex < listStep.size() && listStep[stepIndex]._frameIndex == _frameIndex; ++stepIndex )
        {
            const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( listStep[stepIndex]._kind );
            if ( pRegistration != nullptr && pRegistration->_bBeforeInput && pRegistration->_pRun( *this, listStep[stepIndex] ) == false )
                finish( AutomationResult::LoadError, listStep[stepIndex].describe() + ": the step could not run" );
        }
    }

    AutomationResult AutomationRunner::onFrameEnd( InputManager& input )
    {
        if ( _result == AutomationResult::Running && _bStarted == SW_TRUE )
        {
            const vector<AutomationStep>& listStep = _scenario.getSteps();
            // `<=` — 스크린샷을 기다리느라 밀린 단계는 다음 프레임에 이어 돈다.
            while ( _nextStepIndex < listStep.size() && listStep[_nextStepIndex]._frameIndex <= _frameIndex && _result == AutomationResult::Running )
            {
                const AutomationStep& waitingStep = listStep[_nextStepIndex];
                if ( waitingStep._kind == "ExpectImage" && ( _listPendingScreenshot.empty() == false || _screenshotCompletedCount < _screenshotRequestedCount ) )
                {
                    if ( ++_imageWaitFrameCount <= AutomationRunnerInternal::kMaxImageWaitFrames )
                        break;
                    recordFailure( waitingStep, "the screenshot never completed (is there a renderer?)" );
                    _imageWaitFrameCount = 0;
                    ++_nextStepIndex;
                    continue;
                }
                _imageWaitFrameCount                            = 0;
                const AutomationStep&             step          = listStep[_nextStepIndex++];
                const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( step._kind );
                if ( pRegistration != nullptr )
                {
                    if ( pRegistration->_bBeforeInput == false && pRegistration->_pRun( *this, step ) == false )
                        finish( AutomationResult::LoadError, step.describe() + ": the step could not run" );
                }
                else if ( AutomationRunnerInternal::isInputStepKind( step._kind ) == false && runEngineStep( step ) == false )
                {
                    finish( AutomationResult::LoadError, step.describe() + ": the step could not run" );
                }
            }
            if ( _result == AutomationResult::Running && _exitDeadlineSeconds > 0.0 &&
                 static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 > _exitDeadlineSeconds )
                finish( AutomationResult::Failed, "the window was asked to close but the loop kept running past ExpectExitWithin" );
            // 끝난 프레임은 세지 않는다 — 보고의 프레임 = 끝낸 단계의 프레임.
            if ( _result == AutomationResult::Running )
                ++_frameIndex;
            // 창 닫기를 기다리는 동안은 프레임 시한을 보지 않는다(벽시계 시한이 본다).
            if ( _result == AutomationResult::Running && _exitDeadlineSeconds <= 0.0 && _frameIndex > _scenario.getTimeoutFrames() )
                finish( AutomationResult::TimedOut, "timeoutFrames reached without <Pass/>" );
        }
        if ( _result != AutomationResult::Running )
            endRun( &input );
        return _result;
    }

    AutomationResult AutomationRunner::onWindowClosed( InputManager* pInput )
    {
        if ( _result == AutomationResult::Running )
        {
            if ( _exitDeadlineSeconds > 0.0 )
            {
                const float64 nowSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
                if ( nowSeconds <= _exitDeadlineSeconds )
                    finish( AutomationResult::Passed, "the window closed within ExpectExitWithin" );
                else
                    finish( AutomationResult::Failed, "the window closed after the ExpectExitWithin deadline" );
            }
            else
            {
                finish( AutomationResult::Failed, "the window closed before the scenario ended" );
            }
        }
        endRun( pInput );
        return _result;
    }

    void AutomationRunner::recordFailure( const AutomationStep& step, string_view message )
    {
        string line = step.describe() + ": " + string( message );
        SW_LOG_WARNING( "[Scenario] %#", line.c_str() );
        _listFailure.push_back( std::move( line ) );
    }

    void AutomationRunner::finish( AutomationResult result, string_view reason )
    {
        if ( _result != AutomationResult::Running )
            return;
        _result       = ( result == AutomationResult::Passed && _listFailure.empty() == false ) ? AutomationResult::Failed : result;
        _finishReason = string( reason );
    }

    const utf8* AutomationRunner::getResultName( AutomationResult result )
    {
        switch ( result )
        {
            case AutomationResult::Running:
                return "RUNNING";
            case AutomationResult::Passed:
                return "PASS";
            case AutomationResult::Failed:
                return "FAIL";
            case AutomationResult::LoadError:
                return "LOAD ERROR";
            case AutomationResult::TimedOut:
                return "TIMEOUT";
            case AutomationResult::Skipped:
                return "SKIP";
        }
        return "UNKNOWN";
    }

    bool AutomationRunner::prepare( string& outError )
    {
        for ( const AutomationStep& step : _scenario.getSteps() )
        {
            const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( step._kind );
            if ( pRegistration != nullptr )
            {
                if ( pRegistration->_pValidate != nullptr && pRegistration->_pValidate( step, outError ) == false )
                    return false;
                continue;
            }
            if ( AutomationStepRegistry::isEngineStepKind( step._kind ) == false )
            {
                outError = step.describe() + ": unknown step (no engine step and nothing registered under that name)";
                return false;
            }
            if ( validateEngineStep( step, outError ) == false )
                return false;
            if ( AutomationRunnerInternal::isInputStepKind( step._kind ) && compileInputStep( step, outError ) == false )
                return false;
        }
        _logListenerHandle = Logger::addGlobalListener( SW_DELEGATE_METHOD( LogWrittenDelegate, &AutomationRunner::onLogWritten, this ) );
        _bListeningLog     = _logListenerHandle.isValid() ? SW_TRUE : SW_FALSE;
        return true;
    }

    bool AutomationRunner::validateEngineStep( const AutomationStep& step, string& outError ) const
    {
        using Internal          = AutomationRunnerInternal;
        const string_view kind  = step._kind;
        float32           value = 0.0f;
        uint32            count = 0;
        if ( kind == "Press" || kind == "Release" || kind == "Tap" )
        {
            if ( Internal::checkAttributeNames( step, { "slot", "hold" }, outError ) == false || Internal::readUint( step, "hold", 1, count, outError ) == false )
                return false;
            if ( kind != "Tap" && step.findAttribute( "hold" ) != nullptr )
            {
                outError = step.describe() + ": hold is only for <Tap>";
                return false;
            }
            const string* pSlot = Internal::requireAttribute( step, "slot", outError );
            InputSlot     slot{};
            if ( pSlot == nullptr )
                return false;
            if ( InputSlotUtil::tryParse( *pSlot, slot ) == false )
            {
                outError = step.describe() + ": unknown slot '" + *pSlot + "' (Key.E · Mouse.Left · Gamepad.A · Gamepad1.A)";
                return false;
            }
            return true;
        }
        if ( kind == "MouseDelta" )
        {
            return Internal::checkAttributeNames( step, { "x", "y" }, outError ) && Internal::readFloat( step, "x", 0.0f, value, outError ) &&
                   Internal::readFloat( step, "y", 0.0f, value, outError );
        }
        if ( kind == "MousePosition" )
        {
            if ( Internal::checkAttributeNames( step, { "x", "y" }, outError ) == false || Internal::requireAttribute( step, "x", outError ) == nullptr ||
                 Internal::requireAttribute( step, "y", outError ) == nullptr )
                return false;
            float32 valueY = 0.0f;
            if ( Internal::readFloat( step, "x", 0.0f, value, outError ) == false || Internal::readFloat( step, "y", 0.0f, valueY, outError ) == false )
                return false;
            if ( value < 0.0f || value > 1.0f || valueY < 0.0f || valueY > 1.0f )
            {
                outError = step.describe() + ": x and y are fractions of the client area (0..1)";
                return false;
            }
            return true;
        }
        if ( kind == "GamepadAxis" )
        {
            return Internal::checkAttributeNames( step, { "axis", "value", "pad" }, outError ) && Internal::requireAttribute( step, "axis", outError ) != nullptr &&
                   Internal::requireAttribute( step, "value", outError ) != nullptr && Internal::readUint( step, "axis", 0, count, outError ) &&
                   Internal::readFloat( step, "value", 0.0f, value, outError ) && Internal::readUint( step, "pad", 0, count, outError );
        }
        if ( kind == "Text" )
            return Internal::checkAttributeNames( step, { "value" }, outError ) && Internal::requireAttribute( step, "value", outError ) != nullptr;
        if ( kind == "Variable" )
        {
            if ( Internal::checkAttributeNames( step, { "name", "value" }, outError ) == false )
                return false;
            const string* pName = Internal::requireAttribute( step, "name", outError );
            if ( pName == nullptr || Internal::requireAttribute( step, "value", outError ) == nullptr )
                return false;
            if ( engine::areEngineServicesBound() == false || engine::getGlobalVariableManager().findVariable( *pName ) == nullptr )
            {
                outError = step.describe() + ": unknown global variable '" + *pName + "'";
                return false;
            }
            return true;
        }
        if ( kind == "Expect" )
        {
            if ( Internal::checkAttributeNames( step, { "probe", "equals", "near", "tolerance", "atLeast", "atMost" }, outError ) == false )
                return false;
            const string* pProbe = Internal::requireAttribute( step, "probe", outError );
            if ( pProbe == nullptr )
                return false;
            if ( AutomationProbes::find( *pProbe ) == nullptr )
            {
                outError = step.describe() + ": unknown probe '" + *pProbe + "' (is the game that registers it running?)";
                return false;
            }
            if ( Internal::countComparisons( step, { "equals", "near", "atLeast", "atMost" } ) == false )
            {
                outError = step.describe() + ": needs exactly one of equals · near · atLeast · atMost";
                return false;
            }
            return Internal::readFloat( step, "equals", 0.0f, value, outError ) && Internal::readFloat( step, "near", 0.0f, value, outError ) &&
                   Internal::readFloat( step, "tolerance", 0.0f, value, outError ) && Internal::readFloat( step, "atLeast", 0.0f, value, outError ) &&
                   Internal::readFloat( step, "atMost", 0.0f, value, outError );
        }
        if ( kind == "ExpectLog" )
        {
            if ( Internal::checkAttributeNames( step, { "contains", "count", "atLeast", "since" }, outError ) == false ||
                 Internal::requireAttribute( step, "contains", outError ) == nullptr )
                return false;
            if ( Internal::countComparisons( step, { "count", "atLeast" } ) == false )
            {
                outError = step.describe() + ": needs exactly one of count · atLeast";
                return false;
            }
            return Internal::readUint( step, "count", 0, count, outError ) && Internal::readUint( step, "atLeast", 0, count, outError ) &&
                   Internal::readUint( step, "since", 0, count, outError );
        }
        if ( kind == "Screenshot" )
            return Internal::checkAttributeNames( step, { "file" }, outError ) && Internal::requireAttribute( step, "file", outError ) != nullptr;
        if ( kind == "ExpectImage" )
        {
            if ( Internal::checkAttributeNames( step, { "file", "region", "metric", "ratio", "reference", "equals", "near", "tolerance", "atLeast", "atMost" }, outError ) ==
                     false ||
                 Internal::requireAttribute( step, "file", outError ) == nullptr )
                return false;
            const string*             pMetric = Internal::requireAttribute( step, "metric", outError );
            AutomationImageMetricKind metric  = AutomationImageMetricKind::MeanLuma;
            if ( pMetric == nullptr )
                return false;
            if ( AutomationImageMetric::tryParseKind( *pMetric, metric ) == false )
            {
                outError = step.describe() + ": unknown metric '" + *pMetric + "' (meanLuma · darkFraction · meanRedMinusBlue · differentFrom)";
                return false;
            }
            const string*         pRegion = step.findAttribute( "region" );
            AutomationImageRegion region{};
            if ( pRegion != nullptr && AutomationImageMetric::tryParseRegion( *pRegion, region ) == false )
            {
                outError = step.describe() + ": region must be x0,y0,x1,y1 in 0..1 with x0<x1 and y0<y1, got '" + *pRegion + "'";
                return false;
            }
            if ( ( metric == AutomationImageMetricKind::DifferentFrom ) != ( step.findAttribute( "reference" ) != nullptr ) )
            {
                outError = step.describe() + ": reference=\"…\" goes with metric=\"differentFrom\" (and only with it)";
                return false;
            }
            if ( Internal::countComparisons( step, { "equals", "near", "atLeast", "atMost" } ) == false )
            {
                outError = step.describe() + ": needs exactly one of equals · near · atLeast · atMost";
                return false;
            }
            return Internal::readFloat( step, "ratio", 0.0f, value, outError ) && Internal::readFloat( step, "equals", 0.0f, value, outError ) &&
                   Internal::readFloat( step, "near", 0.0f, value, outError ) && Internal::readFloat( step, "tolerance", 0.0f, value, outError ) &&
                   Internal::readFloat( step, "atLeast", 0.0f, value, outError ) && Internal::readFloat( step, "atMost", 0.0f, value, outError );
        }
        if ( kind == "CloseWindow" || kind == "ExpectExitWithin" )
        {
            const utf8* pName = kind == "CloseWindow" ? "withinSeconds" : "seconds";
            return Internal::checkAttributeNames( step, { pName }, outError ) && Internal::readFloat( step, pName, 0.0f, value, outError );
        }
        if ( kind == "Pass" )
            return Internal::checkAttributeNames( step, {}, outError );
        if ( kind == "Fail" || kind == "Skip" )
            return Internal::checkAttributeNames( step, { "reason" }, outError );
        outError = step.describe() + ": this engine step is not supported yet";
        return false;
    }

    bool AutomationRunner::compileInputStep( const AutomationStep& step, string& outError )
    {
        const string_view kind   = step._kind;
        const uint32      frame  = step._frameIndex;
        float32           valueX = 0.0f;
        float32           valueY = 0.0f;
        if ( kind == "Press" || kind == "Release" || kind == "Tap" )
        {
            InputSlot slot{};
            (void)InputSlotUtil::tryParse( *step.findAttribute( "slot" ), slot ); // validateEngineStep 이 이미 봤다
            uint32 holdFrameCount = 1;
            (void)AutomationRunnerInternal::readUint( step, "hold", 1, holdFrameCount, outError ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            const bool bAdded = kind == "Tap" ? _inputScript.addTap( frame, slot, holdFrameCount ) : _inputScript.addSlot( frame, slot, kind == "Press" );
            if ( bAdded == false )
                outError = step.describe() + ": that slot cannot be pressed (an axis or an unknown device)";
            return bAdded;
        }
        if ( kind == "MouseDelta" )
        {
            (void)AutomationRunnerInternal::readFloat( step, "x", 0.0f, valueX, outError ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "y", 0.0f, valueY, outError ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            _inputScript.addMouseDelta( frame, valueX, valueY );
            return true;
        }
        if ( kind == "MousePosition" )
        {
            // 창 클라이언트 영역의 비율 → 픽셀(시작할 때의 창 크기). 창이 없으면(헤드리스) 그 자리를 낼 수 없다.
            const IWindow* pWindow = IWindow::getActiveWindow();
            if ( pWindow == nullptr || pWindow->getWidth() == 0 || pWindow->getHeight() == 0 )
            {
                outError = step.describe() + ": no window to place the pointer in";
                return false;
            }
            (void)AutomationRunnerInternal::readFloat( step, "x", 0.0f, valueX, outError ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "y", 0.0f, valueY, outError ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            const float32 maxX = static_cast<float32>( pWindow->getWidth() - 1 );
            const float32 maxY = static_cast<float32>( pWindow->getHeight() - 1 );
            _inputScript.addMousePosition( frame, static_cast<int32>( MathUtil::round( valueX * maxX ) ), static_cast<int32>( MathUtil::round( valueY * maxY ) ) );
            return true;
        }
        if ( kind == "GamepadAxis" )
        {
            uint32 axisIndex = 0;
            uint32 padIndex  = 0;
            (void)AutomationRunnerInternal::readUint( step, "axis", 0, axisIndex, outError );   // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "value", 0.0f, valueX, outError ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            (void)AutomationRunnerInternal::readUint( step, "pad", 0, padIndex, outError );     // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            _inputScript.addGamepadAxis( frame, static_cast<uint16>( axisIndex ), valueX, static_cast<uint8>( padIndex ) );
            return true;
        }
        _inputScript.addEvent( frame, RawInputEvent::makeTextInput( *step.findAttribute( "value" ) ) ); // Text
        return true;
    }

    bool AutomationRunner::runEngineStep( const AutomationStep& step )
    {
        const string_view kind = step._kind;
        string            error;
        if ( kind == "Variable" )
        {
            const string& name = *step.findAttribute( "name" );
            if ( engine::getGlobalVariableManager().setValueFromString( name, *step.findAttribute( "value" ) ) == false )
                recordFailure( step, "could not set " + name + " to '" + *step.findAttribute( "value" ) + "'" );
            return true;
        }
        if ( kind == "Expect" )
        {
            runExpect( step );
            return true;
        }
        if ( kind == "ExpectLog" )
        {
            runExpectLog( step );
            return true;
        }
        if ( kind == "Screenshot" )
        {
            const string path      = resolveOutputPath( *step.findAttribute( "file" ) );
            const string directory = FileUtil::getDirectoryPart( path );
            if ( directory.empty() == false )
                (void)FileUtil::ensureDirectoryExists( directory );
            (void)FileUtil::tryRemoveFile( path ); // 지난 실행의 그림을 읽지 않게
            _listPendingScreenshot.push_back( path );
            ++_screenshotRequestedCount;
            return true;
        }
        if ( kind == "ExpectImage" )
        {
            runExpectImage( step );
            return true;
        }
        if ( kind == "CloseWindow" || kind == "ExpectExitWithin" )
        {
            const bool bClose  = kind == "CloseWindow";
            float32    seconds = 0.0f;
            // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, bClose ? "withinSeconds" : "seconds", AutomationRunnerInternal::kDefaultExitWithinSeconds, seconds, error );
            _exitDeadlineSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 + static_cast<float64>( seconds );
            if ( bClose )
            {
                IWindow* pWindow = IWindow::getActiveWindow();
                if ( pWindow == nullptr )
                    recordFailure( step, "there is no window to close" );
                else
                    pWindow->requestClose();
            }
            return true;
        }
        if ( kind == "Pass" )
        {
            finish( AutomationResult::Passed, "<Pass/>" );
            return true;
        }
        const string* pReason = step.findAttribute( "reason" );
        if ( kind == "Fail" )
        {
            recordFailure( step, pReason != nullptr ? string_view{ *pReason } : string_view{ "<Fail/>" } );
            finish( AutomationResult::Failed, pReason != nullptr ? string_view{ *pReason } : string_view{ "<Fail/>" } );
            return true;
        }
        if ( kind == "Skip" )
        {
            finish( AutomationResult::Skipped, pReason != nullptr ? string_view{ *pReason } : string_view{ "<Skip/>" } );
            return true;
        }
        return false;
    }

    void AutomationRunner::runExpect( const AutomationStep& step )
    {
        const string&                      probeName     = *step.findAttribute( "probe" );
        const AutomationProbeRegistration* pRegistration = AutomationProbes::find( probeName );
        float64                            value         = 0.0;
        // 모듈을 내렸다 올리면 탐침이 빠질 수 있다 — 시작 때 있었어도 다시 찾는다.
        if ( pRegistration == nullptr || pRegistration->_pFunction( findActiveObjectManager(), value ) == false )
        {
            recordFailure( step, pRegistration == nullptr ? probeName + " is no longer registered" : probeName + " has no value (no target in the scene)" );
            return;
        }
        string  error;
        float32 expected  = 0.0f;
        float32 tolerance = 0.0f;
        bool    bOk       = true;
        string  rule;
        if ( step.findAttribute( "equals" ) != nullptr )
        {
            (void)AutomationRunnerInternal::readFloat( step, "equals", 0.0f, expected, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            bOk  = value == static_cast<float64>( expected );
            rule = "== " + *step.findAttribute( "equals" );
        }
        else if ( step.findAttribute( "near" ) != nullptr )
        {
            (void)AutomationRunnerInternal::readFloat( step, "near", 0.0f, expected, error );          // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "tolerance", 1.0e-4f, tolerance, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            bOk  = value >= static_cast<float64>( expected ) - static_cast<float64>( tolerance ) && value <= static_cast<float64>( expected ) + static_cast<float64>( tolerance );
            rule = "near " + *step.findAttribute( "near" );
        }
        else if ( step.findAttribute( "atLeast" ) != nullptr )
        {
            (void)AutomationRunnerInternal::readFloat( step, "atLeast", 0.0f, expected, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            bOk  = value >= static_cast<float64>( expected );
            rule = ">= " + *step.findAttribute( "atLeast" );
        }
        else
        {
            (void)AutomationRunnerInternal::readFloat( step, "atMost", 0.0f, expected, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            bOk  = value <= static_cast<float64>( expected );
            rule = "<= " + *step.findAttribute( "atMost" );
        }
        if ( bOk == false )
            recordFailure( step, "Expect " + probeName + " " + rule + ", got " + AutomationRunnerInternal::formatNumber( value ) );
    }

    void AutomationRunner::runExpectLog( const AutomationStep& step )
    {
        const string& needle = *step.findAttribute( "contains" );
        string        error;
        uint32        since = 0;
        (void)AutomationRunnerInternal::readUint( step, "since", 0, since, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
        uint32 matchCount = 0;
        {
            std::scoped_lock<mutex> lock{ _logMutex };
            for ( const AutomationLogLine& line : _listLogLine )
            {
                if ( line._frameIndex >= since && line._message.find( needle ) != string::npos )
                    ++matchCount;
            }
        }
        uint32 expected = 0;
        if ( step.findAttribute( "count" ) != nullptr )
        {
            (void)AutomationRunnerInternal::readUint( step, "count", 0, expected, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            if ( matchCount != expected )
                recordFailure( step, "ExpectLog '" + needle + "' count == " + to_string( expected ) + ", got " + to_string( matchCount ) );
            return;
        }
        (void)AutomationRunnerInternal::readUint( step, "atLeast", 0, expected, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
        if ( matchCount < expected )
            recordFailure( step, "ExpectLog '" + needle + "' count >= " + to_string( expected ) + ", got " + to_string( matchCount ) );
    }

    void AutomationRunner::runExpectImage( const AutomationStep& step )
    {
        using Internal = AutomationRunnerInternal;
        string                    error;
        AutomationImageMetricKind metric = AutomationImageMetricKind::MeanLuma;
        AutomationImageRegion     region{};
        float32                   ratio = Internal::kDefaultDarkRatio;
        (void)AutomationImageMetric::tryParseKind( *step.findAttribute( "metric" ), metric ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
        const string* pRegion = step.findAttribute( "region" );
        if ( pRegion != nullptr )
            (void)AutomationImageMetric::tryParseRegion( *pRegion, region );                   // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
        (void)Internal::readFloat( step, "ratio", Internal::kDefaultDarkRatio, ratio, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다

        const string    path = resolveOutputPath( *step.findAttribute( "file" ) );
        AutomationImage image;
        AutomationImage reference;
        const string*   pReference = step.findAttribute( "reference" );
        if ( AutomationImageMetric::loadPpm( path, image, error ) == false ||
             ( pReference != nullptr && AutomationImageMetric::loadPpm( resolveOutputPath( *pReference ), reference, error ) == false ) )
        {
            recordFailure( step, "could not read the image: " + error );
            return;
        }
        float64 value = 0.0;
        if ( AutomationImageMetric::measure( image, region, metric, ratio, pReference != nullptr ? &reference : nullptr, value, error ) == false )
        {
            recordFailure( step, error );
            return;
        }
        // 값은 늘 적는다 — 문턱을 정할 때 숫자를 본다.
        string line = *step.findAttribute( "metric" ) + "(" + ( pRegion != nullptr ? *pRegion : string( "0,0,1,1" ) ) + ") " + *step.findAttribute( "file" ) + " = " +
                      Internal::formatNumber( value );
        SW_LOG_INFO( "[Scenario] metric %#", line.c_str() );
        _listMetricLine.push_back( std::move( line ) );

        float32 expected  = 0.0f;
        float32 tolerance = 0.0f;
        bool    bOk       = true;
        string  rule;
        if ( step.findAttribute( "equals" ) != nullptr )
        {
            (void)Internal::readFloat( step, "equals", 0.0f, expected, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            bOk  = value == static_cast<float64>( expected );
            rule = "== " + *step.findAttribute( "equals" );
        }
        else if ( step.findAttribute( "near" ) != nullptr )
        {
            (void)Internal::readFloat( step, "near", 0.0f, expected, error );          // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            (void)Internal::readFloat( step, "tolerance", 1.0e-3f, tolerance, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            bOk  = MathUtil::abs( value - static_cast<float64>( expected ) ) <= static_cast<float64>( tolerance );
            rule = "near " + *step.findAttribute( "near" );
        }
        else if ( step.findAttribute( "atLeast" ) != nullptr )
        {
            (void)Internal::readFloat( step, "atLeast", 0.0f, expected, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            bOk  = value >= static_cast<float64>( expected );
            rule = ">= " + *step.findAttribute( "atLeast" );
        }
        else
        {
            (void)Internal::readFloat( step, "atMost", 0.0f, expected, error ); // validateEngineStep 이 이미 읽어 봤다 — 실패할 수 없다
            bOk  = value <= static_cast<float64>( expected );
            rule = "<= " + *step.findAttribute( "atMost" );
        }
        if ( bOk == false )
            recordFailure( step, "ExpectImage " + *step.findAttribute( "metric" ) + " " + rule + ", got " + Internal::formatNumber( value ) );
    }

    string AutomationRunner::resolveOutputPath( string_view file ) const
    {
        const bool bAbsolute = file.empty() == false && ( file[0] == '/' || file[0] == '\\' || ( file.size() > 1 && file[1] == ':' ) );
        return bAbsolute ? string( file ) : _outputDirectory + "/" + string( file );
    }

    bool AutomationRunner::takePendingScreenshotPath( string& outPath )
    {
        if ( _listPendingScreenshot.empty() )
            return false;
        outPath = _listPendingScreenshot.front();
        _listPendingScreenshot.erase( _listPendingScreenshot.begin() );
        return true;
    }

    void AutomationRunner::onLogWritten( const LogEntry& entry )
    {
        // 실행기 자신의 줄([Scenario])은 세지 않는다 — 실패 줄이 단언 문구를 되풀이해 ExpectLog 를 맞추지 않게.
        if ( StringUtil::startsWith( string_view{ entry._message }, "[Scenario]" ) )
            return;
        std::scoped_lock<mutex> lock{ _logMutex };
        if ( _listLogLine.size() >= AutomationRunnerInternal::kMaxLogLine )
            return;
        AutomationLogLine line{};
        line._message    = entry._message;
        line._frameIndex = _logFrameIndex.load( std::memory_order_relaxed );
        _listLogLine.push_back( std::move( line ) );
    }

    void AutomationRunner::endRun( InputManager* pInput )
    {
        if ( _bEnded == SW_TRUE )
            return;
        _bEnded = SW_TRUE;
        if ( pInput != nullptr && pInput->getVirtualInput() == &_inputScript )
            pInput->detachVirtualInput();
        if ( _bListeningLog == SW_TRUE )
        {
            Logger::removeGlobalListener( _logListenerHandle );
            _bListeningLog = SW_FALSE;
        }
        if ( _result == AutomationResult::Passed )
        {
            SW_LOG_INFO( "[Scenario] PASS %# (%# frame(s))", _scenario.getName().c_str(), _frameIndex );
        }
        else if ( _result == AutomationResult::Skipped )
        {
            SW_LOG_WARNING( "[Scenario] SKIP %#: %#", _scenario.getName().c_str(), _finishReason.c_str() );
        }
        else
        {
            string detail = _finishReason;
            for ( const string& failure : _listFailure )
            {
                detail += "\n    ";
                detail += failure;
            }
            SW_LOG_ERROR( "[Scenario] %# %# at frame %#: %#", getResultName( _result ), _scenario.getName().c_str(), _frameIndex, detail.c_str() );
        }
        writeReport();
    }

    void AutomationRunner::writeReport() const
    {
        if ( _reportPath.empty() )
            return;
        using Internal = AutomationRunnerInternal;
        string json    = "{\n  \"name\": \"" + Internal::escapeJson( _scenario.getName() ) + "\",\n";
        json += "  \"result\": \"" + string( getResultName( _result ) ) + "\",\n";
        json += "  \"exitCode\": " + to_string( static_cast<int32>( _result ) ) + ",\n";
        json += "  \"frames\": " + to_string( _frameIndex ) + ",\n";
        json += "  \"reason\": \"" + Internal::escapeJson( _finishReason ) + "\",\n";
        json += "  \"failures\": [";
        for ( size_t index = 0; index < _listFailure.size(); ++index )
        {
            json += index == 0 ? "\n    \"" : ",\n    \"";
            json += Internal::escapeJson( _listFailure[index] ) + "\"";
        }
        json += _listFailure.empty() ? "],\n" : "\n  ],\n";
        json += "  \"metrics\": [";
        for ( size_t index = 0; index < _listMetricLine.size(); ++index )
        {
            json += index == 0 ? "\n    \"" : ",\n    \"";
            json += Internal::escapeJson( _listMetricLine[index] ) + "\"";
        }
        json += _listMetricLine.empty() ? "]\n}\n" : "\n  ]\n}\n";
        const string directory = FileUtil::getDirectoryPart( _reportPath );
        if ( directory.empty() == false )
            (void)FileUtil::ensureDirectoryExists( directory );
        if ( FileUtil::writeTextFile( _reportPath, json ) == false )
            SW_LOG_WARNING( "[Scenario] could not write the report %#", _reportPath.c_str() );
    }
} // namespace sw
