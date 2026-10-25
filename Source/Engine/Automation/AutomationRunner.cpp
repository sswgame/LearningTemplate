#include "pch.h"

#include "Engine/Automation/AutomationRunner.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/AutomationCaptureUtil.h"
#include "Engine/Automation/AutomationEnvironmentSteps.h"
#include "Engine/Automation/AutomationExitCode.h"
#include "Engine/Automation/AutomationImageMetric.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Automation/AutomationThresholdBasis.h"
#include "Engine/Automation/AutomationWindowSteps.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    namespace
    {
        struct AutomationRunnerInternal
        {
            /** @brief `<ExpectImage>` 가 스크린샷을 기다리는 최대 프레임입니다. 렌더 큐 깊이보다 넉넉하게 잡습니다. */
            static constexpr uint32 kMaxImageWaitFrames = 30;
            /**
             * @brief 스크린샷이 에셋 일(읽는 중 · 나타나는 중인 메시, 올리기를 기다리는 텍스처)을 기다리는 최대 프레임 수입니다. 넘기면 실패로 적고 그대로 찍는다.
             * @details 고정 프레임 시간(60 Hz)에서 10 초다 — 첫 프레임에 큰 씬의 텍스처 수백 장이 워커에 줄을 서도 넉넉하다.
             */
            static constexpr uint32 kMaxAssetSettleFrames = 600;
            /**
             * @brief 에셋 일이 0 이 된 뒤 스크린샷 전에 더 기다리는 프레임 수입니다. 올린 텍스처의 새 SRV 는 다음 씬 빌드가 머티리얼에 넣고(`Material::refreshTextureBindings`),
             *        그 스냅샷이 렌더 큐(깊이 2)를 지나야 화면에 나온다.
             */
            static constexpr uint32 kAssetSettleTrailFrames = 3;
            /** @brief `darkFraction` 의 기본 임계값(영역 중앙값에 곱한다)입니다. */
            static constexpr float32 kDefaultDarkRatio = 0.7f;
            /** @brief `CloseWindow` 의 기본 종료 시한(초)입니다. */
            static constexpr float32 kDefaultExitWithinSeconds = 10.0f;
            /** @brief `WaitUntil` 의 기본 시한(프레임)입니다. 부하가 걸린 기계에서 에디터가 에셋을 여는 데 드는 시간보다 넉넉하게 잡습니다. */
            static constexpr uint32 kDefaultWaitTimeoutFrames = 1200;
            /** @brief 시계 잡기 조건 하나가 이어서 시계를 붙잡을 수 있는 벽시계 시한(초)입니다. 저장 검증(파이썬)은 쉬는 기계에서 몇 초, 빌드가 겹치면 30 초를 넘긴다. */
            static constexpr float64 kClockHoldTimeoutSeconds = 180.0;

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

            /** @brief 있으면 수로 읽습니다. 없으면 @p inoutValue 를 둡니다(기본값은 아래 단계 인자 구조체의 초기값). 형식이 틀리면 false 와 이유이고 값은 그대로입니다. */
            [[nodiscard]] static bool readFloat( const AutomationStep& step, string_view name, float32& inoutValue, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    return true;
                float32 value = 0.0f;
                if ( StringUtil::parseFloat( string_view{ *pValue }, value ) )
                {
                    inoutValue = value;
                    return true;
                }
                outError = step.describe() + ": " + string( name ) + " must be a number, got '" + *pValue + "'";
                return false;
            }

            /** @brief 있으면 음이 아닌 정수로 읽습니다. 없으면 @p inoutValue 를 둡니다. */
            [[nodiscard]] static bool readUint( const AutomationStep& step, string_view name, uint32& inoutValue, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    return true;
                int32 value = 0;
                if ( StringUtil::parseInt( string_view{ *pValue }, value ) && value >= 0 )
                {
                    inoutValue = static_cast<uint32>( value );
                    return true;
                }
                outError = step.describe() + ": " + string( name ) + " must be a non-negative integer, got '" + *pValue + "'";
                return false;
            }

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

            /** @brief 수 비교 하나(`equals` · `near` · `atLeast` · `atMost`)입니다. */
            enum class NumberRule : uint8
            {
                Equals,
                Near,
                AtLeast,
                AtMost,
            };
            struct NumberComparison
            {
                string     _text{}; ///< 데이터에 적힌 기대값 글(실패 메시지)
                float32    _expected{ 0.0f };
                float32    _tolerance{ 0.0f }; ///< `near` 의 허용 폭
                NumberRule _rule{ NumberRule::Equals };
            };

            static constexpr float32 kProbeNearTolerance = 1.0e-4f; ///< 탐침 `near` 의 기본 허용 폭
            static constexpr float32 kImageNearTolerance = 1.0e-3f; ///< 이미지 측정 `near` 의 기본 허용 폭

            /** @brief `WaitUntil` 의 시한입니다. `timeoutSeconds` 를 적었으면 초, 아니면 프레임입니다. */
            struct WaitTimeoutArgs
            {
                uint32  _timeoutFrames{ kDefaultWaitTimeoutFrames };
                float32 _timeoutSeconds{ 0.0f };
                bool    _bSeconds{ false };
            };

            /** @brief `ExpectLog` 의 인자입니다. */
            struct ExpectLogArgs
            {
                uint32 _since{ 0 };
                uint32 _expected{ 0 };
                bool   _bExactCount{ false }; ///< 참이면 `count`(같아야 한다), 거짓이면 `atLeast`
            };

            /** @brief `ExpectImage` 의 인자입니다. */
            struct ExpectImageArgs
            {
                NumberComparison          _comparison{};
                AutomationThresholdBasis  _basis{}; ///< 문턱이 잰 값(`measured`)인지 어림값(`provisional`)인지
                AutomationImageRegion     _region{};
                float32                   _ratio{ kDefaultDarkRatio };
                AutomationImageMetricKind _metric{ AutomationImageMetricKind::MeanLuma };
            };

            [[nodiscard]] static bool parseSlotStep( const AutomationStep& step, SlotStepArgs& outArgs, string& outError )
            {
                if ( readUint( step, "hold", outArgs._holdFrameCount, outError ) == false )
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
                return readFloat( step, "x", outArgs._x, outError ) && readFloat( step, "y", outArgs._y, outError );
            }

            [[nodiscard]] static bool parseGamepadAxis( const AutomationStep& step, GamepadAxisArgs& outArgs, string& outError )
            {
                return step.requireAttribute( "axis", outError ) != nullptr && step.requireAttribute( "value", outError ) != nullptr &&
                       readUint( step, "axis", outArgs._axis, outError ) && readFloat( step, "value", outArgs._value, outError ) && readUint( step, "pad", outArgs._pad, outError );
            }

            /** @brief 수 비교 하나를 읽습니다. `equals` · `near` · `atLeast` · `atMost` 중 적힌 것이 없으면 false 입니다(정확히 하나인지는 `countComparisons`). */
            [[nodiscard]] static bool parseNumberComparison( const AutomationStep& step, float32 nearTolerance, NumberComparison& outComparison, string& outError )
            {
                static constexpr std::pair<const utf8*, NumberRule> kArrRule[] = {
                    { "equals",  NumberRule::Equals},
                    {   "near",    NumberRule::Near},
                    {"atLeast", NumberRule::AtLeast},
                    { "atMost",  NumberRule::AtMost},
                };
                outComparison._tolerance = nearTolerance;
                for ( const std::pair<const utf8*, NumberRule>& entry : kArrRule )
                {
                    const string* pText = step.findAttribute( entry.first );
                    if ( pText == nullptr )
                        continue;
                    outComparison._rule = entry.second;
                    outComparison._text = *pText;
                    return readFloat( step, entry.first, outComparison._expected, outError ) && readFloat( step, "tolerance", outComparison._tolerance, outError );
                }
                outError = step.describe() + ": needs one of equals · near · atLeast · atMost";
                return false;
            }

            /** @brief @p comparison 대로 @p value 를 비교합니다. 실패 메시지의 규칙 글을 @p outRule 에 씁니다. */
            static bool matchesNumber( const NumberComparison& comparison, float64 value, string& outRule )
            {
                const float64 expected = static_cast<float64>( comparison._expected );
                switch ( comparison._rule )
                {
                    case NumberRule::Equals:
                    {
                        outRule = "== " + comparison._text;
                        return value == expected;
                    }
                    case NumberRule::Near:
                    {
                        outRule = "near " + comparison._text;
                        return MathUtil::abs( value - expected ) <= static_cast<float64>( comparison._tolerance );
                    }
                    case NumberRule::AtLeast:
                    {
                        outRule = ">= " + comparison._text;
                        return value >= expected;
                    }
                    case NumberRule::AtMost:
                    {
                        outRule = "<= " + comparison._text;
                        return value <= expected;
                    }
                }
                return false;
            }

            [[nodiscard]] static bool parseWaitTimeout( const AutomationStep& step, WaitTimeoutArgs& outArgs, string& outError )
            {
                outArgs._bSeconds = step.findAttribute( "timeoutSeconds" ) != nullptr;
                return readUint( step, "timeoutFrames", outArgs._timeoutFrames, outError ) && readFloat( step, "timeoutSeconds", outArgs._timeoutSeconds, outError );
            }

            [[nodiscard]] static bool parseExpectLog( const AutomationStep& step, ExpectLogArgs& outArgs, string& outError )
            {
                outArgs._bExactCount = step.findAttribute( "count" ) != nullptr;
                return readUint( step, "since", outArgs._since, outError ) && readUint( step, outArgs._bExactCount ? "count" : "atLeast", outArgs._expected, outError );
            }

            [[nodiscard]] static bool parseExpectImage( const AutomationStep& step, ExpectImageArgs& outArgs, string& outError )
            {
                const string* pMetric = step.requireAttribute( "metric", outError );
                if ( pMetric == nullptr )
                    return false;
                if ( AutomationImageMetric::tryParseKind( *pMetric, outArgs._metric ) == false )
                {
                    outError = step.describe() + ": unknown metric '" + *pMetric + "' (meanLuma · darkFraction · meanRedMinusBlue · differentFrom · differingFraction)";
                    return false;
                }
                const string* pRegion = step.findAttribute( "region" );
                if ( pRegion != nullptr && AutomationImageMetric::tryParseRegion( *pRegion, outArgs._region ) == false )
                {
                    outError = step.describe() + ": region must be x0,y0,x1,y1 in 0..1 with x0<x1 and y0<y1, got '" + *pRegion + "'";
                    return false;
                }
                // 문턱 숫자가 잰 것인지 어림인지는 단언 옆에 형식으로 남는다 — 적지 않은 단언은 읽기 오류다.
                if ( AutomationThresholdBasis::tryParse( step.findAttribute( "measured" ), step.findAttribute( "provisional" ), outArgs._basis, outError ) == false )
                {
                    outError = step.describe() + ": " + outError;
                    return false;
                }
                return readFloat( step, "ratio", outArgs._ratio, outError ) && parseNumberComparison( step, kImageNearTolerance, outArgs._comparison, outError );
            }

            /** @brief 종료 시한 단계의 초입니다. 없으면 `kDefaultExitWithinSeconds` 입니다. */
            [[nodiscard]] static bool parseExitSeconds( const AutomationStep& step, const utf8* pName, float32& outSeconds, string& outError )
            {
                outSeconds = kDefaultExitWithinSeconds;
                return readFloat( step, pName, outSeconds, outError );
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
             * @brief 변수의 지금 값이 @p expected 와 같은지 @p outMatched 에 적습니다. 실수는 @p tolerance 안이면 같습니다.
             * @return @p expected 를 변수의 타입으로 읽지 못하면 false 입니다(enum 은 숫자만 받는다).
             */
            [[nodiscard]] static bool compareVariable( const GlobalVariableInfo& info, string_view expected, float32 tolerance, bool& outMatched )
            {
                switch ( info._type )
                {
                    case GlobalVariableType::Boolean:
                    {
                        bool value = false;
                        if ( StringUtil::tryParseBool( expected, value ) == false )
                            return false;
                        outMatched = info.getValueAsBool() == value;
                        return true;
                    }
                    case GlobalVariableType::Int32:
                    case GlobalVariableType::Enum:
                    {
                        int32 value = 0;
                        if ( StringUtil::parseInt( expected, value ) == false )
                            return false;
                        outMatched = info.getValueAsInt() == value;
                        return true;
                    }
                    case GlobalVariableType::Float:
                    {
                        float32 value = 0.0f;
                        if ( StringUtil::parseFloat( expected, value ) == false )
                            return false;
                        outMatched = MathUtil::abs( info.getValueAsFloat() - value ) <= tolerance;
                        return true;
                    }
                    case GlobalVariableType::String:
                    {
                        outMatched = string_view{ info.getValueAsString() } == expected;
                        return true;
                    }
                    case GlobalVariableType::Count:
                    {
                        break;
                    }
                }
                return false;
            }

            /**
             * @brief `ExpectVariable` 의 `value` · `tolerance` 를 @p info 의 타입으로 읽어 지금 값과 비교합니다(검사와 실행이 함께 씁니다).
             * @return `value` 가 그 타입으로 읽히지 않거나 `tolerance` 가 음수이거나 실수 변수가 아닌데 붙었으면 false 와 이유입니다.
             */
            [[nodiscard]] static bool evaluateExpectVariable( const AutomationStep& step, const GlobalVariableInfo& info, bool& outMatched, string& outError )
            {
                float32 tolerance = 0.0f; // 적지 않으면 정확히 같아야 한다
                if ( readFloat( step, "tolerance", tolerance, outError ) == false )
                    return false;
                if ( step.findAttribute( "tolerance" ) != nullptr && ( info._type != GlobalVariableType::Float || tolerance < 0.0f ) )
                {
                    outError = step.describe() + ": tolerance is a non-negative number for float variables only";
                    return false;
                }
                const string& value = *step.findAttribute( "value" );
                if ( compareVariable( info, string_view{ value }, tolerance, outMatched ) == false )
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

            /** @brief 탐침 이름과 그 탐침이 꼭 받아야 하는 `target` · `key` 를 봅니다. */
            static bool validateProbeReference( const AutomationStep& step, string& outError )
            {
                const string* pProbe = step.requireAttribute( "probe", outError );
                if ( pProbe == nullptr )
                    return false;
                const AutomationProbeRegistration* pRegistration = AutomationProbes::find( *pProbe );
                if ( pRegistration == nullptr )
                {
                    outError = step.describe() + ": unknown probe '" + *pProbe + "' (is the game that registers it running?)";
                    return false;
                }
                const bool bQueryOnlyAttribute = pRegistration->_pQueryFunction == nullptr && ( step.findAttribute( "target" ) != nullptr || step.findAttribute( "key" ) != nullptr );
                if ( bQueryOnlyAttribute )
                {
                    outError = step.describe() + ": probe '" + *pProbe + "' takes no target or key";
                    return false;
                }
                if ( pRegistration->_bNeedsTarget && step.requireAttribute( "target", outError ) == nullptr )
                    return false;
                return pRegistration->_bNeedsKey == false || step.requireAttribute( "key", outError ) != nullptr;
            }

            /** @brief 탐침 단계(`Expect`, `WaitUntil`)의 공통 부분입니다. 탐침이 있고 비교가 정확히 하나이며 수 비교는 수로 읽혀야 합니다(`is` 는 글 비교). */
            static bool validateProbeComparison( const AutomationStep& step, string& outError )
            {
                if ( validateProbeReference( step, outError ) == false )
                    return false;
                if ( countComparisons( step, { "equals", "near", "atLeast", "atMost", "is", "sameAs", "differsFrom" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of equals · near · atLeast · atMost · is · sameAs · differsFrom";
                    return false;
                }
                const bool bTextRule = step.findAttribute( "is" ) != nullptr || step.findAttribute( "sameAs" ) != nullptr || step.findAttribute( "differsFrom" ) != nullptr;
                if ( bTextRule )
                    return true;
                NumberComparison comparison;
                return parseNumberComparison( step, kProbeNearTolerance, comparison, outError );
            }

            static bool validateExpect( const AutomationStep& step, string& outError )
            {
                return step.hasOnlyAttributes( { "probe", "target", "key", "equals", "near", "tolerance", "atLeast", "atMost", "is", "sameAs", "differsFrom" }, outError ) &&
                       validateProbeComparison( step, outError );
            }

            /** @brief `WaitUntil` 입니다. 그 프레임에 혼자 있는지는 준비 함수(`AutomationRunnerEngineSteps::prepareWaitUntil`)가 시나리오 전체를 보고 확인합니다. */
            static bool validateWaitUntil( const AutomationStep& step, string& outError )
            {
                if ( step.hasOnlyAttributes( { "probe", "target", "key", "equals", "near", "tolerance", "atLeast", "atMost", "is", "sameAs", "differsFrom", "timeoutFrames",
                                               "timeoutSeconds" },
                                             outError ) == false )
                    return false;
                if ( step.findAttribute( "timeoutFrames" ) != nullptr && step.findAttribute( "timeoutSeconds" ) != nullptr )
                {
                    outError = step.describe() + ": give timeoutFrames or timeoutSeconds, not both";
                    return false;
                }
                WaitTimeoutArgs timeout;
                return parseWaitTimeout( step, timeout, outError ) && validateProbeComparison( step, outError );
            }

            /** @brief `ExpectVisited` 입니다. `is`(그 값을 한 번이라도 거쳤다) 또는 `sequence`(쉼표로 적은 값을 이 순서로 거쳤다) 하나와 `since`(프레임) 입니다. */
            static bool validateExpectVisited( const AutomationStep& step, string& outError )
            {
                uint32 since = 0; // 적지 않으면 처음부터 본다
                if ( step.hasOnlyAttributes( { "probe", "target", "key", "is", "sequence", "since" }, outError ) == false || validateProbeReference( step, outError ) == false )
                    return false;
                if ( countComparisons( step, { "is", "sequence" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of is · sequence";
                    return false;
                }
                return readUint( step, "since", since, outError );
            }

            /** @brief `Record` 입니다. 탐침 값을 보고의 `records` 에 남깁니다. */
            static bool validateRecord( const AutomationStep& step, string& outError )
            {
                return step.hasOnlyAttributes( { "probe", "target", "key" }, outError ) && validateProbeReference( step, outError );
            }

            /** @brief `Remember` 입니다. 탐침 값을 `as` 이름으로 기억합니다(뒤 단계의 `sameAs` · `differsFrom` 이 비교한다). */
            static bool validateRemember( const AutomationStep& step, string& outError )
            {
                return step.hasOnlyAttributes( { "probe", "target", "key", "as" }, outError ) && step.requireAttribute( "as", outError ) != nullptr &&
                       validateProbeReference( step, outError );
            }

            /** @brief 쉼표로 가른 값 목록입니다(앞뒤 공백을 제거하고 빈 항목은 버린다). */
            static vector<string> splitList( string_view text )
            {
                vector<string> listValue;
                while ( text.empty() == false )
                {
                    const size_t      comma = text.find( ',' );
                    const string_view value = StringUtil::trim( text.substr( 0, comma ) );
                    if ( value.empty() == false )
                        listValue.emplace_back( value );
                    text = comma == string_view::npos ? string_view{} : text.substr( comma + 1 );
                }
                return listValue;
            }

            /** @brief 탐침 값의 글입니다(수는 끝의 0 을 정리한다). */
            static string makeValueText( const AutomationProbeValue& value ) { return value._bText ? value._text : formatNumber( value._number ); }

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
                ExpectLogArgs args;
                return parseExpectLog( step, args, outError );
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
             *          그런 시나리오는 앞에서 `<WaitUntil probe="Assets.Pending" equals="0">` 로 로드를 끝내 둔다.
             */
            [[nodiscard]] static bool capturesWithoutSettling( const AutomationStep& step )
            {
                bool       bWait = true;
                const bool bRead = readWaitForSettle( step, bWait ); // 읽지 못한 값은 validateScreenshot 이 이미 거절했다
                return bRead && bWait == false;
            }

            static bool validateExpectImage( const AutomationStep& step, string& outError )
            {
                if ( step.hasOnlyAttributes( { "file", "region", "metric", "ratio", "reference", "equals", "near", "tolerance", "atLeast", "atMost", "measured", "provisional" },
                                             outError ) == false ||
                     step.requireAttribute( "file", outError ) == nullptr )
                    return false;
                if ( countComparisons( step, { "equals", "near", "atLeast", "atMost" } ) == false )
                {
                    outError = step.describe() + ": needs exactly one of equals · near · atLeast · atMost";
                    return false;
                }
                ExpectImageArgs args;
                if ( parseExpectImage( step, args, outError ) == false )
                    return false;
                if ( AutomationImageMetric::usesReference( args._metric ) != ( step.findAttribute( "reference" ) != nullptr ) )
                {
                    outError = step.describe() + ": reference=\"…\" goes with metric=\"differentFrom\" or \"differingFraction\" (and only with them)";
                    return false;
                }
                // 평균 차의 하한 · 상한은 서로 다른 화면이 같은 평균을 내면 통과한다 — "다르다 · 같다" 는 화소를 세는 differingFraction 으로만 쓴다.
                if ( args._metric == AutomationImageMetricKind::DifferentFrom && step.findAttribute( "near" ) == nullptr )
                {
                    outError = step.describe() + ": differentFrom is an amount (near only); use differingFraction with atLeast / atMost to say \"differs\" or \"same\"";
                    return false;
                }
                return true;
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

            /** @brief 탐침 `Input.OsDeviceInputBlocked` 입니다. 무인 자동화가 사람의 마우스와 키보드를 창 메시지 단계에서 버리는 중이면 1 입니다. */
            [[nodiscard]] static bool readOsDeviceInputBlocked( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                if ( engine::areEngineServicesBound() == false )
                    return false;
                outValue = engine::getSubsystem<InputManager>().isOsDeviceInputBlocked() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 탐침 `Input.VirtualCursorEnabled` 입니다. 패드 가상 커서가 켜져 있으면 1 입니다(명령형 디렉터가 플레이 동안 켠다). */
            [[nodiscard]] static bool readVirtualCursorEnabled( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                if ( engine::areEngineServicesBound() == false )
                    return false;
                outValue = engine::getSubsystem<InputManager>().getVirtualCursor().isEnabled() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 탐침 `Input.MouseX` · `Input.MouseY` 입니다. 마우스(가상 커서가 옮긴 것 포함)의 창 클라이언트 픽셀 위치입니다. */
            [[nodiscard]] static bool readMousePosition( bool bX, float64& outValue )
            {
                if ( engine::areEngineServicesBound() == false )
                    return false;
                const MouseDevice* pMouse = engine::getSubsystem<InputManager>().getMouse();
                if ( pMouse == nullptr )
                    return false;
                const int2 position = pMouse->getPosition();
                outValue            = static_cast<float64>( bX ? position._x : position._y );
                return true;
            }

            [[nodiscard]] static bool readMouseX( const GameObjectManager* /*pManager*/, float64& outValue ) { return readMousePosition( true, outValue ); }
            [[nodiscard]] static bool readMouseY( const GameObjectManager* /*pManager*/, float64& outValue ) { return readMousePosition( false, outValue ); }

            /** @brief 탐침 `Assets.Pending` 입니다. 씬이 기다리는 에셋 일의 수입니다(`AutomationCaptureUtil::countPendingAssetWork`). 0 이면 화면이 다 읽힌 상태입니다. */
            [[nodiscard]] static bool readAssetsPending( const GameObjectManager* pManager, float64& outValue )
            {
                outValue = static_cast<float64>( AutomationCaptureUtil::countPendingAssetWork( pManager ) );
                return true;
            }

            /** @brief 질의 탐침의 대상 오브젝트 월드 위치입니다. 대상이 없으면 이유를 적고 false 입니다. */
            [[nodiscard]] static bool readTargetPosition( const AutomationProbeQuery& query, float3& outPosition, AutomationProbeValue& outValue )
            {
                const GameObject*     pTarget = query.findTargetObject();
                const SceneComponent* pScene  = pTarget != nullptr ? pTarget->getPrimarySceneComponent() : nullptr;
                if ( pScene == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "transform" ) );
                    return false;
                }
                outPosition = pScene->getWorldPosition();
                return true;
            }

            /** @brief 탐침 `Object.X` · `Object.Y` · `Object.Z` 의 몸통입니다. */
            [[nodiscard]] static bool readObjectAxis( const AutomationProbeQuery& query, uint32 axis, AutomationProbeValue& outValue )
            {
                float3 position{};
                if ( readTargetPosition( query, position, outValue ) == false )
                    return false;
                outValue.setNumber( static_cast<float64>( axis == 0 ? position._x : ( axis == 1 ? position._y : position._z ) ) );
                return true;
            }

            [[nodiscard]] static bool readObjectX( const AutomationProbeQuery& query, AutomationProbeValue& outValue ) { return readObjectAxis( query, 0, outValue ); }
            [[nodiscard]] static bool readObjectY( const AutomationProbeQuery& query, AutomationProbeValue& outValue ) { return readObjectAxis( query, 1, outValue ); }
            [[nodiscard]] static bool readObjectZ( const AutomationProbeQuery& query, AutomationProbeValue& outValue ) { return readObjectAxis( query, 2, outValue ); }

            /** @brief 탐침 `Object.Exists` 입니다. 활성 씬에 그 오브젝트가 있으면 1, 없으면 0 입니다. 고를 수 없는 대상(같은 이름 여럿, 틀린 번호 글)은 값이 없습니다. */
            [[nodiscard]] static bool readObjectExists( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                if ( query.getTargetLookup() == AutomationTargetLookup::Rejected )
                {
                    outValue.setText( query.getTargetProblem() );
                    return false;
                }
                outValue.setNumber( query.getTargetLookup() == AutomationTargetLookup::Found ? 1.0 : 0.0 );
                return true;
            }

            /**
             * @brief 탐침 `Object.Property` 입니다. 대상 오브젝트의 컴포넌트 PROPERTY 값(`key="<컴포넌트 타입>.<프로퍼티>"`)입니다.
             * @details 수(float32 · int32 · uint32)와 글(string)을 읽는다. 선택과 무관하게 이름으로 고르므로 여러 오브젝트를 한 프레임에 비교할 수 있다
             *          (프리팹 인스턴스마다의 값, 플레이 전후의 값). 비트필드 bool 은 PROPERTY 주소가 없어 읽지 못한다.
             */
            [[nodiscard]] static bool readObjectProperty( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                const GameObject* pTarget = query.findTargetObject();
                if ( pTarget == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return false;
                }
                const size_t dot = query._key.find( '.' );
                if ( dot == string_view::npos )
                {
                    outValue.setText( "key must be <ComponentType>.<property>" );
                    return false;
                }
                const string componentName( query._key.substr( 0, dot ) );
                const string propertyName( query._key.substr( dot + 1 ) );
                Component*   pComponent = pTarget->findComponentByTypeName( hashed_string( componentName ) );
                if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                {
                    outValue.setText( "no " + componentName + " on the target" );
                    return false;
                }
                const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( propertyName ) );
                if ( pProperty == nullptr )
                {
                    outValue.setText( "no property " + propertyName );
                    return false;
                }
                if ( pProperty->_typeName == hashed_string( "float32" ) )
                    outValue.setNumber( static_cast<float64>( *pProperty->getValuePtr<float32>( pComponent ) ) );
                else if ( pProperty->_typeName == hashed_string( "int32" ) )
                    outValue.setNumber( static_cast<float64>( *pProperty->getValuePtr<int32>( pComponent ) ) );
                else if ( pProperty->_typeName == hashed_string( "uint32" ) )
                    outValue.setNumber( static_cast<float64>( *pProperty->getValuePtr<uint32>( pComponent ) ) );
                else if ( pProperty->_typeName == hashed_string( "string" ) )
                    outValue.setText( *pProperty->getValuePtr<string>( pComponent ) );
                else
                {
                    outValue.setText( "property type " + string( pProperty->_typeName.c_str() ) + " is not readable here" );
                    return false;
                }
                return true;
            }

            /** @brief 탐침 `Object.DistanceTo` 입니다. 대상과 `key` 이름의 오브젝트 사이 월드 거리(m)입니다. */
            [[nodiscard]] static bool readObjectDistanceTo( const AutomationProbeQuery& query, AutomationProbeValue& outValue )
            {
                float3 position{};
                if ( readTargetPosition( query, position, outValue ) == false )
                    return false;
                const AutomationProbeQuery otherQuery = AutomationProbeQuery::make( query._pManager, query._key, string_view{} );
                float3                     otherPosition{};
                if ( readTargetPosition( otherQuery, otherPosition, outValue ) == false )
                    return false;
                outValue.setNumber( static_cast<float64>( float3::getDistance( position, otherPosition ) ) );
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
    SW_AUTOMATION_PROBE( inputVirtualCursorEnabled, "Input.VirtualCursorEnabled", "1 while the gamepad virtual cursor is on (command directors turn it on for play)",
                         &AutomationRunnerInternal::readVirtualCursorEnabled );
    SW_AUTOMATION_PROBE( inputMouseX, "Input.MouseX", "Mouse pointer X in window client pixels (the gamepad virtual cursor moves it too)", &AutomationRunnerInternal::readMouseX );
    SW_AUTOMATION_PROBE( inputMouseY, "Input.MouseY", "Mouse pointer Y in window client pixels (the gamepad virtual cursor moves it too)", &AutomationRunnerInternal::readMouseY );
    SW_AUTOMATION_PROBE( assetsPending, "Assets.Pending", "Meshes still loading, waiting for animation or dithering in, plus textures still loading or uploading (0 = settled)",
                         &AutomationRunnerInternal::readAssetsPending );
    SW_AUTOMATION_QUERY_PROBE( objectX, "Object.X", "World X of the target object (m)", &AutomationRunnerInternal::readObjectX, true, false );
    SW_AUTOMATION_QUERY_PROBE( objectY, "Object.Y", "World Y of the target object (m)", &AutomationRunnerInternal::readObjectY, true, false );
    SW_AUTOMATION_QUERY_PROBE( objectZ, "Object.Z", "World Z of the target object (m)", &AutomationRunnerInternal::readObjectZ, true, false );
    SW_AUTOMATION_QUERY_PROBE( objectExists, "Object.Exists", "1 when the active scene has an object named target", &AutomationRunnerInternal::readObjectExists, true, false );
    SW_AUTOMATION_QUERY_PROBE( objectProperty, "Object.Property", "Component property of the target object (key = <ComponentType>.<property>; numbers and strings)",
                               &AutomationRunnerInternal::readObjectProperty, true, true );
    SW_AUTOMATION_QUERY_PROBE( objectDistanceTo, "Object.DistanceTo", "World distance from the target object to the object named key (m)",
                               &AutomationRunnerInternal::readObjectDistanceTo, true, true );
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
        /** @brief 엔진 단계는 실행기 자신의 상태(가상 입력 원천 · 스크린샷 대기열 · 종료 시한)를 만진다. 그래서 문맥을 실행기로 되돌려 쓴다 — 이 단계들은 실행기에만 등록되어 돈다. */
        static AutomationRunner& asRunner( AutomationStepContext& context ) { return static_cast<AutomationRunner&>( context ); }

        // --- 준비(`AutomationStepPrepareFunction`): 입력 단계는 가상 입력 원천으로 옮긴다. 인자는 검사와 같은 `parse*` 로 읽고, 실패하면 버리지 않고 오류다 ---

        static bool prepareSlotStep( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner&                      runner = asRunner( context );
            AutomationRunnerInternal::SlotStepArgs args;
            if ( AutomationRunnerInternal::parseSlotStep( step, args, outError ) == false )
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

        static bool prepareWheel( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner&                      runner = asRunner( context );
            AutomationRunnerInternal::AxisPairArgs args;
            if ( AutomationRunnerInternal::parseAxisPair( step, args, outError ) == false )
                return false;
            if ( args._y != 0.0f )
                runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeMouseWheel( args._y ) );
            if ( args._x != 0.0f )
                runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeMouseHorizontalWheel( args._x ) );
            return true;
        }

        static bool prepareMouseDelta( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner&                      runner = asRunner( context );
            AutomationRunnerInternal::AxisPairArgs args;
            if ( AutomationRunnerInternal::parseAxisPair( step, args, outError ) == false )
                return false;
            runner._inputScript.addMouseDelta( step._frameIndex, args._x, args._y );
            return true;
        }

        static bool prepareMousePosition( AutomationStepContext& context, const AutomationStep& step, string& outError )
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
            AutomationRunnerInternal::AxisPairArgs args;
            if ( AutomationRunnerInternal::parseAxisPair( step, args, outError ) == false )
                return false;
            const float32 maxX = static_cast<float32>( pWindow->getWidth() - 1 );
            const float32 maxY = static_cast<float32>( pWindow->getHeight() - 1 );
            runner._inputScript.addMousePosition( step._frameIndex, static_cast<int32>( MathUtil::round( args._x * maxX ) ), static_cast<int32>( MathUtil::round( args._y * maxY ) ) );
            return true;
        }

        static bool prepareGamepadAxis( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner&                         runner = asRunner( context );
            AutomationRunnerInternal::GamepadAxisArgs args;
            if ( AutomationRunnerInternal::parseGamepadAxis( step, args, outError ) == false )
                return false;
            // 가상 입력 세션의 패드는 끊긴 채 시작하고 끊긴 패드의 스틱은 입력 맵 · 가상 커서가 읽지 않는다 — 축을 내는 단계는 그 패드를 먼저 연결한다.
            runner._inputScript.addGamepadConnection( step._frameIndex, static_cast<uint8>( args._pad ) );
            runner._inputScript.addGamepadAxis( step._frameIndex, static_cast<uint16>( args._axis ), args._value, static_cast<uint8>( args._pad ) );
            return true;
        }

        static bool prepareText( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner& runner = asRunner( context );
            const string*     pValue = step.requireAttribute( "value", outError );
            if ( pValue == nullptr )
                return false;
            runner._inputScript.addEvent( step._frameIndex, RawInputEvent::makeTextInput( *pValue ) );
            return true;
        }

        /** @brief 붙잡는 동안 같은 프레임의 다른 단계가 여러 번 돌거나(입력 전 단계) 조건보다 먼저 돌지 않게, 기다림은 그 프레임에 혼자 둔다. */
        static bool prepareWaitUntil( AutomationStepContext& context, const AutomationStep& step, string& outError )
        {
            AutomationRunner& runner = asRunner( context );
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
            if ( AutomationRunnerInternal::evaluateExpectVariable( step, *pInfo, bMatched, error ) == false ) // 변수가 다른 타입으로 다시 등록됐다(핫 리로드)
                runner.recordFailure( step, error );
            else if ( bMatched == false )
                runner.recordFailure( step, name + " is '" + pInfo->getValueAsString() + "', expected '" + value + "'" +
                                                ( step.findAttribute( "tolerance" ) != nullptr ? " (tolerance " + *step.findAttribute( "tolerance" ) + ")" : string() ) );
            return true;
        }

        static bool runExpect( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner.runExpect( step );
            return true;
        }

        static bool runExpectVisited( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner.runExpectVisited( step );
            return true;
        }

        static bool runRecord( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner.runRecord( step );
            return true;
        }

        static bool runRemember( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner.runRemember( step );
            return true;
        }

        static bool runExpectLog( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner.runExpectLog( step );
            return true;
        }

        static bool runScreenshot( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner    = asRunner( context );
            const string      path      = runner.resolveOutputPath( *step.findAttribute( "file" ) );
            const string      directory = FileUtil::getDirectoryPart( path );
            if ( directory.empty() == false )
                (void)FileUtil::ensureDirectoryExists( directory );
            (void)FileUtil::tryRemoveFile( path ); // 지난 실행의 이미지를 읽지 않게 지운다
            runner._listPendingScreenshot.push_back( path );
            ++runner._screenshotRequestedCount;
            return true;
        }

        static bool runExpectImage( AutomationStepContext& context, const AutomationStep& step )
        {
            AutomationRunner& runner = asRunner( context );
            runner.runExpectImage( step );
            return true;
        }

        /** @brief 종료 시한을 겁니다. @p bClose 면 창 닫기도 요청합니다(`CloseWindow`). 시한을 읽지 못하면(핫 리로드로 바뀐 단계) 오류로 끝나도록 false 입니다. */
        [[nodiscard]] static bool startExitDeadline( AutomationRunner& runner, const AutomationStep& step, bool bClose )
        {
            string  error;
            float32 seconds = 0.0f;
            if ( AutomationRunnerInternal::parseExitSeconds( step, bClose ? "withinSeconds" : "seconds", seconds, error ) == false )
            {
                SW_LOG_ERROR( "[Scenario] %#", error.c_str() );
                return false;
            }
            runner._exitDeadlineSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 + static_cast<float64>( seconds );
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
            if ( AutomationRunnerInternal::capturesWithoutSettling( step ) )
                return AutomationStepHold::Run;
            const bool bSettled             = AutomationCaptureUtil::isReadyForCapture( runner.findActiveObjectManager() );
            runner._assetSettledFrameCount  = bSettled ? runner._assetSettledFrameCount + 1 : 0;
            const bool               bReady = runner._assetSettledFrameCount > AutomationRunnerInternal::kAssetSettleTrailFrames;
            const AutomationStepHold hold   = runner.holdUntilReady( step, input, bReady, AutomationRunnerInternal::kMaxAssetSettleFrames );
            if ( hold == AutomationStepHold::Run )
            {
                if ( bReady == false )
                {
                    const utf8* pBlocker = AutomationStepRegistry::findCaptureBlocker();
                    runner.recordFailure( step, pBlocker != nullptr ? string( "the screen did not settle before the screenshot was due (still covered by " ) + pBlocker + ")"
                                                                    : string( "assets were still loading when the screenshot was due (Assets.Pending never reached 0)" ) );
                }
                runner._assetSettledFrameCount = 0;
            }
            return hold;
        }

        /** @brief 요청한 스크린샷이 다 써질 때까지 기다립니다. 상한을 넘기면 실패로 적고 측정하지 않고 넘깁니다. */
        static AutomationStepHold holdExpectImage( AutomationStepContext& context, const AutomationStep& step, InputManager& input )
        {
            AutomationRunner& runner = asRunner( context );
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
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerMouseDelta, "MouseDelta", nullptr, &AutomationRunnerInternal::validateAxisPair, &AutomationRunnerEngineSteps::prepareMouseDelta,
                                   nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerWheel, "Wheel", nullptr, &AutomationRunnerInternal::validateAxisPair, &AutomationRunnerEngineSteps::prepareWheel, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerMousePosition, "MousePosition", nullptr, &AutomationRunnerInternal::validateMousePosition,
                                   &AutomationRunnerEngineSteps::prepareMousePosition, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerGamepadAxis, "GamepadAxis", nullptr, &AutomationRunnerInternal::validateGamepadAxis, &AutomationRunnerEngineSteps::prepareGamepadAxis,
                                   nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerText, "Text", nullptr, &AutomationRunnerInternal::validateText, &AutomationRunnerEngineSteps::prepareText, nullptr, false );
    SW_AUTOMATION_STEP( runnerVariable, "Variable", &AutomationRunnerEngineSteps::runVariable, &AutomationRunnerInternal::validateVariable, false );
    SW_AUTOMATION_STEP( runnerExpectVariable, "ExpectVariable", &AutomationRunnerEngineSteps::runExpectVariable, &AutomationRunnerInternal::validateExpectVariable, false );
    SW_AUTOMATION_STEP( runnerExpect, "Expect", &AutomationRunnerEngineSteps::runExpect, &AutomationRunnerInternal::validateExpect, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerWaitUntil, "WaitUntil", nullptr, &AutomationRunnerInternal::validateWaitUntil, &AutomationRunnerEngineSteps::prepareWaitUntil,
                                   &AutomationRunnerEngineSteps::holdWaitUntil, false );
    SW_AUTOMATION_STEP( runnerExpectVisited, "ExpectVisited", &AutomationRunnerEngineSteps::runExpectVisited, &AutomationRunnerInternal::validateExpectVisited, false );
    SW_AUTOMATION_STEP( runnerRecord, "Record", &AutomationRunnerEngineSteps::runRecord, &AutomationRunnerInternal::validateRecord, false );
    SW_AUTOMATION_STEP( runnerRemember, "Remember", &AutomationRunnerEngineSteps::runRemember, &AutomationRunnerInternal::validateRemember, false );
    SW_AUTOMATION_STEP( runnerExpectLog, "ExpectLog", &AutomationRunnerEngineSteps::runExpectLog, &AutomationRunnerInternal::validateExpectLog, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( runnerScreenshot, "Screenshot", &AutomationRunnerEngineSteps::runScreenshot, &AutomationRunnerInternal::validateScreenshot, nullptr,
                                   &AutomationRunnerEngineSteps::holdScreenshot, false );
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
        , _logCapture{}
        , _logPolicy{}
        , _reportWriter{}
        , _listFailure{}
        , _listUnconfirmed{}
        , _listUnhandledLog{}
        , _listAllowedLogID{}
        , _listPendingScreenshot{}
        , _listWatch{}
        , _listRemembered{}
        , _outputDirectory{}
        , _finishReason{}
        , _pObjectManagerOverride{ nullptr }
        , _pInputManager{ nullptr }
        , _exitDeadlineSeconds{ 0.0 }
        , _loadDeadlineSeconds{ 0.0 }
        , _nextStepIndex{ 0 }
        , _frameIndex{ 0 }
        , _waitFrameCount{ 0 }
        , _screenshotRequestedCount{ 0 }
        , _screenshotCompletedCount{ 0 }
        , _imageWaitFrameCount{ 0 }
        , _assetSettledFrameCount{ 0 }
        , _holdFrameCount{ 0 }
        , _clockHoldFrameCount{ 0 }
        , _clockHoldStartSeconds{ 0.0 }
        , _clockHolderName{}
        , _result{ AutomationOutcome::Running }
        , _bStarted{ SW_FALSE }
        , _bEnded{ SW_FALSE }
        , _bClockHeld{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    // 로그 리스너 해제는 `_logCapture` 의 소멸자가 맡는다.
    AutomationRunner::~AutomationRunner() = default;

    void AutomationRunner::startFromPath( string_view scenarioPath, string_view reportPath )
    {
        _reportWriter.setReportPath( reportPath );
        string error;
        if ( _scenario.loadFromPath( scenarioPath, error ) == false )
        {
            SW_LOG_ERROR( "[Scenario] could not load '%#': %#", string( scenarioPath ).c_str(), error.c_str() );
            finish( AutomationOutcome::LoadError, error );
            return;
        }
        _outputDirectory = "Saved/Automation/" + _scenario.getName();
        if ( skipIfStepsAreMissingFromThisBuild() || prepareScratchFolders() == false )
            return;
        SW_LOG_INFO( "[Scenario] %# loaded from %# (%# step(s), fixed delta %#)", _scenario.getName().c_str(), string( scenarioPath ).c_str(),
                     static_cast<uint32>( _scenario.getSteps().size() ), _scenario.getFixedDelta() );
        if ( _scenario.getInputMode() == VirtualInputMode::Mixed )
            SW_LOG_WARNING( "[Scenario] %# uses mixed input - do not touch the keyboard or mouse while it runs", _scenario.getName().c_str() );
    }

    bool AutomationRunner::startFromText( string_view xmlText, string_view reportPath )
    {
        _reportWriter.setReportPath( reportPath );
        string error;
        if ( _scenario.parse( xmlText, "<text>", error ) == false )
        {
            finish( AutomationOutcome::LoadError, error );
            return false;
        }
        _outputDirectory = "Saved/Automation/" + _scenario.getName();
        return skipIfStepsAreMissingFromThisBuild() == false && prepareScratchFolders();
    }

    bool AutomationRunner::prepareScratchFolders()
    {
        for ( const AutomationScratchFolder& folder : _scenario.getTestOptions()._listScratchFolder )
        {
            string error;
            if ( AutomationScratch::prepare( folder, error ) == false )
            {
                SW_LOG_ERROR( "[Scenario] %#: scratch folder %# could not be prepared: %#", _scenario.getName().c_str(), folder._scratchID.c_str(), error.c_str() );
                finish( AutomationOutcome::LoadError, error );
                return false;
            }
            SW_LOG_INFO( "[Scenario] %#: %# copied to %# (fresh for this run)", _scenario.getName().c_str(), folder._sourceID.c_str(), folder._scratchID.c_str() );
        }
        return true;
    }

    bool AutomationRunner::skipIfStepsAreMissingFromThisBuild()
    {
        const AutomationStep* pMissingStep = AutomationRunnerInternal::findStepMissingFromThisBuild( _scenario );
        if ( pMissingStep == nullptr )
            return false;
        finish( AutomationOutcome::Skipped, pMissingStep->describe() + ": dev commands are compiled out of this build" );
        return true;
    }

    GameObjectManager* AutomationRunner::findActiveObjectManager() const
    {
        if ( _pObjectManagerOverride != nullptr )
            return _pObjectManagerOverride;
        return engine::findActiveObjectManager();
    }

    void AutomationRunner::onFrameBegin( InputManager& input )
    {
        if ( _result != AutomationOutcome::Running )
            return;
        if ( _bStarted == SW_FALSE )
        {
            // 씬 플레이 중이란 활성 씬이 플레이를 시작했고 시작 차단 조건이 모두 풀렸다는 뜻이다(UI 의 로딩 화면은 게임 입력을 막으므로, 그동안 넣은 입력은 폰에 닿지 않는다).
            const GameObjectManager* pManager = findActiveObjectManager();
            const bool               bBlocked = AutomationStepRegistry::isStartBlocked();
            const bool               bReady   = _scenario.getStartCondition() == AutomationStartCondition::Immediately || ( pManager != nullptr && pManager->hasBegunPlay() && bBlocked == false );
            // 배타 시나리오는 시작 조건을 기다리는 프레임에도 사람의 장치를 버린다. 기다리는 동안 게임이 실제 마우스 위치를 읽으면(건설 커서 등)
            // 그 값이 게임 상태에 남아 같은 시나리오가 실행마다 다른 결과를 낸다. 시작할 때 다시 붙여 가상 입력 프레임을 0 부터 센다.
            const bool bBlockWhileWaiting = _scenario.getInputMode() == VirtualInputMode::Exclusive && input.getVirtualInput() != &_inputScript;
            if ( bBlockWhileWaiting )
            {
                input.attachVirtualInput( &_inputScript, VirtualInputMode::Exclusive );
                input.setOsDeviceInputBlocked( true );
            }
            // 전경이 필요한 시나리오는 첫 프레임부터 창을 전경으로 가져오고, 얻을 때까지 시작하지 않는다 — 게임이 시작하며 켜는 상태(1인칭 커서 잠금)가
            // 전경에서 켜지게. 시작 뒤의 단계로는 늦다(게임의 첫 틱이 먼저 돈다). 시작 시한까지 얻지 못하면 이 세션에서 볼 수 없는 시나리오라 건너뛴다.
            const bool bForegroundMissing = _scenario.getTestOptions()._bRequiresForeground && AutomationWindowUtil::bringToForeground() == false;
            // 앞 시나리오(체인)가 남긴 백그라운드 작업이 끝나야 시작한다 — 그 결과 줄이 이 시나리오 창에 떨어지지 않게. 시작 시한은 세지 않는다(벽시계 시한이 본다).
            if ( holdClockForBackgroundWork( input ) )
                return;
            if ( bReady == false || bForegroundMissing )
            {
                if ( ++_waitFrameCount <= _scenario.getStartTimeoutFrames() )
                    return;
                if ( bReady )
                    finish( AutomationOutcome::Skipped, "the window could not become the foreground window in this session" );
                else
                    finish( AutomationOutcome::TimedOut, "the start condition never became true" );
                return;
            }
            _pInputManager = &input;
            string error;
            if ( prepare( error ) == false )
            {
                finish( AutomationOutcome::LoadError, error );
                return;
            }
            _bStarted = SW_TRUE;
            input.attachVirtualInput( &_inputScript, _scenario.getInputMode() );
            // 배타 시나리오는 무인 실행이다. 사람이 같은 PC 에서 마우스를 움직여도 에디터 UI 까지 결과가 바뀌지 않게 장치 이벤트를 창 메시지 단계에서 버린다.
            input.setOsDeviceInputBlocked( _scenario.getInputMode() == VirtualInputMode::Exclusive );
            SW_LOG_INFO( "[Scenario] %# started after %# frame(s)", _scenario.getName().c_str(), _waitFrameCount );
            startLoadWindow();
        }
        else if ( holdClockForBackgroundWork( input ) )
        {
            return; // 이 프레임은 단계를 돌리지 않는다(`onFrameEnd` 도 `_bClockHeld` 를 본다)
        }
        _logCapture.setFrameIndex( _frameIndex );
        // 액션 계층 단계(레지스트리의 `_bBeforeInput`)는 그 프레임의 입력 재생과 게임 틱 전에 넣는다.
        const vector<AutomationStep>& listStep = _scenario.getSteps();
        for ( size_t stepIndex = _nextStepIndex; stepIndex < listStep.size() && listStep[stepIndex]._frameIndex == _frameIndex; ++stepIndex )
        {
            const AutomationStepRegistration* pRegistration    = AutomationStepRegistry::find( listStep[stepIndex]._kind );
            const bool                        bRunsBeforeInput = pRegistration != nullptr && pRegistration->_bBeforeInput && pRegistration->_pRun != nullptr;
            if ( bRunsBeforeInput && pRegistration->_pRun( *this, listStep[stepIndex] ) == false )
                finish( AutomationOutcome::LoadError, listStep[stepIndex].describe() + ": the step could not run" );
        }
    }

    AutomationOutcome AutomationRunner::onFrameEnd( InputManager& input )
    {
        const bool bRunsSteps = _result == AutomationOutcome::Running && _bStarted == SW_TRUE && _bClockHeld == SW_FALSE; // 시계 잡기 조건이 붙잡은 프레임은 단계를 돌리지 않는다
        if ( bRunsSteps )
        {
            sampleWatches();
            const vector<AutomationStep>& listStep = _scenario.getSteps();
            // `<=` 이므로 스크린샷을 기다리느라 밀린 단계는 다음 프레임에 이어서 돈다.
            while ( true )
            {
                const bool bNextStepDue = _nextStepIndex < listStep.size() && listStep[_nextStepIndex]._frameIndex <= _frameIndex && _result == AutomationOutcome::Running;
                if ( bNextStepDue == false )
                    break;
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
                const bool   bRunsNow        = pRegistration != nullptr && pRegistration->_bBeforeInput == false && pRegistration->_pRun != nullptr;
                const uint32 requestedBefore = _screenshotRequestedCount;
                if ( pRegistration == nullptr || ( bRunsNow && pRegistration->_pRun( *this, step ) == false ) )
                    finish( AutomationOutcome::LoadError, step.describe() + ": the step could not run" );
                // 사진은 다음 렌더 패킷이 찍는다 — 같은 프레임에 뒤 단계가 돌면 그 변화가 사진에 들어간다. 사진을 요청한 프레임은 여기서 닫고 뒤 단계는 다음 프레임에 돈다.
                if ( _screenshotRequestedCount != requestedBefore )
                    break;
            }
            const bool bExitDeadlineExceeded = _result == AutomationOutcome::Running && _exitDeadlineSeconds > 0.0 &&
                                               _exitDeadlineSeconds < static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
            if ( bExitDeadlineExceeded )
                finish( AutomationOutcome::Failed, "the window was asked to close but the loop kept running past ExpectExitWithin" );
            // 끝난 프레임은 세지 않는다. 보고의 프레임은 끝낸 단계의 프레임이다. `WaitUntil` 이 붙잡은 프레임도 세지 않는다(뒤 단계가 그만큼 밀린다).
            if ( _result == AutomationOutcome::Running && _holdFrameCount == 0 )
                ++_frameIndex;
            // 창 닫기를 기다리는 동안은 프레임 시한을 보지 않는다(벽시계 시한이 본다). 부하 시나리오는 측정 창이 끝내므로 벽시계 그물만 본다.
            const bool bFrameTimeoutReached = _result == AutomationOutcome::Running && _exitDeadlineSeconds <= 0.0 && _loadDeadlineSeconds <= 0.0 &&
                                              _frameIndex > _scenario.getTimeoutFrames();
            if ( bFrameTimeoutReached )
                finish( AutomationOutcome::TimedOut, "timeoutFrames reached without <Pass/>" );
            const bool bLoadDeadlineExceeded = _result == AutomationOutcome::Running && _loadDeadlineSeconds > 0.0 &&
                                               _loadDeadlineSeconds < static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
            if ( bLoadDeadlineExceeded )
                finish( AutomationOutcome::TimedOut, "the <Load> measurement window did not close in time (was -gv_profileFrames also given?)" );
        }
        if ( _result != AutomationOutcome::Running )
            endRun( &input );
        return _result;
    }

    void AutomationRunner::startLoadWindow()
    {
        const AutomationLoad& load = _scenario.getTestOptions()._load;
        if ( load._bDeclared == false )
            return;
        // 측정 창은 시나리오가 시작한 프레임부터다(로딩을 빼고 측정한다). 끝은 측정 보고가 낸 종료 요청이다(`FrameProfileSession` 이 늦게 건 목표를 읽는다).
        const int32 seconds = static_cast<int32>( MathUtil::ceil( load._durationSeconds ) );
        if ( engine::getSubsystem<GlobalVariableManager>().setValueFromString( "gv_profileSeconds", to_string( seconds ) ) == false )
        {
            finish( AutomationOutcome::LoadError, "<Load> could not set gv_profileSeconds (a build without the frame profiler?)" );
            return;
        }
        // 멈춤 그물 — 워밍업(60 프레임)과 느린 기계를 넉넉히 덮는다.
        constexpr float64 kLoadDeadlineSlackSeconds = 120.0;
        _loadDeadlineSeconds                        = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 + 2.0 * static_cast<float64>( seconds ) + kLoadDeadlineSlackSeconds;
        SW_LOG_INFO( "[Scenario] load window %# s (clients %#, meshes %#, movers %#, network '%#')", seconds, load._clientCount, load._meshCount, load._moverCount,
                     load._network.c_str() );
    }

    AutomationOutcome AutomationRunner::onWindowClosed( InputManager* pInput )
    {
        if ( _result == AutomationOutcome::Running )
        {
            if ( _exitDeadlineSeconds > 0.0 )
            {
                const float64 nowSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
                if ( nowSeconds <= _exitDeadlineSeconds )
                    finish( AutomationOutcome::Passed, "the window closed within ExpectExitWithin" );
                else
                    finish( AutomationOutcome::Failed, "the window closed after the ExpectExitWithin deadline" );
            }
            else if ( _loadDeadlineSeconds > 0.0 && _nextStepIndex >= _scenario.getSteps().size() )
            {
                // 부하 시나리오의 정상 끝 — 측정 창이 닫혀 엔진이 종료를 요청했다(보고가 남았는지는 실행기가 출력으로 본다).
                finish( AutomationOutcome::Passed, "the <Load> measurement window closed" );
            }
            else
            {
                finish( AutomationOutcome::Failed, "the window closed before the scenario ended" );
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

    void AutomationRunner::endScenario( AutomationStepEnd end, string_view reason )
    {
        // 단계의 말(Pass · Fail · Skip)을 종료 코드로 번역하는 곳은 여기 하나다. 새 끝 까닭이 늘면 스위치가 컴파일러 경고로 알린다.
        switch ( end )
        {
            case AutomationStepEnd::Pass:
            {
                finish( AutomationOutcome::Passed, reason );
                return;
            }
            case AutomationStepEnd::Fail:
            {
                finish( AutomationOutcome::Failed, reason );
                return;
            }
            case AutomationStepEnd::Skip:
            {
                finish( AutomationOutcome::Skipped, reason );
                return;
            }
        }
    }

    void AutomationRunner::finish( AutomationOutcome result, string_view reason )
    {
        if ( _result != AutomationOutcome::Running )
            return;
        _result       = ( result == AutomationOutcome::Passed && _listFailure.empty() == false ) ? AutomationOutcome::Failed : result;
        _finishReason = string( reason );
    }

    bool AutomationRunner::prepare( string& outError )
    {
        // 기동 때 정해지는 값(첫 씬 · 렌더 파이프라인)은 시나리오가 바꿀 수 없다 — 띄운 명령줄이 선언과 다르면 다른 화면을 단언하게 되므로 멈춘다.
        for ( const AutomationAttribute& variable : _scenario.getTestOptions()._listLaunchVariable )
        {
            const GlobalVariableInfo* pInfo           = engine::areEngineServicesBound() ? engine::getSubsystem<GlobalVariableManager>().findVariable( variable._name ) : nullptr;
            bool                      bMatched        = false;
            const bool                bLaunchedWithIt = pInfo != nullptr && AutomationRunnerInternal::compareVariable( *pInfo, string_view{ variable._value }, 0.0f, bMatched ) && bMatched;
            if ( bLaunchedWithIt == false )
            {
                outError = "<Launch> needs the command line -" + variable._name + "=" + variable._value + " (now " +
                           ( pInfo != nullptr ? "'" + pInfo->getValueAsString() + "'" : string( "no such variable" ) ) + ")";
                return false;
            }
        }
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
        if ( _logPolicy.loadAllowList( outError ) == false )
            return false; // 허용 목록이 틀리면 폴백 없이 읽기 오류(11)로 멈춘다
        _logPolicy.selectScenario( _scenario.getName() );
        for ( const AutomationStep& step : _scenario.getSteps() )
        {
            if ( step._kind != "ExpectLog" )
                continue;
            if ( const string* pID = step.findAttribute( "id" ); pID != nullptr )
                _logPolicy.declareExpectedID( *pID );
            else
                _logPolicy.declareExpected( *step.findAttribute( "contains" ) ); // 검사 함수가 id · contains 중 하나를 요구했다
        }
        prepareWatches();
        _listRemembered.clear();
        _logCapture.start();
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

    AutomationStepHold AutomationRunner::holdUntilReady( [[maybe_unused]] const AutomationStep& step, InputManager& input, bool bReady, uint32 timeoutFrames )
    {
        if ( bReady || _holdFrameCount >= timeoutFrames )
        {
            if ( bReady && _holdFrameCount > 0 )
                SW_LOG_INFO( "[Scenario] %# held the clock for %# frame(s)", step.describe().c_str(), _holdFrameCount );
            _holdFrameCount = 0;
            return AutomationStepHold::Run;
        }
        ++_holdFrameCount;
        // 이 프레임은 시나리오 프레임으로 세지 않는다. 다음 프레임에 낼 입력 이벤트부터 한 프레임씩 밀어 시나리오 프레임과 맞춘다.
        _inputScript.delayEventsFrom( input.getVirtualFrameIndex(), 1 );
        return AutomationStepHold::Hold;
    }

    bool AutomationRunner::holdClockForBackgroundWork( InputManager& input )
    {
        const utf8* pHolder = AutomationStepRegistry::findClockHolder();
        if ( pHolder == nullptr )
        {
            if ( _clockHoldFrameCount > 0 )
                SW_LOG_INFO( "[Scenario] %# held the clock for %# frame(s)", _clockHolderName.c_str(), _clockHoldFrameCount );
            _clockHoldFrameCount = 0;
            _bClockHeld          = SW_FALSE;
            return false;
        }
        const float64 nowSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6;
        if ( _clockHoldFrameCount == 0 )
        {
            _clockHoldStartSeconds = nowSeconds;
            _clockHolderName       = pHolder;
        }
        ++_clockHoldFrameCount;
        _bClockHeld = SW_TRUE;
        // 시작한 시나리오는 이 프레임을 세지 않는다. 이 프레임에 낼 입력 이벤트부터 한 프레임씩 밀어 시나리오 프레임과 맞춘다(`holdUntilReady` 와 같다).
        if ( _bStarted == SW_TRUE )
            _inputScript.delayEventsFrom( input.getVirtualFrameIndex(), 1 );
        if ( nowSeconds - _clockHoldStartSeconds > AutomationRunnerInternal::kClockHoldTimeoutSeconds )
            finish( AutomationOutcome::TimedOut, _clockHolderName + " held the scenario clock for more than " +
                                                     to_string( static_cast<uint32>( AutomationRunnerInternal::kClockHoldTimeoutSeconds ) ) + " s" );
        return true;
    }

    bool AutomationRunner::shouldHoldForCondition( const AutomationStep& step, InputManager& input )
    {
        bool                                      bMatched = false;
        string                                    message;
        const bool                                bHasValue = compareProbe( step, bMatched, message );
        const bool                                bReady    = bHasValue && bMatched;
        string                                    error;
        AutomationRunnerInternal::WaitTimeoutArgs timeout;
        if ( AutomationRunnerInternal::parseWaitTimeout( step, timeout, error ) == false )
        {
            recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드) — 기다리지 않고 실패로 적는다
            return false;
        }
        const uint32 timeoutFrames  = timeout._bSeconds ? convertSecondsToFrames( timeout._timeoutSeconds ) : timeout._timeoutFrames;
        const uint32 heldFrameCount = _holdFrameCount;
        if ( holdUntilReady( step, input, bReady, timeoutFrames ) == AutomationStepHold::Hold )
            return true;
        if ( bReady == false )
            recordFailure( step, "WaitUntil gave up after " + to_string( heldFrameCount ) + " frame(s): " + message );
        return false;
    }

    bool AutomationRunner::readProbeValue( const AutomationStep& step, AutomationProbeValue& outValue, string& outMessage ) const
    {
        const string&                      probeName     = *step.findAttribute( "probe" );
        const AutomationProbeRegistration* pRegistration = AutomationProbes::find( probeName );
        // 모듈을 언로드했다 로드하면 탐침이 빠질 수 있다. 시작 때 있었어도 다시 찾는다.
        if ( pRegistration == nullptr )
        {
            outMessage = probeName + " is no longer registered";
            return false;
        }
        const string*              pTarget = step.findAttribute( "target" );
        const string*              pKey    = step.findAttribute( "key" );
        const AutomationProbeQuery query   = AutomationProbeQuery::make( findActiveObjectManager(), pTarget != nullptr ? string_view{ *pTarget } : string_view{},
                                                                       pKey != nullptr ? string_view{ *pKey } : string_view{} );
          string subject = probeName;
        if ( pTarget != nullptr )
            subject += "(" + *pTarget + ( pKey != nullptr ? ", " + *pKey : string() ) + ")";
        else if ( pKey != nullptr )
            subject += "(" + *pKey + ")";
        if ( AutomationProbes::read( *pRegistration, query, outValue ) == false )
        {
            const string reason = outValue._bText && outValue._text.empty() == false ? outValue._text : string( "no target in the scene" );
            outMessage          = subject + " has no value (" + reason + ")";
            return false;
        }
        outMessage = subject;
        return true;
    }

    bool AutomationRunner::compareProbe( const AutomationStep& step, bool& outMatched, string& outMessage ) const
    {
        using Internal = AutomationRunnerInternal;
        AutomationProbeValue value{};
        outMatched = false;
        if ( readProbeValue( step, value, outMessage ) == false )
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
            if ( Internal::parseNumberComparison( step, Internal::kProbeNearTolerance, comparison, error ) == false )
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

    uint32 AutomationRunner::convertSecondsToFrames( float32 seconds ) const
    {
        return static_cast<uint32>( seconds / _scenario.getFixedDelta() + 0.5f );
    }

    void AutomationRunner::addRecordLine( string line )
    {
        SW_LOG_INFO( "[Scenario] record %#", line.c_str() );
        _reportWriter.addRecordLine( std::move( line ) );
    }

    void AutomationRunner::prepareWatches()
    {
        _listWatch.clear();
        for ( const AutomationStep& step : _scenario.getSteps() )
        {
            if ( string_view{ step._kind } != "ExpectVisited" )
                continue;
            const string* pTarget = step.findAttribute( "target" );
            const string* pKey    = step.findAttribute( "key" );
            ProbeWatch    watch{};
            watch._probe  = *step.findAttribute( "probe" );
            watch._target = pTarget != nullptr ? *pTarget : string();
            watch._key    = pKey != nullptr ? *pKey : string();
            bool bKnown   = false;
            for ( const ProbeWatch& existing : _listWatch )
            {
                bKnown = bKnown || ( existing._probe == watch._probe && existing._target == watch._target && existing._key == watch._key );
            }
            if ( bKnown == false )
                _listWatch.push_back( std::move( watch ) );
        }
    }

    void AutomationRunner::sampleWatches()
    {
        for ( ProbeWatch& watch : _listWatch )
        {
            AutomationStep step{};
            step._kind = "ExpectVisited";
            step._listAttribute.push_back( AutomationAttribute{ "probe", watch._probe } );
            if ( watch._target.empty() == false )
                step._listAttribute.push_back( AutomationAttribute{ "target", watch._target } );
            if ( watch._key.empty() == false )
                step._listAttribute.push_back( AutomationAttribute{ "key", watch._key } );
            AutomationProbeValue value{};
            string               message;
            if ( readProbeValue( step, value, message ) == false )
                continue;
            string text = AutomationRunnerInternal::makeValueText( value );
            if ( watch._listValue.empty() == false && watch._listValue.back() == text )
                continue;
            watch._listValue.push_back( std::move( text ) );
            watch._listFrame.push_back( _frameIndex );
        }
    }

    void AutomationRunner::runExpectVisited( const AutomationStep& step )
    {
        const string*     pTarget = step.findAttribute( "target" );
        const string*     pKey    = step.findAttribute( "key" );
        const string_view probe   = *step.findAttribute( "probe" );
        const string_view target  = pTarget != nullptr ? string_view{ *pTarget } : string_view{};
        const string_view key     = pKey != nullptr ? string_view{ *pKey } : string_view{};
        const ProbeWatch* pWatch  = nullptr;
        for ( const ProbeWatch& watch : _listWatch )
        {
            const bool bSame = watch._probe == probe && watch._target == target && watch._key == key;
            if ( bSame )
                pWatch = &watch;
        }
        string error;
        uint32 since = 0; // 적지 않으면 처음부터 본다
        if ( AutomationRunnerInternal::readUint( step, "since", since, error ) == false )
        {
            recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
            return;
        }
        const string*        pIs      = step.findAttribute( "is" );
        const vector<string> listWant = pIs != nullptr ? vector<string>{ *pIs } : AutomationRunnerInternal::splitList( *step.findAttribute( "sequence" ) );
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
        string subject = string( probe );
        if ( target.empty() == false || key.empty() == false )
            subject += "(" + string( target ) + ( target.empty() || key.empty() ? "" : ", " ) + string( key ) + ")";
        recordFailure( step, subject + " never went through '" + want + "' since frame " + to_string( since ) + " (missing '" + listWant[wantIndex] +
                                 "'), saw: " + ( seen.empty() ? string( "no value" ) : seen ) );
    }

    void AutomationRunner::runRecord( const AutomationStep& step )
    {
        AutomationProbeValue value{};
        string               message;
        if ( readProbeValue( step, value, message ) == false )
        {
            recordFailure( step, "Record " + message );
            return;
        }
        addRecordLine( "frame " + to_string( _frameIndex ) + " " + message + " = " + AutomationRunnerInternal::makeValueText( value ) );
    }

    void AutomationRunner::runRemember( const AutomationStep& step )
    {
        AutomationProbeValue value{};
        string               message;
        if ( readProbeValue( step, value, message ) == false )
        {
            recordFailure( step, "Remember " + message );
            return;
        }
        const string& name = *step.findAttribute( "as" );
        string        text = AutomationRunnerInternal::makeValueText( value );
        SW_LOG_INFO( "[Scenario] frame %# remember %# = %# (%#)", _frameIndex, name.c_str(), text.c_str(), message.c_str() );
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

    void AutomationRunner::runExpectLog( const AutomationStep& step )
    {
        const string*                           pID     = step.findAttribute( "id" );
        const string&                           needle  = pID != nullptr ? *pID : *step.findAttribute( "contains" ); // 검사 함수가 둘 중 하나를 요구했다
        const utf8*                             pTarget = pID != nullptr ? "ExpectLog id '" : "ExpectLog '";
        string                                  error;
        AutomationRunnerInternal::ExpectLogArgs args;
        if ( AutomationRunnerInternal::parseExpectLog( step, args, error ) == false )
        {
            recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
            return;
        }
        const uint32 matchCount = pID != nullptr ? _logCapture.countLinesWithID( needle, args._since ) : _logCapture.countMatchingLines( needle, args._since );
        if ( args._bExactCount )
        {
            if ( matchCount != args._expected )
                recordFailure( step, string( pTarget ) + needle + "' count == " + to_string( args._expected ) + ", got " + to_string( matchCount ) );
            return;
        }
        if ( matchCount < args._expected )
            recordFailure( step, string( pTarget ) + needle + "' count >= " + to_string( args._expected ) + ", got " + to_string( matchCount ) );
    }

    void AutomationRunner::runExpectImage( const AutomationStep& step )
    {
        using Internal = AutomationRunnerInternal;
        string                    error;
        Internal::ExpectImageArgs args;
        if ( Internal::parseExpectImage( step, args, error ) == false )
        {
            recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
            return;
        }
        const string* pRegion = step.findAttribute( "region" );

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
        if ( AutomationImageMetric::measure( image, args._region, args._metric, args._ratio, pReference != nullptr ? &reference : nullptr, value, error ) == false )
        {
            recordFailure( step, error );
            return;
        }
        // 값은 항상 적는다. 임계값을 정할 때 숫자를 보기 위해서다.
        string line = *step.findAttribute( "metric" ) + "(" + ( pRegion != nullptr ? *pRegion : string( "0,0,1,1" ) ) + ") " + *step.findAttribute( "file" ) + " = " +
                      Internal::formatNumber( value );
        SW_LOG_INFO( "[Scenario] metric %#", line.c_str() );
        _reportWriter.addMetricLine( std::move( line ) );

        string     rule;
        const bool bOk = Internal::matchesNumber( args._comparison, value, rule );
        if ( bOk == false )
        {
            recordFailure( step, "ExpectImage " + *step.findAttribute( "metric" ) + " " + rule + ", got " + Internal::formatNumber( value ) );
            return;
        }
        if ( args._basis._kind == AutomationThresholdBasisKind::Provisional )
        {
            // 어림값 문턱이 맞았다 — 통과로 세지 않는다. 위 지표 줄의 잰 값을 `measured` 로 옮기면 확정이다.
            string unconfirmed = step.describe() + ": " + *step.findAttribute( "metric" ) + " " + rule + " held with " + Internal::formatNumber( value ) +
                                 " on a provisional threshold (" + args._basis._text + ")";
            SW_LOG_INFO( "[Scenario] unconfirmed %#", unconfirmed.c_str() );
            _listUnconfirmed.push_back( std::move( unconfirmed ) );
        }
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

    void AutomationRunner::applyLogGate()
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
        for ( const string& line : _listUnhandledLog )
        {
            _listFailure.push_back( "unhandled log: " + line );
        }
        _result       = AutomationOutcome::UnhandledLog;
        _finishReason = to_string( _listUnhandledLog.size() ) + " unhandled Warning/Error log line(s), " + to_string( judgement._missingIDCount ) +
                        " without a log id - fix the cause, declare the line with <ExpectLog>, or add its log id with scope, reason and plan to " +
                        string( AutomationLogPolicy::kAllowListPath ) + " (a line without a log id needs SW_LOG_WARNING_ID / SW_LOG_ERROR_ID first)";
    }

    void AutomationRunner::endRun( InputManager* pInput )
    {
        if ( _bEnded == SW_TRUE )
            return;
        _bEnded = SW_TRUE;
        if ( pInput != nullptr && pInput->getVirtualInput() == &_inputScript )
            pInput->detachVirtualInput();
        _logCapture.stop();
        if ( _result == AutomationOutcome::Passed )
            applyLogGate();
        if ( _result == AutomationOutcome::Passed && _listUnconfirmed.empty() == false )
            _result = AutomationOutcome::Unconfirmed;
        if ( _result == AutomationOutcome::Passed )
        {
            SW_LOG_INFO( "[Scenario] PASS %# (%# frame(s))", _scenario.getName().c_str(), _frameIndex );
        }
        else if ( _result == AutomationOutcome::Unconfirmed )
        {
            SW_LOG_INFO( "[Scenario] UNCONFIRMED %# (%# frame(s)): every assertion held, but %# threshold(s) are provisional - move the measured values into measured=\"...\"",
                         _scenario.getName().c_str(), _frameIndex, static_cast<uint32>( _listUnconfirmed.size() ) );
        }
        else if ( _result == AutomationOutcome::Skipped )
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
            SW_LOG_ERROR( "[Scenario] %# %# at frame %#: %#", getAutomationOutcomeName( _result ), _scenario.getName().c_str(), _frameIndex, detail.c_str() );
        }
        AutomationReportParams report{};
        report._scenarioName      = _scenario.getName();
        report._pResultName       = getAutomationOutcomeName( _result );
        report._exitCode          = getAutomationExitCode( _result );
        report._frameCount        = _frameIndex;
        report._reason            = _finishReason;
        report._pListFailure      = &_listFailure;
        report._pListUnconfirmed  = &_listUnconfirmed;
        report._pListUnhandledLog = &_listUnhandledLog;
        report._pListAllowedLog   = &_listAllowedLogID;
        _reportWriter.write( report );
    }
} // namespace sw
