/**
 * @file AutomationActionSteps.cpp
 * @brief 자동화 시나리오의 입력 맵 액션 단계(`Action`, `ActionPress`, `ActionRelease`, `ActionAxis`)를 정의합니다. 단계는 이 .cpp 의 정적 등록자가 등록합니다(Shipping 은 Engine 을 통째로 링크한다 — `sw_linkWholeArchive`).
 * @details 액션 이름을 시작 프레임의 통합 입력 맵(`InputManager::getInputMap`)에서 바인딩으로 풀어 그 키 · 버튼 · 스틱을 가상 입력으로 낸다.
 *          그래서 트리거(Hold · DoubleTap · Tap), 레이어, UI 입력 소비가 사람의 입력과 같은 길을 지난다. 키 배치를 바꿔도 시나리오는 그대로다.
 *          언리얼 Enhanced Input 의 `InjectInputForAction` 이 값을 바로 넣는 것과 달리 장치 층에서 내므로 트리거 결함까지 잡는다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"
#include "Engine/Scenario/AutomationScenario.h"
#include "Engine/Scenario/AutomationStepContext.h"
#include "Engine/Scenario/AutomationStepRegistry.h"

namespace sw
{
    namespace
    {
        struct AutomationActionStepsInternal
        {
            /** @brief 왼쪽 · 오른쪽 스틱의 X 축 번호입니다(Y 는 그다음 — `GamepadDevice::setAxis`). */
            static constexpr uint16 kLeftStickAxisX  = 0;
            static constexpr uint16 kRightStickAxisX = 2;

            /** @brief 누르는 길이 · 축 값을 내는 길이를 적지 않았을 때의 프레임 수입니다. */
            static constexpr uint32 kDefaultHoldFrames = 1;
            /** @brief 누르는 횟수를 적지 않았을 때입니다. */
            static constexpr uint32 kDefaultPressCount = 1;

            /** @brief `device` 속성이 고른 장치입니다. 없으면 아무 장치나 받습니다. */
            struct DeviceFilter
            {
                bool            _bAny{ true };                      ///< 장치를 고르지 않았다
                InputDeviceKind _kind{ InputDeviceKind::Keyboard }; ///< `_bAny` 가 거짓일 때 고른 장치
            };

            /** @brief `Action` 단계의 인자입니다. 검사와 실행이 같은 `parseAction` 으로 만든다. */
            struct ActionArgs
            {
                uint32       _holdFrames{ kDefaultHoldFrames };
                uint32       _count{ kDefaultPressCount };
                uint32       _intervalFrames{ 0 }; ///< 적지 않으면 누르는 길이 + 1(떼고 한 프레임 뒤 다시 누른다)
                DeviceFilter _device{};
            };

            /** @brief `ActionAxis` 단계의 인자입니다. 적지 않은 축은 0 입니다. */
            struct AxisArgs
            {
                float32      _valueX{ 0.0f };
                float32      _valueY{ 0.0f };
                uint32       _frames{ kDefaultHoldFrames };
                DeviceFilter _device{};
            };

            /** @brief 있으면 음이 아닌 정수로 읽습니다. 없으면 @p inoutValue 를 그대로 둡니다(형식의 초기값). */
            [[nodiscard]] static bool readUint( const AutomationStep& step, string_view name, uint32& inoutValue, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    return true;
                int32 value = 0;
                if ( StringUtil::parseInt( StringUtil::trim( string_view{ *pValue } ), value ) && value >= 0 )
                {
                    inoutValue = static_cast<uint32>( value );
                    return true;
                }
                outError = step.describe() + ": " + string( name ) + " must be a non-negative integer, got '" + *pValue + "'";
                return false;
            }

            /** @brief 있으면 수로 읽습니다. 없으면 @p inoutValue 를 그대로 둡니다. */
            [[nodiscard]] static bool readFloat( const AutomationStep& step, string_view name, float32& inoutValue, string& outError )
            {
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr || StringUtil::parseFloat( StringUtil::trim( string_view{ *pValue } ), inoutValue ) )
                    return true;
                outError = step.describe() + ": " + string( name ) + " must be a number, got '" + *pValue + "'";
                return false;
            }

            /**
             * @brief 프레임 길이를 읽습니다 — @p pFramesName(프레임) 또는 @p pSecondsName(초, 시나리오 고정 프레임 시간으로 바꾼다) 하나, 없으면 @p inoutFrames 를 그대로 둡니다.
             * @details 검사 때는 실행기가 없어(@p pRunner 가 nullptr) 형식만 본다.
             */
            [[nodiscard]] static bool readDuration( const AutomationStepContext* pRunner, const AutomationStep& step, const utf8* pFramesName, const utf8* pSecondsName, uint32& inoutFrames,
                                                    string& outError )
            {
                const bool bFrames  = step.findAttribute( pFramesName ) != nullptr;
                const bool bSeconds = step.findAttribute( pSecondsName ) != nullptr;
                if ( bFrames && bSeconds )
                {
                    outError = step.describe() + ": give " + pFramesName + " or " + pSecondsName + ", not both";
                    return false;
                }
                float32 seconds = 0.0f;
                if ( readUint( step, pFramesName, inoutFrames, outError ) == false || readFloat( step, pSecondsName, seconds, outError ) == false )
                    return false;
                if ( seconds < 0.0f )
                {
                    outError = step.describe() + ": " + pSecondsName + " must not be negative";
                    return false;
                }
                if ( bSeconds && pRunner != nullptr )
                    inoutFrames = pRunner->convertSecondsToFrames( seconds );
                return true;
            }

            /** @brief `device` 속성이 장치 종류 하나(keyboard · mouse · gamepad)인지 봅니다. 없으면 아무 장치나 받습니다. */
            [[nodiscard]] static bool readDevice( const AutomationStep& step, DeviceFilter& outDevice, string& outError )
            {
                outDevice             = DeviceFilter{};
                const string* pDevice = step.findAttribute( "device" );
                if ( pDevice == nullptr )
                    return true;
                outDevice._bAny        = false;
                const string_view name = *pDevice;
                if ( name == "keyboard" )
                    outDevice._kind = InputDeviceKind::Keyboard;
                else if ( name == "mouse" )
                    outDevice._kind = InputDeviceKind::Mouse;
                else if ( name == "gamepad" )
                    outDevice._kind = InputDeviceKind::Gamepad;
                else
                {
                    outError = step.describe() + ": unknown device '" + *pDevice + "' (keyboard · mouse · gamepad)";
                    return false;
                }
                return true;
            }

            static bool validateName( const AutomationStep& step, string& outError )
            {
                if ( step.findAttribute( "name" ) != nullptr )
                    return true;
                outError = step.describe() + ": needs name=\"<input map action>\"";
                return false;
            }

            /** @brief `Action` 단계의 인자를 읽습니다. 검사(@p pRunner 가 nullptr — 초를 프레임으로 바꾸지 않는다)와 실행이 이 함수 하나를 쓴다. */
            [[nodiscard]] static bool parseAction( const AutomationStepContext* pRunner, const AutomationStep& step, ActionArgs& outArgs, string& outError )
            {
                outArgs = ActionArgs{};
                if ( readDuration( pRunner, step, "hold", "seconds", outArgs._holdFrames, outError ) == false || readUint( step, "count", outArgs._count, outError ) == false )
                    return false;
                outArgs._intervalFrames = outArgs._holdFrames + 1;
                return readDuration( pRunner, step, "interval", "intervalSeconds", outArgs._intervalFrames, outError ) && readDevice( step, outArgs._device, outError );
            }

            /** @brief `ActionAxis` 단계의 인자를 읽습니다(검사와 실행이 함께 쓴다). */
            [[nodiscard]] static bool parseAxis( const AutomationStepContext* pRunner, const AutomationStep& step, AxisArgs& outArgs, string& outError )
            {
                outArgs = AxisArgs{};
                return readFloat( step, "x", outArgs._valueX, outError ) && readFloat( step, "y", outArgs._valueY, outError ) &&
                       readDuration( pRunner, step, "frames", "seconds", outArgs._frames, outError ) && readDevice( step, outArgs._device, outError );
            }

            static bool validateAction( const AutomationStep& step, string& outError )
            {
                ActionArgs args{};
                if ( step.hasOnlyAttributes( { "name", "hold", "seconds", "count", "interval", "intervalSeconds", "device" }, outError ) == false || validateName( step, outError ) == false )
                    return false;
                return parseAction( nullptr, step, args, outError );
            }

            static bool validatePressOrRelease( const AutomationStep& step, string& outError )
            {
                DeviceFilter device{};
                return step.hasOnlyAttributes( { "name", "device" }, outError ) && validateName( step, outError ) && readDevice( step, device, outError );
            }

            static bool validateAxis( const AutomationStep& step, string& outError )
            {
                AxisArgs args{};
                if ( step.hasOnlyAttributes( { "name", "x", "y", "frames", "seconds", "device" }, outError ) == false || validateName( step, outError ) == false )
                    return false;
                return parseAxis( nullptr, step, args, outError );
            }

            /** @brief 바인딩이 쓰는 장치 종류입니다. */
            static InputDeviceKind getBindingDevice( const ActionBinding& binding )
            {
                switch ( binding._kind )
                {
                    case BindingKind::GamepadStick2D:
                        return InputDeviceKind::Gamepad;
                    case BindingKind::MouseDelta2D:
                    case BindingKind::MouseWheel1D:
                    case BindingKind::VirtualJoystick2D:
                        return InputDeviceKind::Mouse;
                    default:
                        return binding._arrSlot[0]._deviceKind;
                }
            }

            /**
             * @brief 액션의 바인딩 중 @p pAccept 가 받고 @p device 에 맞는 첫 것을 찾습니다(바인딩 순서 — 입력 맵 파일의 순서). 액션이 없거나 받을 바인딩이 없으면 이유와 nullptr 입니다.
             * @details 바인딩은 시작 프레임의 맵에서 찾는다. 게임이 맵을 다시 읽어도 단계가 이미 낸 이벤트는 그대로다.
             */
            static const ActionBinding* findBinding( AutomationStepContext& runner, const AutomationStep& step, const DeviceFilter& device, bool ( *pAccept )( const ActionBinding& ),
                                                     const utf8* pWhat, string& outError )
            {
                const InputManager* pInput = runner.getInputManager();
                const string&       name   = *step.findAttribute( "name" );
                if ( pInput == nullptr )
                {
                    outError = step.describe() + ": there is no input manager";
                    return nullptr;
                }
                const InputMap&     inputMap = pInput->getInputMap();
                const hashed_string action( name );
                if ( inputMap.hasAction( action ) == false )
                {
                    outError = step.describe() + ": the input map has no action '" + name + "'";
                    return nullptr;
                }
                const uint32 bindingCount = inputMap.getBindingCount( action );
                for ( uint32 bindIndex = 0; bindIndex < bindingCount; ++bindIndex )
                {
                    const ActionBinding* pBinding = inputMap.getBinding( action, bindIndex );
                    const bool           bUsable  = pBinding != nullptr && pAccept( *pBinding ) && ( device._bAny || getBindingDevice( *pBinding ) == device._kind );
                    if ( bUsable )
                        return pBinding;
                }
                outError = step.describe() + ": action '" + name + "' has no " + pWhat + ( device._bAny ? string() : " on " + *step.findAttribute( "device" ) ) + " binding";
                return nullptr;
            }

            /** @brief 누를 수 있는 슬롯 하나(키 · 마우스 단추 · 패드 단추)인 바인딩입니다. */
            static bool isButtonBinding( const ActionBinding& binding ) { return binding._kind == BindingKind::SingleSlot; }

            /** @brief 축 값을 낼 수 있는 바인딩(합성 축 · 스틱 · 마우스 이동)입니다. */
            static bool isAxisBinding( const ActionBinding& binding )
            {
                const bool bComposite = binding._kind == BindingKind::Axis1DComposite || binding._kind == BindingKind::Vector2DComposite;
                return bComposite || binding._kind == BindingKind::GamepadStick2D || binding._kind == BindingKind::MouseDelta2D;
            }

            [[nodiscard]] static bool prepareAction( AutomationStepContext& runner, const AutomationStep& step, string& outError )
            {
                ActionArgs args{};
                if ( parseAction( &runner, step, args, outError ) == false )
                    return false;
                const ActionBinding* pBinding = findBinding( runner, step, args._device, &isButtonBinding, "single button", outError );
                if ( pBinding == nullptr )
                    return false;
                if ( args._count > 1 && args._intervalFrames <= args._holdFrames )
                {
                    outError = step.describe() + ": interval (" + to_string( args._intervalFrames ) + " frames) must be longer than hold (" + to_string( args._holdFrames ) +
                               ") to press again";
                    return false;
                }
                connectGamepadOf( runner.getInputScript(), step._frameIndex, pBinding->_arrSlot[0] );
                for ( uint32 pressIndex = 0; pressIndex < args._count; ++pressIndex )
                {
                    if ( runner.getInputScript().addTap( step._frameIndex + pressIndex * args._intervalFrames, pBinding->_arrSlot[0], args._holdFrames ) == false )
                    {
                        outError = step.describe() + ": the slot " + InputSlotUtil::toText( pBinding->_arrSlot[0] ) + " cannot be pressed";
                        return false;
                    }
                }
                return true;
            }

            [[nodiscard]] static bool preparePressOrRelease( AutomationStepContext& runner, const AutomationStep& step, string& outError )
            {
                DeviceFilter device{};
                if ( readDevice( step, device, outError ) == false )
                    return false;
                const ActionBinding* pBinding = findBinding( runner, step, device, &isButtonBinding, "single button", outError );
                if ( pBinding == nullptr )
                    return false;
                connectGamepadOf( runner.getInputScript(), step._frameIndex, pBinding->_arrSlot[0] );
                if ( runner.getInputScript().addSlot( step._frameIndex, pBinding->_arrSlot[0], string_view{ step._kind } == "ActionPress" ) )
                    return true;
                outError = step.describe() + ": the slot " + InputSlotUtil::toText( pBinding->_arrSlot[0] ) + " cannot be pressed";
                return false;
            }

            /** @brief 슬롯이 패드 단추면 그 패드를 먼저 연결합니다(`VirtualInputScript::addGamepadConnection`). */
            static void connectGamepadOf( VirtualInputScript& script, uint32 frameIndex, const InputSlot& slot )
            {
                if ( slot._deviceKind == InputDeviceKind::Gamepad )
                    script.addGamepadConnection( frameIndex, slot._deviceIndex );
            }

            /** @brief 디지털 슬롯 하나를 @p frames 동안 누릅니다. */
            [[nodiscard]] static bool holdSlot( VirtualInputScript& script, uint32 frameIndex, const InputSlot& slot, uint32 frames )
            {
                return script.addSlot( frameIndex, slot, true ) && script.addSlot( frameIndex + frames, slot, false );
            }

            /**
             * @brief 축 값을 @p frames 동안 냅니다. 스틱은 그 값 그대로, 합성 축은 부호의 방향 키(크기는 1), 마우스 이동은 프레임마다 그 픽셀만큼입니다.
             * @details 합성 축 값의 크기가 1 이 아니어도 키는 켜고 끄는 것뿐이다 — 아날로그 크기가 필요하면 `device="gamepad"` 로 스틱을 고른다.
             */
            [[nodiscard]] static bool prepareAxis( AutomationStepContext& runner, const AutomationStep& step, string& outError )
            {
                AxisArgs args{};
                if ( parseAxis( &runner, step, args, outError ) == false )
                    return false;
                const ActionBinding* pBinding = findBinding( runner, step, args._device, &isAxisBinding, "axis", outError );
                if ( pBinding == nullptr )
                    return false;
                const float32       valueX = args._valueX;
                const float32       valueY = args._valueY;
                const uint32        frames = args._frames;
                VirtualInputScript& script = runner.getInputScript();
                const uint32        first  = step._frameIndex;
                bool                bOk    = true;
                switch ( pBinding->_kind )
                {
                    case BindingKind::GamepadStick2D:
                    {
                        const uint16 axisX = pBinding->_stick == GamepadStick::Left ? kLeftStickAxisX : kRightStickAxisX;
                        // 한 성분 바인딩(`axis="Y"`, 1D 액션)은 x 값이 그 성분이다 — 액션 값과 같은 규칙(InputMapEvaluate).
                        const bool    bYOnly = pBinding->_stickAxis == GamepadStickAxis::Y;
                        const bool    bXOnly = pBinding->_stickAxis == GamepadStickAxis::X;
                        const float32 stickX = bYOnly ? 0.0f : valueX;
                        const float32 stickY = bYOnly ? valueX : ( bXOnly ? 0.0f : valueY );
                        script.addGamepadConnection( first, pBinding->_deviceIndex );
                        script.addGamepadAxis( first, axisX, stickX, pBinding->_deviceIndex );
                        script.addGamepadAxis( first, static_cast<uint16>( axisX + 1 ), stickY, pBinding->_deviceIndex );
                        script.addGamepadAxis( first + frames, axisX, 0.0f, pBinding->_deviceIndex );
                        script.addGamepadAxis( first + frames, static_cast<uint16>( axisX + 1 ), 0.0f, pBinding->_deviceIndex );
                        break;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        // 슬롯: 0 위 · 1 아래 · 2 왼쪽 · 3 오른쪽
                        if ( valueY > 0.0f )
                            bOk = bOk && holdSlot( script, first, pBinding->_arrSlot[0], frames );
                        if ( valueY < 0.0f )
                            bOk = bOk && holdSlot( script, first, pBinding->_arrSlot[1], frames );
                        if ( valueX < 0.0f )
                            bOk = bOk && holdSlot( script, first, pBinding->_arrSlot[2], frames );
                        if ( valueX > 0.0f )
                            bOk = bOk && holdSlot( script, first, pBinding->_arrSlot[3], frames );
                        break;
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        // 슬롯: 0 음 · 1 양
                        if ( valueX != 0.0f )
                            bOk = holdSlot( script, first, pBinding->_arrSlot[valueX < 0.0f ? 0 : 1], frames );
                        break;
                    }
                    default:
                    {
                        for ( uint32 frame = 0; frame < frames; ++frame )
                        {
                            script.addMouseDelta( first + frame, valueX, valueY );
                        }
                        break;
                    }
                }
                if ( bOk == false )
                    outError = step.describe() + ": a key of the composite binding cannot be pressed";
                return bOk;
            }
        };
    } // namespace

    // 액션 단계는 입력 단계처럼 시작할 때 가상 입력으로 옮겨진다(프레임에 할 일이 없다).
    SW_AUTOMATION_STEP_WITH_HOOKS( actionTap, "Action", nullptr, &AutomationActionStepsInternal::validateAction, &AutomationActionStepsInternal::prepareAction, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( actionPress, "ActionPress", nullptr, &AutomationActionStepsInternal::validatePressOrRelease, &AutomationActionStepsInternal::preparePressOrRelease,
                                   nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( actionRelease, "ActionRelease", nullptr, &AutomationActionStepsInternal::validatePressOrRelease,
                                   &AutomationActionStepsInternal::preparePressOrRelease, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( actionAxis, "ActionAxis", nullptr, &AutomationActionStepsInternal::validateAxis, &AutomationActionStepsInternal::prepareAxis, nullptr, false );
} // namespace sw
