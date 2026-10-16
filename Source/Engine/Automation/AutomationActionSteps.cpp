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

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"

namespace sw
{
    namespace
    {
        struct AutomationActionStepsInternal
        {
            /** @brief 왼쪽 · 오른쪽 스틱의 X 축 번호입니다(Y 는 그다음 — `GamepadDevice::setAxis`). */
            static constexpr uint16 kLeftStickAxisX  = 0;
            static constexpr uint16 kRightStickAxisX = 2;

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

            /** @brief 있으면 음이 아닌 정수로 읽습니다(없으면 @p fallback). */
            [[nodiscard]] static bool readUint( const AutomationStep& step, string_view name, uint32 fallback, uint32& outValue, string& outError )
            {
                outValue             = fallback;
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr )
                    return true;
                int32 value = 0;
                if ( StringUtil::parseInt( StringUtil::trim( string_view{ *pValue } ), value ) && value >= 0 )
                {
                    outValue = static_cast<uint32>( value );
                    return true;
                }
                outError = step.describe() + ": " + string( name ) + " must be a non-negative integer, got '" + *pValue + "'";
                return false;
            }

            /** @brief 있으면 수로 읽습니다(없으면 @p fallback). */
            [[nodiscard]] static bool readFloat( const AutomationStep& step, string_view name, float32 fallback, float32& outValue, string& outError )
            {
                outValue             = fallback;
                const string* pValue = step.findAttribute( name );
                if ( pValue == nullptr || StringUtil::parseFloat( StringUtil::trim( string_view{ *pValue } ), outValue ) )
                    return true;
                outError = step.describe() + ": " + string( name ) + " must be a number, got '" + *pValue + "'";
                return false;
            }

            /**
             * @brief 프레임 길이를 읽습니다 — @p pFramesName(프레임) 또는 @p pSecondsName(초, 시나리오 고정 프레임 시간으로 바꾼다) 하나, 없으면 @p fallback 입니다.
             * @details 검사 때는 실행기가 없어(@p pRunner 가 nullptr) 형식만 본다.
             */
            [[nodiscard]] static bool readDuration( const AutomationRunner* pRunner, const AutomationStep& step, const utf8* pFramesName, const utf8* pSecondsName, uint32 fallback,
                                                    uint32& outFrames, string& outError )
            {
                const bool bFrames  = step.findAttribute( pFramesName ) != nullptr;
                const bool bSeconds = step.findAttribute( pSecondsName ) != nullptr;
                if ( bFrames && bSeconds )
                {
                    outError = step.describe() + ": give " + pFramesName + " or " + pSecondsName + ", not both";
                    return false;
                }
                float32 seconds = 0.0f;
                if ( readUint( step, pFramesName, fallback, outFrames, outError ) == false || readFloat( step, pSecondsName, 0.0f, seconds, outError ) == false )
                    return false;
                if ( seconds < 0.0f )
                {
                    outError = step.describe() + ": " + pSecondsName + " must not be negative";
                    return false;
                }
                if ( bSeconds && pRunner != nullptr )
                    outFrames = pRunner->convertSecondsToFrames( seconds );
                return true;
            }

            /** @brief `device` 속성이 장치 종류 하나(keyboard · mouse · gamepad)인지 봅니다. 없으면 아무 장치나 받습니다. */
            [[nodiscard]] static bool readDevice( const AutomationStep& step, bool& outbAny, InputDeviceKind& outKind, string& outError )
            {
                outbAny               = true;
                outKind               = InputDeviceKind::Keyboard;
                const string* pDevice = step.findAttribute( "device" );
                if ( pDevice == nullptr )
                    return true;
                outbAny                = false;
                const string_view name = *pDevice;
                if ( name == "keyboard" )
                    outKind = InputDeviceKind::Keyboard;
                else if ( name == "mouse" )
                    outKind = InputDeviceKind::Mouse;
                else if ( name == "gamepad" )
                    outKind = InputDeviceKind::Gamepad;
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

            static bool validateAction( const AutomationStep& step, string& outError )
            {
                uint32          frames = 0;
                bool            bAny   = true;
                InputDeviceKind kind   = InputDeviceKind::Keyboard;
                if ( checkAttributeNames( step, { "name", "hold", "seconds", "count", "interval", "intervalSeconds", "device" }, outError ) == false || validateName( step, outError ) == false )
                    return false;
                return readDuration( nullptr, step, "hold", "seconds", 1, frames, outError ) && readUint( step, "count", 1, frames, outError ) &&
                       readDuration( nullptr, step, "interval", "intervalSeconds", 0, frames, outError ) && readDevice( step, bAny, kind, outError );
            }

            static bool validatePressOrRelease( const AutomationStep& step, string& outError )
            {
                bool            bAny = true;
                InputDeviceKind kind = InputDeviceKind::Keyboard;
                return checkAttributeNames( step, { "name", "device" }, outError ) && validateName( step, outError ) && readDevice( step, bAny, kind, outError );
            }

            static bool validateAxis( const AutomationStep& step, string& outError )
            {
                uint32          frames = 0;
                float32         value  = 0.0f;
                bool            bAny   = true;
                InputDeviceKind kind   = InputDeviceKind::Keyboard;
                if ( checkAttributeNames( step, { "name", "x", "y", "frames", "seconds", "device" }, outError ) == false || validateName( step, outError ) == false )
                    return false;
                return readFloat( step, "x", 0.0f, value, outError ) && readFloat( step, "y", 0.0f, value, outError ) &&
                       readDuration( nullptr, step, "frames", "seconds", 1, frames, outError ) && readDevice( step, bAny, kind, outError );
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
             * @brief 액션의 바인딩 중 @p pAccept 가 받는 첫 것을 찾습니다(바인딩 순서 — 입력 맵 파일의 순서). 액션이 없거나 받을 바인딩이 없으면 이유와 nullptr 입니다.
             * @details 바인딩은 시작 프레임의 맵에서 찾는다. 게임이 맵을 다시 읽어도 단계가 이미 낸 이벤트는 그대로다.
             */
            static const ActionBinding* findBinding( AutomationRunner& runner, const AutomationStep& step, bool ( *pAccept )( const ActionBinding& ), const utf8* pWhat,
                                                     string& outError )
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
                bool            bAny = true;
                InputDeviceKind kind = InputDeviceKind::Keyboard;
                (void)readDevice( step, bAny, kind, outError ); // 검사 함수가 이미 확인했다
                const uint32 bindingCount = inputMap.getBindingCount( action );
                for ( uint32 bindIndex = 0; bindIndex < bindingCount; ++bindIndex )
                {
                    const ActionBinding* pBinding = inputMap.getBinding( action, bindIndex );
                    const bool           bUsable  = pBinding != nullptr && pAccept( *pBinding ) && ( bAny || getBindingDevice( *pBinding ) == kind );
                    if ( bUsable )
                        return pBinding;
                }
                outError = step.describe() + ": action '" + name + "' has no " + pWhat + ( bAny ? string() : " on " + *step.findAttribute( "device" ) ) + " binding";
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

            static bool prepareAction( AutomationRunner& runner, const AutomationStep& step, string& outError )
            {
                const ActionBinding* pBinding = findBinding( runner, step, &isButtonBinding, "single button", outError );
                if ( pBinding == nullptr )
                    return false;
                uint32     holdFrames     = 1;
                uint32     count          = 1;
                uint32     intervalFrames = 0;
                const bool bRead          = readDuration( &runner, step, "hold", "seconds", 1, holdFrames, outError ) && readUint( step, "count", 1, count, outError ) &&
                                   readDuration( &runner, step, "interval", "intervalSeconds", holdFrames + 1, intervalFrames, outError );
                if ( bRead == false )
                    return false;
                if ( count > 1 && intervalFrames <= holdFrames )
                {
                    outError = step.describe() + ": interval (" + to_string( intervalFrames ) + " frames) must be longer than hold (" + to_string( holdFrames ) + ") to press again";
                    return false;
                }
                connectGamepadOf( runner.getInputScript(), step._frameIndex, pBinding->_arrSlot[0] );
                for ( uint32 pressIndex = 0; pressIndex < count; ++pressIndex )
                {
                    if ( runner.getInputScript().addTap( step._frameIndex + pressIndex * intervalFrames, pBinding->_arrSlot[0], holdFrames ) == false )
                    {
                        outError = step.describe() + ": the slot " + InputSlotUtil::toText( pBinding->_arrSlot[0] ) + " cannot be pressed";
                        return false;
                    }
                }
                return true;
            }

            static bool preparePressOrRelease( AutomationRunner& runner, const AutomationStep& step, string& outError )
            {
                const ActionBinding* pBinding = findBinding( runner, step, &isButtonBinding, "single button", outError );
                if ( pBinding == nullptr )
                    return false;
                connectGamepadOf( runner.getInputScript(), step._frameIndex, pBinding->_arrSlot[0] );
                if ( runner.getInputScript().addSlot( step._frameIndex, pBinding->_arrSlot[0], string_view{ step._kind } == "ActionPress" ) )
                    return true;
                outError = step.describe() + ": the slot " + InputSlotUtil::toText( pBinding->_arrSlot[0] ) + " cannot be pressed";
                return false;
            }

            /**
             * @brief 패드를 가상 연결합니다. 가상 입력 세션의 패드는 끊긴 채 시작하고(`GamepadDevice::setVirtualSession`) 끊긴 패드의 스틱은 입력 맵이 읽지 않는다.
             * @details 같은 연결 이벤트를 여러 번 내도 상태만 다시 정할 뿐이다.
             */
            static void connectGamepad( VirtualInputScript& script, uint32 frameIndex, uint8 padIndex )
            {
                script.addEvent( frameIndex, RawInputEvent::makeGamepadConnection( padIndex, true ) );
            }

            /** @brief 슬롯이 패드 단추면 그 패드를 먼저 연결합니다. */
            static void connectGamepadOf( VirtualInputScript& script, uint32 frameIndex, const InputSlot& slot )
            {
                if ( slot._deviceKind == InputDeviceKind::Gamepad )
                    connectGamepad( script, frameIndex, slot._deviceIndex );
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
            static bool prepareAxis( AutomationRunner& runner, const AutomationStep& step, string& outError )
            {
                const ActionBinding* pBinding = findBinding( runner, step, &isAxisBinding, "axis", outError );
                if ( pBinding == nullptr )
                    return false;
                float32    valueX = 0.0f;
                float32    valueY = 0.0f;
                uint32     frames = 1;
                const bool bRead  = readFloat( step, "x", 0.0f, valueX, outError ) && readFloat( step, "y", 0.0f, valueY, outError ) &&
                                   readDuration( &runner, step, "frames", "seconds", 1, frames, outError );
                if ( bRead == false )
                    return false;
                VirtualInputScript& script = runner.getInputScript();
                const uint32        first  = step._frameIndex;
                bool                bOk    = true;
                switch ( pBinding->_kind )
                {
                    case BindingKind::GamepadStick2D:
                    {
                        const uint16 axisX = pBinding->_stick == GamepadStick::Left ? kLeftStickAxisX : kRightStickAxisX;
                        connectGamepad( script, first, pBinding->_deviceIndex );
                        script.addGamepadAxis( first, axisX, valueX, pBinding->_deviceIndex );
                        script.addGamepadAxis( first, static_cast<uint16>( axisX + 1 ), valueY, pBinding->_deviceIndex );
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
