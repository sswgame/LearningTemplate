#include "pch.h"

#include "Engine/Automation/AutomationRunner.h"

#include "Core/Console/DevCommandRegistry.h"
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
#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    namespace
    {
        struct AutomationRunnerInternal
        {
            /** @brief 시나리오 동안 모으는 로그 줄 상한입니다. 넘치면 더 모으지 않습니다(`ExpectLog` 는 그때까지만 셉니다). */
            static constexpr size_t kMaxLogLine = 20000;
            /** @brief `<ExpectImage>` 가 스크린샷을 기다리는 최대 프레임입니다. 렌더 큐 깊이보다 넉넉하게 잡습니다. */
            static constexpr uint32 kMaxImageWaitFrames = 30;
            /** @brief `darkFraction` 의 기본 임계값(영역 중앙값에 곱한다)입니다. */
            static constexpr float32 kDefaultDarkRatio = 0.7f;
            /** @brief `CloseWindow` 의 기본 종료 시한(초)입니다. */
            static constexpr float32 kDefaultExitWithinSeconds = 10.0f;
            /** @brief `WaitUntil` 의 기본 시한(프레임)입니다. 부하가 걸린 기계에서 에디터가 에셋을 여는 데 드는 시간보다 넉넉하게 잡습니다. */
            static constexpr uint32 kDefaultWaitTimeoutFrames = 1200;

            /**
             * @brief 이 구성에 없는 단계가 들어 있는 시나리오면 그 첫 단계를 반환합니다. 개발 명령(`DevCommand`)은 Shipping 에서 빠집니다.
             * @details 그런 시나리오는 이 구성에서 돌 수 없을 뿐 틀린 것이 아니다. 읽기 오류(11)가 아니라 건너뜀(13)으로 처리한다
             *          (언리얼 자동화 테스트가 구성에 없는 문맥의 테스트를 목록에서 빼는 것과 같다).
             */
            static const AutomationStep* findStepMissingFromThisBuild( [[maybe_unused]] const AutomationScenario& scenario )
            {
#if SW_DEV_COMMANDS_ENABLED
                return nullptr;
#else
                for ( const AutomationStep& step : scenario.getSteps() )
                {
                    if ( string_view{ step._kind } == AutomationEnvironmentSteps::kDevCommandKind )
                        return &step;
                }
                return nullptr;
#endif
            }

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

            // --- 엔진 단계의 검사 함수(`AutomationStepValidateFunction`). 속성의 허용 값과 오류 메시지가 여기 있다 -------------

            /** @brief `Press` · `Release` · `Tap` 입니다. `hold` 는 `Tap` 에만 씁니다. */
            static bool validateSlotStep( const AutomationStep& step, string& outError )
            {
                uint32 count = 0;
                if ( checkAttributeNames( step, { "slot", "hold" }, outError ) == false || readUint( step, "hold", 1, count, outError ) == false )
                    return false;
                if ( string_view{ step._kind } != "Tap" && step.findAttribute( "hold" ) != nullptr )
                {
                    outError = step.describe() + ": hold is only for <Tap>";
                    return false;
                }
                const string* pSlot = requireAttribute( step, "slot", outError );
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

            static bool validateMouseDelta( const AutomationStep& step, string& outError )
            {
                float32 value = 0.0f;
                return checkAttributeNames( step, { "x", "y" }, outError ) && readFloat( step, "x", 0.0f, value, outError ) && readFloat( step, "y", 0.0f, value, outError );
            }

            static bool validateMousePosition( const AutomationStep& step, string& outError )
            {
                if ( checkAttributeNames( step, { "x", "y" }, outError ) == false || requireAttribute( step, "x", outError ) == nullptr ||
                     requireAttribute( step, "y", outError ) == nullptr )
                    return false;
                float32 valueX = 0.0f;
                float32 valueY = 0.0f;
                if ( readFloat( step, "x", 0.0f, valueX, outError ) == false || readFloat( step, "y", 0.0f, valueY, outError ) == false )
                    return false;
                if ( valueX < 0.0f || valueX > 1.0f || valueY < 0.0f || valueY > 1.0f )
                {
                    outError = step.describe() + ": x and y are fractions of the client area (0..1)";
                    return false;
                }
                return true;
            }

            static bool validateGamepadAxis( const AutomationStep& step, string& outError )
            {
                float32 value = 0.0f;
                uint32  count = 0;
                return checkAttributeNames( step, { "axis", "value", "pad" }, outError ) && requireAttribute( step, "axis", outError ) != nullptr &&
                       requireAttribute( step, "value", outError ) != nullptr && readUint( step, "axis", 0, count, outError ) && readFloat( step, "value", 0.0f, value, outError ) &&
                       readUint( step, "pad", 0, count, outError );
            }

            static bool validateText( const AutomationStep& step, string& outError )
            {
                return checkAttributeNames( step, { "value" }, outError ) && requireAttribute( step, "value", outError ) != nullptr;
            }

            static bool validateVariable( const AutomationStep& step, string& outError )
            {
                if ( checkAttributeNames( step, { "name", "value" }, outError ) == false )
                    return false;
                const string* pName = requireAttribute( step, "name", outError );
                if ( pName == nullptr || requireAttribute( step, "value", outError ) == nullptr )
                    return false;
                if ( engine::areEngineServicesBound() == false || engine::getGlobalVariableManager().findVariable( *pName ) == nullptr )
                {
                    outError = step.describe() + ": unknown global variable '" + *pName + "'";
                    return false;
                }
                return true;
            }

            /** @brief 탐침 단계(`Expect`, `WaitUntil`)의 공통 부분입니다. 탐침이 있고 비교가 정확히 하나이며 수로 읽혀야 합니다. */
            static bool validateProbeComparison( const AutomationStep& step, string& outError )
            {
                const string* pProbe = requireAttribute( step, "probe", outError );
                if ( pProbe == nullptr )
                    return false;
                if ( AutomationProbes::find( *pProbe ) == nullptr )
                {
                    outError = step.describe() + ": unknown probe '" + *pProbe + "' (is the game that registers it running?)";
                    return false;
                }
                if ( countComparisons( step, { "equals", "near", "atLeast", "atMost" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of equals · near · atLeast · atMost";
                    return false;
                }
                float32 value = 0.0f;
                return readFloat( step, "equals", 0.0f, value, outError ) && readFloat( step, "near", 0.0f, value, outError ) &&
                       readFloat( step, "tolerance", 0.0f, value, outError ) && readFloat( step, "atLeast", 0.0f, value, outError ) &&
                       readFloat( step, "atMost", 0.0f, value, outError );
            }

            static bool validateExpect( const AutomationStep& step, string& outError )
            {
                return checkAttributeNames( step, { "probe", "equals", "near", "tolerance", "atLeast", "atMost" }, outError ) && validateProbeComparison( step, outError );
            }

            /** @brief `WaitUntil` 입니다. 그 프레임에 혼자 있는지는 준비 함수(`AutomationRunnerEngineSteps::prepareWaitUntil`)가 시나리오 전체를 보고 확인합니다. */
            static bool validateWaitUntil( const AutomationStep& step, string& outError )
            {
                uint32 count = 0;
                return checkAttributeNames( step, { "probe", "equals", "near", "tolerance", "atLeast", "atMost", "timeoutFrames" }, outError ) &&
                       readUint( step, "timeoutFrames", kDefaultWaitTimeoutFrames, count, outError ) && validateProbeComparison( step, outError );
            }

            static bool validateExpectLog( const AutomationStep& step, string& outError )
            {
                if ( checkAttributeNames( step, { "contains", "count", "atLeast", "since" }, outError ) == false || requireAttribute( step, "contains", outError ) == nullptr )
                    return false;
                if ( countComparisons( step, { "count", "atLeast" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of count · atLeast";
                    return false;
                }
                uint32 count = 0;
                return readUint( step, "count", 0, count, outError ) && readUint( step, "atLeast", 0, count, outError ) && readUint( step, "since", 0, count, outError );
            }

            static bool validateScreenshot( const AutomationStep& step, string& outError )
            {
                return checkAttributeNames( step, { "file" }, outError ) && requireAttribute( step, "file", outError ) != nullptr;
            }

            static bool validateExpectImage( const AutomationStep& step, string& outError )
            {
                if ( checkAttributeNames( step, { "file", "region", "metric", "ratio", "reference", "equals", "near", "tolerance", "atLeast", "atMost" }, outError ) == false ||
                     requireAttribute( step, "file", outError ) == nullptr )
                    return false;
                const string*             pMetric = requireAttribute( step, "metric", outError );
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
                if ( countComparisons( step, { "equals", "near", "atLeast", "atMost" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of equals · near · atLeast · atMost";
                    return false;
                }
                float32 value = 0.0f;
                return readFloat( step, "ratio", 0.0f, value, outError ) && readFloat( step, "equals", 0.0f, value, outError ) && readFloat( step, "near", 0.0f, value, outError ) &&
                       readFloat( step, "tolerance", 0.0f, value, outError ) && readFloat( step, "atLeast", 0.0f, value, outError ) &&
                       readFloat( step, "atMost", 0.0f, value, outError );
            }

            /** @brief 종료 시한 단계의 시간 속성 @p pName 하나만 받습니다. */
            static bool validateExitSeconds( const AutomationStep& step, const utf8* pName, string& outError )
            {
                float32 value = 0.0f;
                return checkAttributeNames( step, { pName }, outError ) && readFloat( step, pName, 0.0f, value, outError );
            }

            static bool validateCloseWindow( const AutomationStep& step, string& outError ) { return validateExitSeconds( step, "withinSeconds", outError ); }

            static bool validateExpectExitWithin( const AutomationStep& step, string& outError ) { return validateExitSeconds( step, "seconds", outError ); }

            static bool validatePass( const AutomationStep& step, string& outError ) { return checkAttributeNames( step, {}, outError ); }

            static bool validateFailOrSkip( const AutomationStep& step, string& outError ) { return checkAttributeNames( step, { "reason" }, outError ); }

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
                // 끝의 0 을 정리한다(1.000000 → 1)
                while ( text.find( '.' ) != string::npos && ( text.back() == '0' || text.back() == '.' ) )
                {
                    const bool bDot = text.back() == '.';
                    text.pop_back();
                    if ( bDot )
                        break;
                }
                return text;
            }

            static string escapeJSON( string_view text )
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

            /** @brief 탐침 `Input.OsDeviceInputBlocked` 입니다. 무인 자동화가 사람의 마우스와 키보드를 창 메시지 단계에서 버리는 중이면 1 입니다. */
            [[nodiscard]] static bool readOsDeviceInputBlocked( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                if ( engine::areEngineServicesBound() == false )
                    return false;
                outValue = engine::getInputManager().isOsDeviceInputBlocked() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 탐침 `Scene.ObjectCount` 입니다. 활성 씬의 오브젝트 수입니다(핫 리로드 · 백엔드 교체 앞뒤로 씬이 그대로인지 볼 때 씁니다). */
            [[nodiscard]] static bool readSceneObjectCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 count = 0;
                pManager->forEachGameObject( [&count]( GameObject* /*pObject*/ )
                { ++count; } );
                outValue = static_cast<float64>( count );
                return true;
            }

#if SW_PROFILER_TRACE_ENABLED
            /** @brief 탐침 `Profiler.TraceRecording` 입니다. 긴 캡처(`profile.trace record`)가 아직 모으는 중이면 1 입니다. */
            [[nodiscard]] static bool readTraceRecording( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                outValue = ProfilerTraceExport::isRecording() ? 1.0 : 0.0;
                return true;
            }
#endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_AUTOMATION_PROBE( inputOsDeviceInputBlocked, "Input.OsDeviceInputBlocked", "1 while unattended automation drops the person's mouse and keyboard window events",
                         &AutomationRunnerInternal::readOsDeviceInputBlocked );
    SW_AUTOMATION_PROBE( sceneObjectCount, "Scene.ObjectCount", "Objects in the active scene", &AutomationRunnerInternal::readSceneObjectCount );
#if SW_PROFILER_TRACE_ENABLED
    // 긴 캡처는 벽시계로 끝난다. 시나리오는 고정 프레임이 아니라 이 탐침을 `WaitUntil` 로 기다린다(빠른 백엔드는 몇 백 프레임이 0.3 초 안에 돈다).
    SW_AUTOMATION_PROBE( profilerTraceRecording, "Profiler.TraceRecording", "1 while a profile.trace record capture is still collecting",
                         &AutomationRunnerInternal::readTraceRecording );
#endif
} // namespace sw

namespace sw
{
    /**
     * @struct AutomationRunnerEngineSteps
     * @brief 실행기 상태(가상 입력 원천, 스크린샷 대기열, 종료 시한)를 직접 만지는 엔진 단계의 준비 · 실행 · 차례 함수입니다. `AutomationRunner` 의 friend 입니다.
     * @details 엔진 단계도 다른 모듈의 단계와 같이 `AutomationStepRegistry` 에 등록한다. 실행기는 단계 이름으로 분기하지 않는다.
     */
    struct AutomationRunnerEngineSteps
    {
        // --- 준비(`AutomationStepPrepareFunction`): 입력 단계는 가상 입력 원천으로 옮긴다. 속성은 검사 함수가 이미 읽어 봤으므로 다시 읽기는 실패할 수 없다 ---

        static bool prepareSlotStep( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            InputSlot slot{};
            (void)InputSlotUtil::tryParse( *step.findAttribute( "slot" ), slot ); // 검사 함수가 이미 확인했다
            uint32 holdFrameCount = 1;
            (void)AutomationRunnerInternal::readUint( step, "hold", 1, holdFrameCount, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            const string_view kind   = step._kind;
            const bool        bAdded = kind == "Tap" ? runner._inputScript.addTap( step._frameIndex, slot, holdFrameCount )
                                                     : runner._inputScript.addSlot( step._frameIndex, slot, kind == "Press" );
            if ( bAdded == false )
                outError = step.describe() + ": that slot cannot be pressed (an axis or an unknown device)";
            return bAdded;
        }

        static bool prepareMouseDelta( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            float32 valueX = 0.0f;
            float32 valueY = 0.0f;
            (void)AutomationRunnerInternal::readFloat( step, "x", 0.0f, valueX, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "y", 0.0f, valueY, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            runner._inputScript.addMouseDelta( step._frameIndex, valueX, valueY );
            return true;
        }

        static bool prepareMousePosition( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            // 창 클라이언트 영역의 비율을 픽셀로 바꾼다(시작할 때의 창 크기). 창이 없으면(헤드리스) 그 위치를 낼 수 없다.
            const IWindow* pWindow = IWindow::getActiveWindow();
            if ( pWindow == nullptr || pWindow->getWidth() == 0 || pWindow->getHeight() == 0 )
            {
                outError = step.describe() + ": no window to place the pointer in";
                return false;
            }
            float32 valueX = 0.0f;
            float32 valueY = 0.0f;
            (void)AutomationRunnerInternal::readFloat( step, "x", 0.0f, valueX, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "y", 0.0f, valueY, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            const float32 maxX = static_cast<float32>( pWindow->getWidth() - 1 );
            const float32 maxY = static_cast<float32>( pWindow->getHeight() - 1 );
            runner._inputScript.addMousePosition( step._frameIndex, static_cast<int32>( MathUtil::round( valueX * maxX ) ), static_cast<int32>( MathUtil::round( valueY * maxY ) ) );
            return true;
        }

        static bool prepareGamepadAxis( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            uint32  axisIndex = 0;
            uint32  padIndex  = 0;
            float32 value     = 0.0f;
            (void)AutomationRunnerInternal::readUint( step, "axis", 0, axisIndex, outError );  // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "value", 0.0f, value, outError ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)AutomationRunnerInternal::readUint( step, "pad", 0, padIndex, outError );    // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            runner._inputScript.addGamepadAxis( step._frameIndex, static_cast<uint16>( axisIndex ), value, static_cast<uint8>( padIndex ) );
            return true;
        }

        static bool prepareText( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            (void)outError;
            runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeTextInput( *step.findAttribute( "value" ) ) );
            return true;
        }

        /** @brief 붙잡는 동안 같은 프레임의 다른 단계가 여러 번 돌거나(입력 전 단계) 조건보다 먼저 돌지 않게, 기다림은 그 프레임에 혼자 둔다. */
        static bool prepareWaitUntil( AutomationRunner& runner, const AutomationStep& step, string& outError )
        {
            for ( const AutomationStep& otherStep : runner._scenario.getSteps() )
            {
                if ( &otherStep != &step && otherStep._frameIndex == step._frameIndex )
                {
                    outError = step.describe() + ": <WaitUntil> must be the only step at its frame (move " + otherStep.describe() + ")";
                    return false;
                }
            }
            return true;
        }

        // --- 실행(`AutomationStepFunction`): 씬 틱 뒤에 돈다 -------------------------------------------------------

        static bool runVariable( AutomationRunner& runner, const AutomationStep& step )
        {
            const string& name = *step.findAttribute( "name" );
            if ( engine::getGlobalVariableManager().setValueFromString( name, *step.findAttribute( "value" ) ) == false )
                runner.recordFailure( step, "could not set " + name + " to '" + *step.findAttribute( "value" ) + "'" );
            return true;
        }

        static bool runExpect( AutomationRunner& runner, const AutomationStep& step )
        {
            runner.runExpect( step );
            return true;
        }

        static bool runExpectLog( AutomationRunner& runner, const AutomationStep& step )
        {
            runner.runExpectLog( step );
            return true;
        }

        static bool runScreenshot( AutomationRunner& runner, const AutomationStep& step )
        {
            const string path      = runner.resolveOutputPath( *step.findAttribute( "file" ) );
            const string directory = FileUtil::getDirectoryPart( path );
            if ( directory.empty() == false )
                (void)FileUtil::ensureDirectoryExists( directory );
            (void)FileUtil::tryRemoveFile( path ); // 지난 실행의 이미지를 읽지 않게 지운다
            runner._listPendingScreenshot.push_back( path );
            ++runner._screenshotRequestedCount;
            return true;
        }

        static bool runExpectImage( AutomationRunner& runner, const AutomationStep& step )
        {
            runner.runExpectImage( step );
            return true;
        }

        /** @brief 종료 시한을 겁니다. @p bClose 면 창 닫기도 요청합니다(`CloseWindow`). */
        static void startExitDeadline( AutomationRunner& runner, const AutomationStep& step, bool bClose )
        {
            string  error;
            float32 seconds = 0.0f;
            // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다.
            (void)AutomationRunnerInternal::readFloat( step, bClose ? "withinSeconds" : "seconds", AutomationRunnerInternal::kDefaultExitWithinSeconds, seconds, error );
            runner._exitDeadlineSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 + static_cast<float64>( seconds );
            if ( bClose == false )
                return;
            IWindow* pWindow = IWindow::getActiveWindow();
            if ( pWindow == nullptr )
                runner.recordFailure( step, "there is no window to close" );
            else
                pWindow->requestClose();
        }

        static bool runCloseWindow( AutomationRunner& runner, const AutomationStep& step )
        {
            startExitDeadline( runner, step, true );
            return true;
        }

        static bool runExpectExitWithin( AutomationRunner& runner, const AutomationStep& step )
        {
            startExitDeadline( runner, step, false );
            return true;
        }

        static bool runPass( AutomationRunner& runner, const AutomationStep& step )
        {
            (void)step;
            runner.finish( AutomationResult::Passed, "<Pass/>" );
            return true;
        }

        static bool runFail( AutomationRunner& runner, const AutomationStep& step )
        {
            const string*     pReason = step.findAttribute( "reason" );
            const string_view reason  = pReason != nullptr ? string_view{ *pReason } : string_view{ "<Fail/>" };
            runner.recordFailure( step, reason );
            runner.finish( AutomationResult::Failed, reason );
            return true;
        }

        static bool runSkip( AutomationRunner& runner, const AutomationStep& step )
        {
            const string* pReason = step.findAttribute( "reason" );
            runner.finish( AutomationResult::Skipped, pReason != nullptr ? string_view{ *pReason } : string_view{ "<Skip/>" } );
            return true;
        }

        // --- 차례(`AutomationStepHoldFunction`) -------------------------------------------------------------------

        /** @brief 조건이 참이 될 때까지(또는 시한까지) 시나리오 시계를 붙잡습니다. 놓으면 단계는 끝난다(실행 함수가 없다). */
        static AutomationStepHold holdWaitUntil( AutomationRunner& runner, const AutomationStep& step, InputManager& input )
        {
            return runner.shouldHoldForCondition( step, input ) ? AutomationStepHold::Hold : AutomationStepHold::Run;
        }

        /** @brief 요청한 스크린샷이 다 써질 때까지 기다립니다. 상한을 넘기면 실패로 적고 측정하지 않고 넘깁니다. */
        static AutomationStepHold holdExpectImage( AutomationRunner& runner, const AutomationStep& step, InputManager& input )
        {
            (void)input;
            const bool bPending = runner._listPendingScreenshot.empty() == false || runner._screenshotCompletedCount < runner._screenshotRequestedCount;
            if ( bPending == false )
                return AutomationStepHold::Run;
            if ( ++runner._imageWaitFrameCount <= AutomationRunnerInternal::kMaxImageWaitFrames )
                return AutomationStepHold::Hold;
            runner.recordFailure( step, "the screenshot never completed (is there a renderer?)" );
            return AutomationStepHold::Skip;
        }
    };
} // namespace sw

namespace sw
{
    // 엔진 단계입니다. 다른 모듈의 단계와 같은 레지스트리에 올라, 같은 이름의 등록은 중복으로 거절됩니다.
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerPress, "Press", nullptr, &AutomationRunnerInternal::validateSlotStep, &AutomationRunnerEngineSteps::prepareSlotStep, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerRelease, "Release", nullptr, &AutomationRunnerInternal::validateSlotStep, &AutomationRunnerEngineSteps::prepareSlotStep, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerTap, "Tap", nullptr, &AutomationRunnerInternal::validateSlotStep, &AutomationRunnerEngineSteps::prepareSlotStep, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerMouseDelta, "MouseDelta", nullptr, &AutomationRunnerInternal::validateMouseDelta, &AutomationRunnerEngineSteps::prepareMouseDelta,
                                   nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerMousePosition, "MousePosition", nullptr, &AutomationRunnerInternal::validateMousePosition,
                                   &AutomationRunnerEngineSteps::prepareMousePosition, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerGamepadAxis, "GamepadAxis", nullptr, &AutomationRunnerInternal::validateGamepadAxis, &AutomationRunnerEngineSteps::prepareGamepadAxis,
                                   nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerText, "Text", nullptr, &AutomationRunnerInternal::validateText, &AutomationRunnerEngineSteps::prepareText, nullptr, false );
    SW_AUTOMATION_STEP( runnerVariable, "Variable", &AutomationRunnerEngineSteps::runVariable, &AutomationRunnerInternal::validateVariable, false );
    SW_AUTOMATION_STEP( runnerExpect, "Expect", &AutomationRunnerEngineSteps::runExpect, &AutomationRunnerInternal::validateExpect, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerWaitUntil, "WaitUntil", nullptr, &AutomationRunnerInternal::validateWaitUntil, &AutomationRunnerEngineSteps::prepareWaitUntil,
                                   &AutomationRunnerEngineSteps::holdWaitUntil, false );
    SW_AUTOMATION_STEP( runnerExpectLog, "ExpectLog", &AutomationRunnerEngineSteps::runExpectLog, &AutomationRunnerInternal::validateExpectLog, false );
    SW_AUTOMATION_STEP( runnerScreenshot, "Screenshot", &AutomationRunnerEngineSteps::runScreenshot, &AutomationRunnerInternal::validateScreenshot, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerExpectImage, "ExpectImage", &AutomationRunnerEngineSteps::runExpectImage, &AutomationRunnerInternal::validateExpectImage, nullptr,
                                   &AutomationRunnerEngineSteps::holdExpectImage, false );
    SW_AUTOMATION_STEP( runnerCloseWindow, "CloseWindow", &AutomationRunnerEngineSteps::runCloseWindow, &AutomationRunnerInternal::validateCloseWindow, false );
    SW_AUTOMATION_STEP( runnerExpectExitWithin, "ExpectExitWithin", &AutomationRunnerEngineSteps::runExpectExitWithin, &AutomationRunnerInternal::validateExpectExitWithin,
                        false );
    SW_AUTOMATION_STEP( runnerPass, "Pass", &AutomationRunnerEngineSteps::runPass, &AutomationRunnerInternal::validatePass, false );
    SW_AUTOMATION_STEP( runnerFail, "Fail", &AutomationRunnerEngineSteps::runFail, &AutomationRunnerInternal::validateFailOrSkip, false );
    SW_AUTOMATION_STEP( runnerSkip, "Skip", &AutomationRunnerEngineSteps::runSkip, &AutomationRunnerInternal::validateFailOrSkip, false );
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
        , _holdFrameCount{ 0 }
        , _result{ AutomationResult::Running }
        , _bStarted{ SW_FALSE }
        , _bEnded{ SW_FALSE }
        , _bListeningLog{ SW_FALSE }
        , _reserved{ 0 }
    {
        AutomationEnvironmentSteps::ensureLinked();
        AutomationWindowSteps::ensureLinked();
    }

    AutomationRunner::~AutomationRunner()
    {
        if ( _bListeningLog == SW_TRUE )
            Logger::unregisterGlobalListener( _logListenerHandle );
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
        if ( skipIfStepsAreMissingFromThisBuild() )
            return;
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
        return skipIfStepsAreMissingFromThisBuild() == false;
    }

    bool AutomationRunner::skipIfStepsAreMissingFromThisBuild()
    {
        const AutomationStep* pMissingStep = AutomationRunnerInternal::findStepMissingFromThisBuild( _scenario );
        if ( pMissingStep == nullptr )
            return false;
        finish( AutomationResult::Skipped, pMissingStep->describe() + ": dev commands are compiled out of this build" );
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
            // 씬 플레이 중이란 활성 씬이 플레이를 시작했고 시작 차단 조건이 모두 풀렸다는 뜻이다(UI 의 로딩 화면은 게임 입력을 막으므로, 그동안 넣은 입력은 폰에 닿지 않는다).
            const GameObjectManager* pManager = findActiveObjectManager();
            const bool               bBlocked = AutomationStepRegistry::isStartBlocked();
            const bool               bReady   = _scenario.getStartCondition() == AutomationStartCondition::Immediately || ( pManager != nullptr && pManager->hasBegunPlay() && bBlocked == false );
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
            // 배타 시나리오는 무인 실행이다. 사람이 같은 PC 에서 마우스를 움직여도 에디터 UI 까지 결과가 바뀌지 않게 장치 이벤트를 창 메시지 단계에서 버린다.
            input.setOsDeviceInputBlocked( _scenario.getInputMode() == VirtualInputMode::Exclusive );
            SW_LOG_INFO( "[Scenario] %# started after %# frame(s)", _scenario.getName().c_str(), _waitFrameCount );
        }
        _logFrameIndex.store( _frameIndex, std::memory_order_relaxed );
        // 액션 계층 단계(레지스트리의 `_bBeforeInput`)는 그 프레임의 입력 재생과 게임 틱 전에 넣는다.
        const vector<AutomationStep>& listStep = _scenario.getSteps();
        for ( size_t stepIndex = _nextStepIndex; stepIndex < listStep.size() && listStep[stepIndex]._frameIndex == _frameIndex; ++stepIndex )
        {
            const AutomationStepRegistration* pRegistration    = AutomationStepRegistry::find( listStep[stepIndex]._kind );
            const bool                        bRunsBeforeInput = pRegistration != nullptr && pRegistration->_bBeforeInput && pRegistration->_pRun != nullptr;
            if ( bRunsBeforeInput && pRegistration->_pRun( *this, listStep[stepIndex] ) == false )
                finish( AutomationResult::LoadError, listStep[stepIndex].describe() + ": the step could not run" );
        }
    }

    AutomationResult AutomationRunner::onFrameEnd( InputManager& input )
    {
        if ( _result == AutomationResult::Running && _bStarted == SW_TRUE )
        {
            const vector<AutomationStep>& listStep = _scenario.getSteps();
            // `<=` 이므로 스크린샷을 기다리느라 밀린 단계는 다음 프레임에 이어서 돈다.
            while ( _nextStepIndex < listStep.size() && listStep[_nextStepIndex]._frameIndex <= _frameIndex && _result == AutomationResult::Running )
            {
                const AutomationStep&             step          = listStep[_nextStepIndex];
                const AutomationStepRegistration* pRegistration = AutomationStepRegistry::find( step._kind );
                const AutomationStepHold          hold          = pRegistration != nullptr && pRegistration->_pHold != nullptr ? pRegistration->_pHold( *this, step, input )
                                                                                                                               : AutomationStepHold::Run;
                if ( hold == AutomationStepHold::Hold )
                    break;
                _imageWaitFrameCount = 0;
                ++_nextStepIndex;
                if ( hold == AutomationStepHold::Skip )
                    continue;
                // 등록이 없으면 검사 뒤에 그 단계를 등록한 모듈이 내려갔다(핫 리로드). 실행 함수가 없는 단계(입력)는 준비에서 이미 끝났다.
                const bool bRunsNow = pRegistration != nullptr && pRegistration->_bBeforeInput == false && pRegistration->_pRun != nullptr;
                if ( pRegistration == nullptr || ( bRunsNow && pRegistration->_pRun( *this, step ) == false ) )
                    finish( AutomationResult::LoadError, step.describe() + ": the step could not run" );
            }
            if ( _result == AutomationResult::Running && _exitDeadlineSeconds > 0.0 &&
                 static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 > _exitDeadlineSeconds )
                finish( AutomationResult::Failed, "the window was asked to close but the loop kept running past ExpectExitWithin" );
            // 끝난 프레임은 세지 않는다. 보고의 프레임은 끝낸 단계의 프레임이다. `WaitUntil` 이 붙잡은 프레임도 세지 않는다(뒤 단계가 그만큼 밀린다).
            if ( _result == AutomationResult::Running && _holdFrameCount == 0 )
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
            if ( pRegistration == nullptr )
            {
                outError = step.describe() + ": unknown step (nothing registered under that name)";
                return false;
            }
            if ( pRegistration->_pValidate != nullptr && pRegistration->_pValidate( step, outError ) == false )
                return false;
            if ( pRegistration->_pPrepare != nullptr && pRegistration->_pPrepare( *this, step, outError ) == false )
                return false;
        }
        _logListenerHandle = Logger::registerGlobalListener( SW_DELEGATE_METHOD( LogWrittenDelegate, &AutomationRunner::onLogWritten, this ) );
        _bListeningLog     = _logListenerHandle.isValid() ? SW_TRUE : SW_FALSE;
        return true;
    }

    void AutomationRunner::runExpect( const AutomationStep& step )
    {
        bool   bMatched = false;
        string message;
        if ( compareProbe( step, bMatched, message ) == false )
            recordFailure( step, message );
        else if ( bMatched == false )
            recordFailure( step, "Expect " + message );
    }

    bool AutomationRunner::shouldHoldForCondition( const AutomationStep& step, InputManager& input )
    {
        bool       bMatched = false;
        string     message;
        const bool bHasValue = compareProbe( step, bMatched, message );
        if ( bHasValue && bMatched )
        {
            if ( _holdFrameCount > 0 )
                SW_LOG_INFO( "[Scenario] %# held the clock for %# frame(s)", step.describe().c_str(), _holdFrameCount );
            _holdFrameCount = 0;
            return false;
        }
        string error;
        uint32 timeoutFrames = 0;
        (void)AutomationRunnerInternal::readUint( step, "timeoutFrames", AutomationRunnerInternal::kDefaultWaitTimeoutFrames, timeoutFrames,
                                                  error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
        if ( _holdFrameCount >= timeoutFrames )
        {
            recordFailure( step, "WaitUntil gave up after " + to_string( _holdFrameCount ) + " frame(s): " + message );
            _holdFrameCount = 0;
            return false;
        }
        ++_holdFrameCount;
        // 이 프레임은 시나리오 프레임으로 세지 않는다. 다음 프레임에 낼 입력 이벤트부터 한 프레임씩 밀어 시나리오 프레임과 맞춘다.
        _inputScript.delayEventsFrom( input.getVirtualFrameIndex(), 1 );
        return true;
    }

    bool AutomationRunner::compareProbe( const AutomationStep& step, bool& outMatched, string& outMessage ) const
    {
        const string&                      probeName     = *step.findAttribute( "probe" );
        const AutomationProbeRegistration* pRegistration = AutomationProbes::find( probeName );
        float64                            value         = 0.0;
        outMatched                                       = false;
        // 모듈을 언로드했다 로드하면 탐침이 빠질 수 있다. 시작 때 있었어도 다시 찾는다.
        if ( pRegistration == nullptr || pRegistration->_pFunction( findActiveObjectManager(), value ) == false )
        {
            outMessage = pRegistration == nullptr ? probeName + " is no longer registered" : probeName + " has no value (no target in the scene)";
            return false;
        }
        string  error;
        float32 expected  = 0.0f;
        float32 tolerance = 0.0f;
        bool    bOk       = true;
        string  rule;
        if ( step.findAttribute( "equals" ) != nullptr )
        {
            (void)AutomationRunnerInternal::readFloat( step, "equals", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value == static_cast<float64>( expected );
            rule = "== " + *step.findAttribute( "equals" );
        }
        else if ( step.findAttribute( "near" ) != nullptr )
        {
            (void)AutomationRunnerInternal::readFloat( step, "near", 0.0f, expected, error );          // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)AutomationRunnerInternal::readFloat( step, "tolerance", 1.0e-4f, tolerance, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value >= static_cast<float64>( expected ) - static_cast<float64>( tolerance ) && value <= static_cast<float64>( expected ) + static_cast<float64>( tolerance );
            rule = "near " + *step.findAttribute( "near" );
        }
        else if ( step.findAttribute( "atLeast" ) != nullptr )
        {
            (void)AutomationRunnerInternal::readFloat( step, "atLeast", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value >= static_cast<float64>( expected );
            rule = ">= " + *step.findAttribute( "atLeast" );
        }
        else
        {
            (void)AutomationRunnerInternal::readFloat( step, "atMost", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value <= static_cast<float64>( expected );
            rule = "<= " + *step.findAttribute( "atMost" );
        }
        outMatched = bOk;
        outMessage = probeName + " " + rule + ", got " + AutomationRunnerInternal::formatNumber( value );
        return true;
    }

    void AutomationRunner::runExpectLog( const AutomationStep& step )
    {
        const string& needle = *step.findAttribute( "contains" );
        string        error;
        uint32        since = 0;
        (void)AutomationRunnerInternal::readUint( step, "since", 0, since, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
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
            (void)AutomationRunnerInternal::readUint( step, "count", 0, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            if ( matchCount != expected )
                recordFailure( step, "ExpectLog '" + needle + "' count == " + to_string( expected ) + ", got " + to_string( matchCount ) );
            return;
        }
        (void)AutomationRunnerInternal::readUint( step, "atLeast", 0, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
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
        (void)AutomationImageMetric::tryParseKind( *step.findAttribute( "metric" ), metric ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
        const string* pRegion = step.findAttribute( "region" );
        if ( pRegion != nullptr )
            (void)AutomationImageMetric::tryParseRegion( *pRegion, region );                   // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
        (void)Internal::readFloat( step, "ratio", Internal::kDefaultDarkRatio, ratio, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다

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
        // 값은 항상 적는다. 임계값을 정할 때 숫자를 보기 위해서다.
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
            (void)Internal::readFloat( step, "equals", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value == static_cast<float64>( expected );
            rule = "== " + *step.findAttribute( "equals" );
        }
        else if ( step.findAttribute( "near" ) != nullptr )
        {
            (void)Internal::readFloat( step, "near", 0.0f, expected, error );          // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            (void)Internal::readFloat( step, "tolerance", 1.0e-3f, tolerance, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = MathUtil::abs( value - static_cast<float64>( expected ) ) <= static_cast<float64>( tolerance );
            rule = "near " + *step.findAttribute( "near" );
        }
        else if ( step.findAttribute( "atLeast" ) != nullptr )
        {
            (void)Internal::readFloat( step, "atLeast", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
            bOk  = value >= static_cast<float64>( expected );
            rule = ">= " + *step.findAttribute( "atLeast" );
        }
        else
        {
            (void)Internal::readFloat( step, "atMost", 0.0f, expected, error ); // 검사 함수가 이미 읽어 봤으므로 실패할 수 없다
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
        // 실행기 자신의 줄([Scenario])은 세지 않는다. 실패 줄이 단언 문구를 반복해 ExpectLog 를 맞추지 않게 하기 위해서다.
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
            Logger::unregisterGlobalListener( _logListenerHandle );
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
        string json    = "{\n  \"name\": \"" + Internal::escapeJSON( _scenario.getName() ) + "\",\n";
        json += "  \"result\": \"" + string( getResultName( _result ) ) + "\",\n";
        json += "  \"exitCode\": " + to_string( static_cast<int32>( _result ) ) + ",\n";
        json += "  \"frames\": " + to_string( _frameIndex ) + ",\n";
        json += "  \"reason\": \"" + Internal::escapeJSON( _finishReason ) + "\",\n";
        json += "  \"failures\": [";
        for ( size_t index = 0; index < _listFailure.size(); ++index )
        {
            json += index == 0 ? "\n    \"" : ",\n    \"";
            json += Internal::escapeJSON( _listFailure[index] ) + "\"";
        }
        json += _listFailure.empty() ? "],\n" : "\n  ],\n";
        json += "  \"metrics\": [";
        for ( size_t index = 0; index < _listMetricLine.size(); ++index )
        {
            json += index == 0 ? "\n    \"" : ",\n    \"";
            json += Internal::escapeJSON( _listMetricLine[index] ) + "\"";
        }
        json += _listMetricLine.empty() ? "]\n}\n" : "\n  ]\n}\n";
        const string directory = FileUtil::getDirectoryPart( _reportPath );
        if ( directory.empty() == false )
            (void)FileUtil::ensureDirectoryExists( directory );
        if ( FileUtil::writeTextFile( _reportPath, json ) == false )
            SW_LOG_WARNING( "[Scenario] could not write the report %#", _reportPath.c_str() );
    }
} // namespace sw
