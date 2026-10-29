#include "pch.h"

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
#include "Engine/Automation/Runner/AutomationRunner.h"
#include "Engine/Automation/Runner/AutomationStepArgs.h"
#include "Engine/Automation/Scenario/AutomationEnvironmentSteps.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Scenario/AutomationStepValidation.h"
#include "Engine/Automation/Scenario/AutomationWindowSteps.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
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

    namespace
    {
        struct AutomationRunnerStepsInternal
        {
            // 시한 상수(기다리는 프레임 · 초)는 `AutomationTimeLimits` 한 곳에 있다.
            /**
             * @brief 에셋 일이 0 이 된 뒤 스크린샷 전에 더 기다리는 프레임 수입니다. 올린 텍스처의 새 SRV 는 다음 씬 빌드가 머티리얼에 넣고(`Material::refreshTextureBindings`),
             *        그 스냅샷이 렌더 큐(깊이 2)를 지나야 화면에 나온다.
             */
            static constexpr uint32 kAssetSettleTrailFrames = 3;

            // --- 단계 인자: 검사 함수와 준비 · 실행 함수가 같은 읽기(`parse*`) 하나를 쓴다 -----------------------------------------
            //     검사가 읽어 본 뒤 실행이 결과를 버리며 다시 읽던 것을 없앤다. 실행 때 읽기가 실패하면(핫 리로드로 바뀐 단계) 버리지 않고 오류로 끝낸다.
            //     기본값은 구조체의 초기값 하나다.

            /** @brief `Press` · `Release` · `Tap` 의 인자입니다. */
            struct SlotStepArgs
            {
                InputSlot _slot{};
                uint32    _holdFrameCount{ 1 }; ///< `Tap` 이 누르고 있는 프레임 수
            };

            /** @brief `Wheel` · `MouseDelta` · `MousePosition` 의 두 축 인자입니다. 없는 축은 0 입니다. */
            struct AxisPairArgs
            {
                float32 _x{ 0.0f };
                float32 _y{ 0.0f };
            };

            /** @brief `GamepadAxis` 의 인자입니다. */
            struct GamepadAxisArgs
            {
                uint32  _axis{ 0 };
                uint32  _pad{ 0 };
                float32 _value{ 0.0f };
            };

            [[nodiscard]] static bool parseSlotStep( const AutomationStep& step, SlotStepArgs& outArgs, string& outError )
            {
                if ( AutomationStepArgsUtil::readUint( step, "hold", outArgs._holdFrameCount, outError ) == false )
                    return false;
                const string* pSlot = step.requireAttribute( "slot", outError );
                if ( pSlot == nullptr )
                    return false;
                if ( InputSlotUtil::tryParse( *pSlot, outArgs._slot ) == false )
                {
                    outError = step.describe() + ": unknown slot '" + *pSlot + "' (Key.E · Mouse.Left · Gamepad.A · Gamepad1.A)";
                    return false;
                }
                return true;
            }

            [[nodiscard]] static bool parseAxisPair( const AutomationStep& step, AxisPairArgs& outArgs, string& outError )
            {
                return AutomationStepArgsUtil::readFloat( step, "x", outArgs._x, outError ) && AutomationStepArgsUtil::readFloat( step, "y", outArgs._y, outError );
            }

            [[nodiscard]] static bool parseGamepadAxis( const AutomationStep& step, GamepadAxisArgs& outArgs, string& outError )
            {
                return step.requireAttribute( "axis", outError ) != nullptr && step.requireAttribute( "value", outError ) != nullptr &&
                       AutomationStepArgsUtil::readUint( step, "axis", outArgs._axis, outError ) && AutomationStepArgsUtil::readFloat( step, "value", outArgs._value, outError ) && AutomationStepArgsUtil::readUint( step, "pad", outArgs._pad, outError );
            }

            /** @brief 종료 시한 단계의 초입니다. 없으면 `AutomationTimeLimits::kDefaultExitWithinSeconds` 입니다. */
            [[nodiscard]] static bool parseExitSeconds( const AutomationStep& step, const utf8* pName, float32& outSeconds, string& outError )
            {
                outSeconds = AutomationTimeLimits::kDefaultExitWithinSeconds;
                return AutomationStepArgsUtil::readFloat( step, pName, outSeconds, outError );
            }

            // --- 엔진 단계의 검사 함수(`AutomationStepValidateFunction`). 속성의 허용 값과 오류 메시지가 여기 있다 -------------

            /** @brief `Press` · `Release` · `Tap` 입니다. `hold` 는 `Tap` 에만 씁니다. */
            static bool validateSlotStep( const AutomationStep& step, string& outError )
            {
                if ( step.hasOnlyAttributes( { "slot", "hold" }, outError ) == false )
                    return false;
                if ( string_view{ step._kind } != "Tap" && step.findAttribute( "hold" ) != nullptr )
                {
                    outError = step.describe() + ": hold is only for <Tap>";
                    return false;
                }
                SlotStepArgs args;
                return parseSlotStep( step, args, outError );
            }

            /** @brief `MouseDelta` · `Wheel`(`y` 는 세로 휠, `x` 는 가로 휠 눈금)입니다. 없는 축은 0 입니다. */
            static bool validateAxisPair( const AutomationStep& step, string& outError )
            {
                AxisPairArgs args;
                return step.hasOnlyAttributes( { "x", "y" }, outError ) && parseAxisPair( step, args, outError );
            }

            static bool validateMousePosition( const AutomationStep& step, string& outError )
            {
                const bool bInvalidMouseAttributes = step.hasOnlyAttributes( { "x", "y" }, outError ) == false || step.requireAttribute( "x", outError ) == nullptr ||
                                                     step.requireAttribute( "y", outError ) == nullptr;
                if ( bInvalidMouseAttributes )
                    return false;
                AxisPairArgs args;
                if ( parseAxisPair( step, args, outError ) == false )
                    return false;
                const bool bMouseFractionOutOfRange = args._x < 0.0f || args._x > 1.0f || args._y < 0.0f || args._y > 1.0f;
                if ( bMouseFractionOutOfRange )
                {
                    outError = step.describe() + ": x and y are fractions of the client area (0..1)";
                    return false;
                }
                return true;
            }

            static bool validateGamepadAxis( const AutomationStep& step, string& outError )
            {
                GamepadAxisArgs args;
                return step.hasOnlyAttributes( { "axis", "value", "pad" }, outError ) && parseGamepadAxis( step, args, outError );
            }

            static bool validateText( const AutomationStep& step, string& outError )
            {
                return step.hasOnlyAttributes( { "value" }, outError ) && step.requireAttribute( "value", outError ) != nullptr;
            }

            /** @brief `name` · `value` 가 있고 `name` 이 등록된 전역 변수인지 봅니다. 아니면 nullptr 과 이유입니다(`Variable` · `ExpectVariable` 이 함께 씁니다). */
            static GlobalVariableInfo* findRequiredVariable( const AutomationStep& step, string& outError )
            {
                const string* pName = step.requireAttribute( "name", outError );
                if ( pName == nullptr || step.requireAttribute( "value", outError ) == nullptr )
                    return nullptr;
                GlobalVariableInfo* pInfo = engine::areEngineServicesBound() ? engine::getSubsystem<GlobalVariableManager>().findVariable( *pName ) : nullptr;
                if ( pInfo == nullptr )
                    outError = step.describe() + ": unknown global variable '" + *pName + "'";
                return pInfo;
            }

            static bool validateVariable( const AutomationStep& step, string& outError )
            {
                return step.hasOnlyAttributes( { "name", "value" }, outError ) && findRequiredVariable( step, outError ) != nullptr;
            }

            /**
             * @brief `ExpectVariable` 의 `value` · `tolerance` 를 @p info 의 타입으로 읽어 지금 값과 비교합니다(검사와 실행이 함께 씁니다).
             * @return `value` 가 그 타입으로 읽히지 않거나 `tolerance` 가 음수이거나 실수 변수가 아닌데 붙었으면 false 와 이유입니다.
             */
            [[nodiscard]] static bool evaluateExpectVariable( const AutomationStep& step, const GlobalVariableInfo& info, bool& outMatched, string& outError )
            {
                float32 tolerance = 0.0f; // 적지 않으면 정확히 같아야 한다
                if ( AutomationStepArgsUtil::readFloat( step, "tolerance", tolerance, outError ) == false )
                    return false;
                if ( step.findAttribute( "tolerance" ) != nullptr && ( info._type != GlobalVariableType::Float || tolerance < 0.0f ) )
                {
                    outError = step.describe() + ": tolerance is a non-negative number for float variables only";
                    return false;
                }
                const string& value = *step.findAttribute( "value" );
                if ( AutomationStepArgsUtil::compareVariable( info, string_view{ value }, tolerance, outMatched ) == false )
                {
                    outError = step.describe() + ": value '" + value + "' is not a " + info.getTypeName() + " (enum variables take the number)";
                    return false;
                }
                return true;
            }

            /** @brief `ExpectVariable` 입니다. 변수가 등록돼 있고, `value` 가 그 타입으로 읽히며, `tolerance`(0 이상)는 실수 변수에만 붙습니다. */
            static bool validateExpectVariable( const AutomationStep& step, string& outError )
            {
                if ( step.hasOnlyAttributes( { "name", "value", "tolerance" }, outError ) == false )
                    return false;
                const GlobalVariableInfo* pInfo    = findRequiredVariable( step, outError );
                bool                      bMatched = false;
                return pInfo != nullptr && evaluateExpectVariable( step, *pInfo, bMatched, outError );
            }

            /** @brief 감시 값 이름과 그 감시 값이 꼭 받아야 하는 `target` · `key` 를 봅니다. */
            static bool validateWatchReference( const AutomationStep& step, string& outError )
            {
                const string* pWatch = step.requireAttribute( "watch", outError );
                if ( pWatch == nullptr )
                    return false;
                const AutomationWatchRegistration* pRegistration = AutomationWatchRegistry::find( *pWatch );
                if ( pRegistration == nullptr )
                {
                    outError = step.describe() + ": unknown watch '" + *pWatch + "' (is the game that registers it running?)";
                    return false;
                }
                const bool bQueryOnlyAttribute = pRegistration->_pQueryFunction == nullptr && ( step.findAttribute( "target" ) != nullptr || step.findAttribute( "key" ) != nullptr );
                if ( bQueryOnlyAttribute )
                {
                    outError = step.describe() + ": watch '" + *pWatch + "' takes no target or key";
                    return false;
                }
                if ( pRegistration->_bNeedsTarget && step.requireAttribute( "target", outError ) == nullptr )
                    return false;
                return pRegistration->_bNeedsKey == false || step.requireAttribute( "key", outError ) != nullptr;
            }

            /** @brief 감시 값 단계(`Expect`, `WaitUntil`)의 공통 부분입니다. 감시 값이 있고 비교가 정확히 하나이며 수 비교는 수로 읽혀야 합니다(`is` 는 글 비교). */
            static bool validateWatchComparison( const AutomationStep& step, string& outError )
            {
                if ( validateWatchReference( step, outError ) == false )
                    return false;
                if ( countComparisons( step, { "equals", "near", "atLeast", "atMost", "is", "sameAs", "differsFrom" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of equals · near · atLeast · atMost · is · sameAs · differsFrom";
                    return false;
                }
                const bool bTextRule = step.findAttribute( "is" ) != nullptr || step.findAttribute( "sameAs" ) != nullptr || step.findAttribute( "differsFrom" ) != nullptr;
                if ( bTextRule )
                    return true;
                AutomationStepArgsUtil::NumberComparison comparison;
                return AutomationStepArgsUtil::parseNumberComparison( step, AutomationStepArgsUtil::kWatchNearTolerance, comparison, outError );
            }

            static bool validateExpect( const AutomationStep& step, string& outError )
            {
                return step.hasOnlyAttributes( { "watch", "target", "key", "equals", "near", "tolerance", "atLeast", "atMost", "is", "sameAs", "differsFrom" }, outError ) &&
                       validateWatchComparison( step, outError );
            }

            /** @brief `WaitUntil` 입니다. 그 프레임에 혼자 있는지는 준비 함수(`AutomationRunnerEngineSteps::prepareWaitUntil`)가 시나리오 전체를 보고 확인합니다. */
            static bool validateWaitUntil( const AutomationStep& step, string& outError )
            {
                if ( step.hasOnlyAttributes( { "watch", "target", "key", "equals", "near", "tolerance", "atLeast", "atMost", "is", "sameAs", "differsFrom", "timeoutFrames",
                                               "timeoutSeconds" },
                                             outError ) == false )
                    return false;
                if ( step.findAttribute( "timeoutFrames" ) != nullptr && step.findAttribute( "timeoutSeconds" ) != nullptr )
                {
                    outError = step.describe() + ": give timeoutFrames or timeoutSeconds, not both";
                    return false;
                }
                AutomationStepArgsUtil::WaitTimeoutArgs timeout;
                return AutomationStepArgsUtil::parseWaitTimeout( step, timeout, outError ) && validateWatchComparison( step, outError );
            }

            /** @brief `ExpectVisited` 입니다. `is`(그 값을 한 번이라도 거쳤다) 또는 `sequence`(쉼표로 적은 값을 이 순서로 거쳤다) 하나와 `since`(프레임) 입니다. */
            static bool validateExpectVisited( const AutomationStep& step, string& outError )
            {
                uint32 since = 0; // 적지 않으면 처음부터 본다
                if ( step.hasOnlyAttributes( { "watch", "target", "key", "is", "sequence", "since" }, outError ) == false || validateWatchReference( step, outError ) == false )
                    return false;
                if ( countComparisons( step, { "is", "sequence" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of is · sequence";
                    return false;
                }
                return AutomationStepArgsUtil::readUint( step, "since", since, outError );
            }

            /** @brief `Record` 입니다. 감시 값 값을 보고의 `records` 에 남깁니다. */
            static bool validateRecord( const AutomationStep& step, string& outError )
            {
                return step.hasOnlyAttributes( { "watch", "target", "key" }, outError ) && validateWatchReference( step, outError );
            }

            /** @brief `Remember` 입니다. 감시 값 값을 `as` 이름으로 기억합니다(뒤 단계의 `sameAs` · `differsFrom` 이 비교한다). */
            static bool validateRemember( const AutomationStep& step, string& outError )
            {
                return step.hasOnlyAttributes( { "watch", "target", "key", "as" }, outError ) && step.requireAttribute( "as", outError ) != nullptr &&
                       validateWatchReference( step, outError );
            }

            static bool validateExpectLog( const AutomationStep& step, string& outError )
            {
                if ( step.hasOnlyAttributes( { "id", "contains", "count", "atLeast", "since" }, outError ) == false )
                    return false;
                // 로그 id 가 있는 줄은 id 로 센다(문구가 바뀌어도 단언이 따라온다). contains 는 id 없는 줄에만 쓴다 — 둘 중 정확히 하나.
                if ( ( step.findAttribute( "id" ) != nullptr ) == ( step.findAttribute( "contains" ) != nullptr ) )
                {
                    outError = step.describe() + ": needs exactly one of id · contains (id for lines written with SW_LOG_*_ID, contains only for lines without a log id)";
                    return false;
                }
                if ( countComparisons( step, { "count", "atLeast" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of count · atLeast";
                    return false;
                }
                AutomationStepArgsUtil::ExpectLogArgs args;
                return AutomationStepArgsUtil::parseExpectLog( step, args, outError );
            }

            static bool validateScreenshot( const AutomationStep& step, string& outError )
            {
                if ( step.hasOnlyAttributes( { "file", "waitForSettle" }, outError ) == false || step.requireAttribute( "file", outError ) == nullptr )
                    return false;
                bool bWait = true;
                if ( readWaitForSettle( step, bWait ) == false )
                {
                    outError = step.describe() + ": waitForSettle must be a boolean (true or false), got '" + *step.findAttribute( "waitForSettle" ) + "'";
                    return false;
                }
                return true;
            }

            /** @brief `waitForSettle` 속성을 읽습니다. 적지 않았으면 true(자리 잡기를 기다린다)이고, 불리언으로 읽지 못하면 false 입니다. 검증과 실행이 같은 철자 표(`StringUtil::tryParseBool`)를 씁니다. */
            [[nodiscard]] static bool readWaitForSettle( const AutomationStep& step, bool& outWait )
            {
                outWait             = true;
                const string* pWait = step.findAttribute( "waitForSettle" );
                return pWait == nullptr || StringUtil::tryParseBool( *pWait, outWait );
            }

            /**
             * @brief `<Screenshot waitForSettle="false">` 면 true 입니다 — 그 프레임을 바로 찍는다.
             * @details 찍는 프레임이 정해져야 하는 시나리오(디더 등장의 중간 단계, 프레임마다 움직이는 카메라)가 쓴다. 자리 잡기를 기다리면 등장이 끝나거나 카메라가 멈춘 뒤를 찍는다.
             *          그런 시나리오는 앞에서 `<WaitUntil watch="Assets.Pending" equals="0">` 로 로드를 끝내 둔다.
             */
            [[nodiscard]] static bool capturesWithoutSettling( const AutomationStep& step )
            {
                bool       bWait = true;
                const bool bRead = readWaitForSettle( step, bWait ); // 읽지 못한 값은 validateScreenshot 이 이미 거절했다
                return bRead && bWait == false;
            }

            static bool validateExpectImage( const AutomationStep& step, string& outError )
            {
                if ( step.hasRequiredAttributes( { "file", "golden", "region", "tolerance" }, { "file", "golden" }, outError ) == false )
                    return false;
                AutomationStepArgsUtil::ExpectImageArgs args;
                return AutomationStepArgsUtil::parseExpectImage( step, args, outError );
            }

            /** @brief 종료 시한 단계의 시간 속성 @p pName 하나만 받습니다. */
            static bool validateExitSeconds( const AutomationStep& step, const utf8* pName, string& outError )
            {
                float32 seconds = 0.0f;
                return step.hasOnlyAttributes( { pName }, outError ) && parseExitSeconds( step, pName, seconds, outError );
            }

            static bool validateCloseWindow( const AutomationStep& step, string& outError ) { return validateExitSeconds( step, "withinSeconds", outError ); }

            static bool validateExpectExitWithin( const AutomationStep& step, string& outError ) { return validateExitSeconds( step, "seconds", outError ); }

            static bool validatePass( const AutomationStep& step, string& outError ) { return step.hasOnlyAttributes( {}, outError ); }

            static bool validateFailOrSkip( const AutomationStep& step, string& outError ) { return step.hasOnlyAttributes( { "reason" }, outError ); }

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
        };
    } // namespace
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
        /** @brief 엔진 단계는 실행기 자신의 상태(가상 입력 원천 · 스크린샷 대기열 · 종료 시한)를 만진다. 그래서 문맥을 실행기로 되돌려 쓴다 — 이 단계들은 실행기에만 등록되어 돈다. */
        static AutomationRunner& asRunner( AutomationStepContext& context ) { return static_cast<AutomationRunner&>( context ); }

        // --- 준비(`AutomationStepPrepareFunction`): 입력 단계는 가상 입력 원천으로 옮긴다. 인자는 검사와 같은 `parse*` 로 읽고, 실패하면 버리지 않고 오류다 ---

        [[nodiscard]] static bool prepareSlotStep( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner&                           runner = asRunner( context );
            AutomationRunnerStepsInternal::SlotStepArgs args;
            if ( AutomationRunnerStepsInternal::parseSlotStep( step, args, outError ) == false )
                return false;
            const string_view kind = step._kind;
            // 가상 입력 세션의 패드는 끊긴 채 시작한다 — 패드 단추를 누르는 단계는 그 패드를 먼저 연결한다(안 하면 단추가 입력 맵에 닿지 않는다).
            if ( args._slot._deviceKind == InputDeviceKind::Gamepad )
                runner._inputScript.addGamepadConnection( step._frameIndex, args._slot._deviceIndex );
            const bool bAdded = kind == "Tap" ? runner._inputScript.addTap( step._frameIndex, args._slot, args._holdFrameCount )
                                              : runner._inputScript.addSlot( step._frameIndex, args._slot, kind == "Press" );
            if ( bAdded == false )
                outError = step.describe() + ": that slot cannot be pressed (an axis or an unknown device)";
            return bAdded;
        }

        [[nodiscard]] static bool prepareWheel( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner&                           runner = asRunner( context );
            AutomationRunnerStepsInternal::AxisPairArgs args;
            if ( AutomationRunnerStepsInternal::parseAxisPair( step, args, outError ) == false )
                return false;
            if ( args._y != 0.0f )
                runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeMouseWheel( args._y ) );
            if ( args._x != 0.0f )
                runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeMouseHorizontalWheel( args._x ) );
            return true;
        }

        [[nodiscard]] static bool prepareMouseDelta( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner&                           runner = asRunner( context );
            AutomationRunnerStepsInternal::AxisPairArgs args;
            if ( AutomationRunnerStepsInternal::parseAxisPair( step, args, outError ) == false )
                return false;
            runner._inputScript.addMouseDelta( step._frameIndex, args._x, args._y );
            return true;
        }

        [[nodiscard]] static bool prepareMousePosition( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner& runner = asRunner( context );
            // 창 클라이언트 영역의 비율을 픽셀로 바꾼다(시작할 때의 창 크기). 창이 없으면(헤드리스) 그 위치를 낼 수 없다.
            const MainWindowSubsystem* pWindowHost         = engine::findSubsystem<MainWindowSubsystem>();
            const IWindow*             pWindow             = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
            const bool                 bNoWindowForPointer = pWindow == nullptr || pWindow->getWidth() == 0 || pWindow->getHeight() == 0;
            if ( bNoWindowForPointer )
            {
                outError = step.describe() + ": no window to place the pointer in";
                return false;
            }
            AutomationRunnerStepsInternal::AxisPairArgs args;
            if ( AutomationRunnerStepsInternal::parseAxisPair( step, args, outError ) == false )
                return false;
            const float32 maxX = static_cast<float32>( pWindow->getWidth() - 1 );
            const float32 maxY = static_cast<float32>( pWindow->getHeight() - 1 );
            runner._inputScript.addMousePosition( step._frameIndex, static_cast<int32>( MathUtil::round( args._x * maxX ) ), static_cast<int32>( MathUtil::round( args._y * maxY ) ) );
            return true;
        }

        [[nodiscard]] static bool prepareGamepadAxis( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner&                              runner = asRunner( context );
            AutomationRunnerStepsInternal::GamepadAxisArgs args;
            if ( AutomationRunnerStepsInternal::parseGamepadAxis( step, args, outError ) == false )
                return false;
            // 가상 입력 세션의 패드는 끊긴 채 시작하고 끊긴 패드의 스틱은 입력 맵 · 가상 커서가 읽지 않는다 — 축을 내는 단계는 그 패드를 먼저 연결한다.
            runner._inputScript.addGamepadConnection( step._frameIndex, static_cast<uint8>( args._pad ) );
            runner._inputScript.addGamepadAxis( step._frameIndex, static_cast<uint16>( args._axis ), args._value, static_cast<uint8>( args._pad ) );
            return true;
        }

        [[nodiscard]] static bool prepareText( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner& runner = asRunner( context );
            const string*     pValue = step.requireAttribute( "value", outError );
            if ( pValue == nullptr )
                return false;
            runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeTextInput( *pValue ) );
            return true;
        }

        /** @brief 붙잡는 동안 같은 프레임의 다른 단계가 여러 번 돌거나(입력 전 단계) 조건보다 먼저 돌지 않게, 기다림은 그 프레임에 혼자 둔다(순차 형식은 읽기가 자리를 나눈다 — 이것은 그 약속의 그물이다). */
        [[nodiscard]] static bool prepareWaitUntil( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner&     runner     = asRunner( context );
            const AutomationStep* pOtherStep = runner._scenario.findOtherStepAtSameFrame( step );
            if ( pOtherStep != nullptr )
            {
                outError = step.describe() + ": <WaitUntil> must be the only step at its frame (move " + pOtherStep->describe() + ")";
                return false;
            }
            return true;
        }

        // --- 실행(`AutomationStepFunction`): 씬 틱 뒤에 돈다 -------------------------------------------------------

        static bool runVariable( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            const string&     name   = *step.findAttribute( "name" );
            if ( engine::getSubsystem<GlobalVariableManager>().setValueFromString( name, *step.findAttribute( "value" ) ) == false )
                runner.recordFailure( step, "could not set " + name + " to '" + *step.findAttribute( "value" ) + "'" );
            return true;
        }

        static bool runExpectVariable( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner&         runner = asRunner( context );
            const string&             name   = *step.findAttribute( "name" );
            const string&             value  = *step.findAttribute( "value" );
            const GlobalVariableInfo* pInfo  = engine::getSubsystem<GlobalVariableManager>().findVariable( name );
            if ( pInfo == nullptr )
            {
                runner.recordFailure( step, name + " is no longer registered" ); // 검사 뒤에 모듈이 언로드됐다(핫 리로드)
                return true;
            }
            bool   bMatched = false;
            string error;
            if ( AutomationRunnerStepsInternal::evaluateExpectVariable( step, *pInfo, bMatched, error ) == false ) // 변수가 다른 타입으로 다시 등록됐다(핫 리로드)
                runner.recordFailure( step, error );
            else if ( bMatched == false )
                runner.recordFailure( step, name + " is '" + pInfo->getValueAsString() + "', expected '" + value + "'" +
                                                ( step.findAttribute( "tolerance" ) != nullptr ? " (tolerance " + *step.findAttribute( "tolerance" ) + ")" : string() ) );
            return true;
        }

        static bool runExpect( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._judge.runExpect( runner, step );
            return true;
        }

        static bool runExpectVisited( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._judge.runExpectVisited( runner, step );
            return true;
        }

        static bool runRecord( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._judge.runRecord( runner, step );
            return true;
        }

        static bool runRemember( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._judge.runRemember( runner, step );
            return true;
        }

        static bool runExpectLog( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._judge.runExpectLog( runner, step );
            return true;
        }

        static bool runScreenshot( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner    = asRunner( context );
            const string      path      = runner.resolveOutputPath( *step.findAttribute( "file" ) );
            const string      directory = FileUtil::getDirectoryPart( path );
            if ( directory.empty() == false )
                (void)FileUtil::ensureDirectoryExists( directory ); // 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다
            (void)FileUtil::tryRemoveFile( path );                  // 지난 실행의 이미지를 읽지 않게 지운다
            runner._run.requestScreenshot( path );
            return true;
        }

        static bool runExpectImage( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner._judge.runExpectImage( runner, step );
            return true;
        }

        /** @brief 종료 시한을 겁니다. @p bClose 면 창 닫기도 요청합니다(`CloseWindow`). 시한을 읽지 못하면(핫 리로드로 바뀐 단계) 오류로 끝나도록 false 입니다. */
        [[nodiscard]] static bool startExitDeadline( AutomationRunner& runner, const AutomationStep& step, bool bClose )
        {
            string  error;
            float32 seconds = 0.0f;
            if ( AutomationRunnerStepsInternal::parseExitSeconds( step, bClose ? "withinSeconds" : "seconds", seconds, error ) == false )
            {
                SW_LOG_ERROR( "[Scenario] %#", error.c_str() );
                return false;
            }
            runner._run._exitDeadlineSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 + static_cast<float64>( seconds );
            if ( bClose == false )
                return true;
            MainWindowSubsystem* pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
            IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
            if ( pWindow == nullptr )
                runner.recordFailure( step, "there is no window to close" );
            else
                pWindow->requestClose();
            return true;
        }

        static bool runCloseWindow( AutomationStepContext& context, const AutomationStep& step ) { return startExitDeadline( asRunner( context ), step, true ); }

        static bool runExpectExitWithin( AutomationStepContext& context, const AutomationStep& step ) { return startExitDeadline( asRunner( context ), step, false ); }

        static bool runPass( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            (void)step;
            runner.finish( AutomationOutcome::Passed, "<Pass/>" );
            return true;
        }

        static bool runFail( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner  = asRunner( context );
            const string*     pReason = step.findAttribute( "reason" );
            const string_view reason  = pReason != nullptr ? string_view{ *pReason } : string_view{ "<Fail/>" };
            runner.recordFailure( step, reason );
            runner.finish( AutomationOutcome::Failed, reason );
            return true;
        }

        static bool runSkip( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner  = asRunner( context );
            const string*     pReason = step.findAttribute( "reason" );
            runner.finish( AutomationOutcome::Skipped, pReason != nullptr ? string_view{ *pReason } : string_view{ "<Skip/>" } );
            return true;
        }

        // --- 차례(`AutomationStepHoldFunction`) -------------------------------------------------------------------

        /** @brief 조건이 참이 될 때까지(또는 시한까지) 시나리오 시계를 붙잡습니다. 놓으면 단계는 끝난다(실행 함수가 없다). */
        static AutomationStepHold holdWaitUntil( AutomationStepContext& context, const AutomationStep& step, InputManager& input )
        {
            AutomationRunner& runner = asRunner( context );
            return runner.shouldHoldForCondition( step, input ) ? AutomationStepHold::Hold : AutomationStepHold::Run;
        }

        /**
         * @brief 화면이 자리 잡고(`AutomationCaptureUtil::isReadyForCapture` — 로딩 화면 · 페이드 · 에셋 일이 끝남) 몇 프레임이 더 지날 때까지 스크린샷을 미룹니다.
         *        상한을 넘기면 실패로 적고 그대로 찍는다.
         * @details 메시 · 텍스처는 워커가 읽고 페이드는 실제 시간으로 걷힌다 — 첫 프레임을 찍으면 빈 메시 · 로드 중 회색 · 어두운 화면이 찍힌다. 시나리오마다 기다림을 적지 않고 찍는 단계가 기다린다.
         *          기다리는 동안은 `WaitUntil` 처럼 시나리오 시계를 붙잡는다 — 세면 뒤 단계의 프레임이 지나 버려 사진을 요청한 프레임에 한꺼번에 돌고,
         *          그 변화(순간이동 · 변수)가 다음 패킷이 찍는 사진에 들어간다.
         */
        static AutomationStepHold holdScreenshot( AutomationStepContext& context, const AutomationStep& step, InputManager& input )
        {
            AutomationRunner& runner = asRunner( context );
            if ( AutomationRunnerStepsInternal::capturesWithoutSettling( step ) )
                return AutomationStepHold::Run;
            const bool bSettled                 = AutomationCaptureUtil::isReadyForCapture( runner.findActiveObjectManager() );
            runner._run._assetSettledFrameCount = bSettled ? runner._run._assetSettledFrameCount + 1 : 0;
            const bool               bReady     = runner._run._assetSettledFrameCount > AutomationRunnerStepsInternal::kAssetSettleTrailFrames;
            const AutomationStepHold hold       = runner.holdUntilReady( step, input, bReady, AutomationTimeLimits::kMaxAssetSettleFrames );
            if ( hold == AutomationStepHold::Run )
            {
                if ( bReady == false )
                {
                    const utf8* pBlocker = AutomationStepRegistry::findCaptureBlocker();
                    runner.recordFailure( step, pBlocker != nullptr ? string( "the screen did not settle before the screenshot was due (still covered by " ) + pBlocker + ")"
                                                                    : string( "assets were still loading when the screenshot was due (Assets.Pending never reached 0)" ) );
                }
                runner._run._assetSettledFrameCount = 0;
            }
            return hold;
        }

        /** @brief 요청한 스크린샷이 다 써질 때까지 기다립니다. 상한을 넘기면 실패로 적고 측정하지 않고 넘깁니다. */
        static AutomationStepHold holdExpectImage( AutomationStepContext& context, const AutomationStep& step, InputManager& input )
        {
            AutomationRunner& runner = asRunner( context );
            (void)input;
            const bool bPending = runner._run.hasPendingScreenshot();
            if ( bPending == false )
                return AutomationStepHold::Run;
            if ( ++runner._run._imageWaitFrameCount <= AutomationTimeLimits::kMaxImageWaitFrames )
                return AutomationStepHold::Hold;
            runner.recordFailure( step, "the screenshot never completed (is there a renderer?)" );
            return AutomationStepHold::Skip;
        }
    };
} // namespace sw

namespace sw
{
    // 엔진 단계입니다. 다른 모듈의 단계와 같은 레지스트리에 올라, 같은 이름의 등록은 중복으로 거절됩니다.
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerPress, "Press", nullptr, &AutomationRunnerStepsInternal::validateSlotStep, &AutomationRunnerEngineSteps::prepareSlotStep, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerRelease, "Release", nullptr, &AutomationRunnerStepsInternal::validateSlotStep, &AutomationRunnerEngineSteps::prepareSlotStep, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerTap, "Tap", nullptr, &AutomationRunnerStepsInternal::validateSlotStep, &AutomationRunnerEngineSteps::prepareSlotStep, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerMouseDelta, "MouseDelta", nullptr, &AutomationRunnerStepsInternal::validateAxisPair, &AutomationRunnerEngineSteps::prepareMouseDelta,
                                   nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerWheel, "Wheel", nullptr, &AutomationRunnerStepsInternal::validateAxisPair, &AutomationRunnerEngineSteps::prepareWheel, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerMousePosition, "MousePosition", nullptr, &AutomationRunnerStepsInternal::validateMousePosition,
                                   &AutomationRunnerEngineSteps::prepareMousePosition, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerGamepadAxis, "GamepadAxis", nullptr, &AutomationRunnerStepsInternal::validateGamepadAxis, &AutomationRunnerEngineSteps::prepareGamepadAxis,
                                   nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerText, "Text", nullptr, &AutomationRunnerStepsInternal::validateText, &AutomationRunnerEngineSteps::prepareText, nullptr, false );
    SW_AUTOMATION_STEP( runnerVariable, "Variable", &AutomationRunnerEngineSteps::runVariable, &AutomationRunnerStepsInternal::validateVariable, false );
    SW_AUTOMATION_STEP( runnerExpectVariable, "ExpectVariable", &AutomationRunnerEngineSteps::runExpectVariable, &AutomationRunnerStepsInternal::validateExpectVariable, false );
    SW_AUTOMATION_STEP( runnerExpect, "Expect", &AutomationRunnerEngineSteps::runExpect, &AutomationRunnerStepsInternal::validateExpect, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerWaitUntil, "WaitUntil", nullptr, &AutomationRunnerStepsInternal::validateWaitUntil, &AutomationRunnerEngineSteps::prepareWaitUntil,
                                   &AutomationRunnerEngineSteps::holdWaitUntil, false );
    SW_AUTOMATION_STEP( runnerExpectVisited, "ExpectVisited", &AutomationRunnerEngineSteps::runExpectVisited, &AutomationRunnerStepsInternal::validateExpectVisited, false );
    SW_AUTOMATION_STEP( runnerRecord, "Record", &AutomationRunnerEngineSteps::runRecord, &AutomationRunnerStepsInternal::validateRecord, false );
    SW_AUTOMATION_STEP( runnerRemember, "Remember", &AutomationRunnerEngineSteps::runRemember, &AutomationRunnerStepsInternal::validateRemember, false );
    SW_AUTOMATION_STEP( runnerExpectLog, "ExpectLog", &AutomationRunnerEngineSteps::runExpectLog, &AutomationRunnerStepsInternal::validateExpectLog, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerScreenshot, "Screenshot", &AutomationRunnerEngineSteps::runScreenshot, &AutomationRunnerStepsInternal::validateScreenshot, nullptr,
                                   &AutomationRunnerEngineSteps::holdScreenshot, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerExpectImage, "ExpectImage", &AutomationRunnerEngineSteps::runExpectImage, &AutomationRunnerStepsInternal::validateExpectImage, nullptr,
                                   &AutomationRunnerEngineSteps::holdExpectImage, false );
    SW_AUTOMATION_STEP( runnerCloseWindow, "CloseWindow", &AutomationRunnerEngineSteps::runCloseWindow, &AutomationRunnerStepsInternal::validateCloseWindow, false );
    SW_AUTOMATION_STEP( runnerExpectExitWithin, "ExpectExitWithin", &AutomationRunnerEngineSteps::runExpectExitWithin, &AutomationRunnerStepsInternal::validateExpectExitWithin,
                        false );
    SW_AUTOMATION_STEP( runnerPass, "Pass", &AutomationRunnerEngineSteps::runPass, &AutomationRunnerStepsInternal::validatePass, false );
    SW_AUTOMATION_STEP( runnerFail, "Fail", &AutomationRunnerEngineSteps::runFail, &AutomationRunnerStepsInternal::validateFailOrSkip, false );
    SW_AUTOMATION_STEP( runnerSkip, "Skip", &AutomationRunnerEngineSteps::runSkip, &AutomationRunnerStepsInternal::validateFailOrSkip, false );
} // namespace sw
