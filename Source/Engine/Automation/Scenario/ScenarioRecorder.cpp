#include "pch.h"

#include "Engine/Automation/Scenario/ScenarioRecorder.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/XML/XMLWriter.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    namespace
    {
        struct ScenarioRecorderInternal
        {
            static constexpr string_view kOutputFolder   = kAutomationOutputFolder; // 뒤에 '/' 를 붙여 비교한다
            static constexpr string_view kScenarioSuffix = ".scenario.xml";
            static constexpr uint32      kTailFrameCount = 10; ///< 마지막 입력 뒤 `Pass` 까지의 프레임입니다.
            static constexpr uint32      kTimeoutMargin  = 60;

            /** @brief 단계 값의 소수 자리입니다. */
            static constexpr int32 kValueDecimals = 4;
            /** @brief 고정 프레임 시간의 소수 자리입니다. 넷째 자리에서 자르면(1/60 → 0.0167) 재생이 프레임마다 0.2 % 더 흘러 위치 검사점이 어긋난다. */
            static constexpr int32 kFixedDeltaDecimals = 7;

            /** @brief 실수 속성 하나를 소수 @p decimals 자리까지 씁니다. */
            static void writeFloatAttribute( XMLWriter& writer, string_view name, float32 value, int32 decimals = kValueDecimals )
            {
                utf8         arrDigit[constant::kMaxBuffer32]{};
                const uint32 length = StringUtil::formatNumber( arrDigit, constant::kMaxBuffer32, value, decimals );
                writer.attribute( name, string_view{ arrDigit, length } );
            }

            /** @brief 슬롯을 시나리오 글로 바꿉니다. 다시 읽을 수 없는 이름(엔진 서비스 없이 `Unknown` 등)이면 false 입니다. */
            [[nodiscard]] static bool tryMakeSlotText( const InputSlot& slot, string& outText )
            {
                outText = InputSlotUtil::toText( slot );
                InputSlot parsed{};
                return outText.empty() == false && InputSlotUtil::tryParse( outText, parsed );
            }

            /**
             * @brief @p slot 이 어떤 액션의, 그 장치의 첫 단추 바인딩이면 그 액션 이름입니다. 없으면 nullptr 입니다.
             * @details 재생 단계(`ActionPress` · `ActionRelease` + `device`)는 그 장치의 첫 단추 바인딩을 누른다 — 같은 슬롯일 때만 같은 입력이 재현된다.
             *          여럿이면 맵의 액션 순서로 첫 것이다(어느 것이든 같은 슬롯을 누른다).
             */
            [[nodiscard]] static const hashed_string* findActionOfSlot( const InputMap& inputMap, const InputSlot& slot )
            {
                for ( const hashed_string& action : inputMap.getActionNames() )
                {
                    const uint32 bindingCount = inputMap.getBindingCount( action );
                    for ( uint32 bindIndex = 0; bindIndex < bindingCount; ++bindIndex )
                    {
                        const ActionBinding* pBinding        = inputMap.getBinding( action, bindIndex );
                        const bool           bButtonOfDevice = pBinding != nullptr && pBinding->_kind == BindingKind::SingleSlot && pBinding->_arrSlot[0]._deviceKind == slot._deviceKind;
                        if ( bButtonOfDevice == false )
                            continue;
                        if ( pBinding->_arrSlot[0] == slot )
                            return &action;
                        break; // 재생은 이 바인딩을 누른다 — 다른 슬롯이면 이 액션으로 쓸 수 없다
                    }
                }
                return nullptr;
            }

            /** @brief 액션 단계의 `device` 값입니다. 액션 단계가 받지 않는 장치면 nullptr 입니다. */
            static const utf8* toDeviceName( InputDeviceKind kind )
            {
                switch ( kind )
                {
                    case InputDeviceKind::Keyboard:
                        return "keyboard";
                    case InputDeviceKind::Mouse:
                        return "mouse";
                    case InputDeviceKind::Gamepad:
                        return "gamepad";
                    default:
                        return nullptr;
                }
            }

            /// 패드 스틱 축 번호입니다(왼쪽 x · y = 0 · 1, 오른쪽 x · y = 2 · 3). `ActionAxis` 재생(AutomationActionSteps)과 같은 번호다.
            static constexpr uint16 kStickAxisCount = 4;

            /**
             * @struct StickTrack
             * @brief 액션 스틱 하나(패드 · 왼쪽/오른쪽)의 녹화 상태입니다. 값이 바뀔 때마다 앞 구간을 `ActionAxis`(그 값으로 구간 길이만큼)로 닫는다.
             */
            struct StickTrack
            {
                const hashed_string* _pAction{ nullptr };
                float32              _arrValue[2]{};   ///< 열린 구간의 값(x, y)
                float32              _arrPending[2]{}; ///< 이번 프레임까지 받은 최신 값(x, y)
                uint32               _runStart{ 0 };   ///< 열린 구간이 시작한 녹화 프레임
                uint8                _pad{ 0 };
                uint8                _stick{ 0 };        ///< 0 왼쪽, 1 오른쪽
                bool                 _bChanged{ false }; ///< 이번 프레임에 값이 바뀌었다
                bool                 _bOpen{ false };    ///< 0 이 아닌 값의 구간이 열려 있다
            };

            /** @brief 바인딩이 쓰는 장치 종류입니다. `ActionAxis` 재생이 `device` 로 바인딩을 고르는 규칙(AutomationActionSteps `getBindingDevice`)과 같다. */
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
             * @brief 패드 @p pad 의 스틱 @p stick 이 어떤 액션의, `ActionAxis device="gamepad"` 가 고를 바인딩(그 액션의 첫 패드 축 바인딩)이면 그 액션 이름입니다. 없으면 nullptr 입니다.
             * @details 재생은 그 바인딩의 스틱 두 축에 값을 낸다 — 같은 패드 · 같은 스틱의 2D 바인딩일 때만 같은 입력이 재현된다(한 성분 바인딩은 x 를 그 성분에 낸다).
             */
            [[nodiscard]] static const hashed_string* findActionOfStick( const InputMap& inputMap, uint8 pad, uint8 stick )
            {
                const GamepadStick wanted = stick == 0 ? GamepadStick::Left : GamepadStick::Right;
                for ( const hashed_string& action : inputMap.getActionNames() )
                {
                    const uint32 bindingCount = inputMap.getBindingCount( action );
                    for ( uint32 bindIndex = 0; bindIndex < bindingCount; ++bindIndex )
                    {
                        const ActionBinding* pBinding = inputMap.getBinding( action, bindIndex );
                        if ( pBinding == nullptr || getBindingDevice( *pBinding ) != InputDeviceKind::Gamepad )
                            continue;
                        const bool bComposite = pBinding->_kind == BindingKind::Axis1DComposite || pBinding->_kind == BindingKind::Vector2DComposite;
                        if ( bComposite == false && pBinding->_kind != BindingKind::GamepadStick2D )
                            continue; // 단추 바인딩은 ActionAxis 가 고르지 않는다
                        const bool bSameStick = pBinding->_kind == BindingKind::GamepadStick2D && pBinding->_stick == wanted && pBinding->_deviceIndex == pad &&
                                                pBinding->_stickAxis == GamepadStickAxis::Both;
                        if ( bSameStick )
                            return &action;
                        break; // 재생은 이 바인딩에 값을 낸다 — 다른 스틱이면 이 액션으로 쓸 수 없다
                    }
                }
                return nullptr;
            }

            /**
             * @brief 패드 스틱 축 이벤트를 액션 스틱의 구간 값으로 받습니다. 액션 스틱이 아니면(입력 맵이 없거나 어느 액션의 그 자리도 아니다, 트리거 축) false 이고 호출자가 `GamepadAxis` 로 쓴다.
             */
            [[nodiscard]] static bool trackStickAxis( const RawInputEvent& event, const ScenarioRecordSettings& settings, vector<StickTrack>& inoutListTrack )
            {
                const uint16 axis = event._payload._gamepadData._axisIndex;
                if ( settings._pInputMap == nullptr || axis >= kStickAxisCount )
                    return false;
                const uint8 pad    = event._deviceIndex;
                const uint8 stick  = static_cast<uint8>( axis / 2 );
                StickTrack* pTrack = nullptr;
                for ( StickTrack& track : inoutListTrack )
                {
                    if ( track._pad == pad && track._stick == stick )
                        pTrack = &track;
                }
                if ( pTrack == nullptr )
                {
                    StickTrack track;
                    track._pAction = findActionOfStick( *settings._pInputMap, pad, stick );
                    track._pad     = pad;
                    track._stick   = stick;
                    inoutListTrack.push_back( track );
                    pTrack = &inoutListTrack.back();
                }
                if ( pTrack->_pAction == nullptr )
                    return false;
                pTrack->_arrPending[axis % 2] = event._payload._gamepadData._axisValue;
                pTrack->_bChanged             = true;
                return true;
            }

            /** @brief 열린 구간을 `ActionAxis` 하나로 닫아 그 구간의 첫 프레임 본문에 씁니다. */
            static void closeStickRun( vector<unique_ptr<XMLWriter>>& inoutListBody, StickTrack& inoutTrack, uint32 endFrame, ScenarioRecordOutput& outResult )
            {
                if ( inoutTrack._bOpen == false )
                    return;
                inoutTrack._bOpen = false;
                XMLWriter& writer = *inoutListBody[inoutTrack._runStart];
                writer.beginElement( "ActionAxis" );
                writer.attribute( "name", inoutTrack._pAction->view() );
                writeFloatAttribute( writer, "x", inoutTrack._arrValue[0] );
                writeFloatAttribute( writer, "y", inoutTrack._arrValue[1] );
                writer.attributeInt( "frames", endFrame > inoutTrack._runStart ? endFrame - inoutTrack._runStart : 1u );
                writer.attribute( "device", "gamepad" );
                writer.endElement();
                ++outResult._writtenStepCount;
            }

            /**
             * @brief 이번 프레임(@p frameIndex)에 값이 바뀐 액션 스틱마다 앞 구간을 닫고, 새 값이 0 이 아니면 새 구간을 엽니다.
             * @details 재생의 `ActionAxis` 는 구간 끝에 두 축을 0 으로 되돌리고, 같은 프레임에 시작하는 다음 구간이 그 뒤에 값을 낸다(단계는 파일 순서로 준비된다).
             */
            static void advanceStickRuns( vector<unique_ptr<XMLWriter>>& inoutListBody, vector<StickTrack>& inoutListTrack, uint32 frameIndex, ScenarioRecordOutput& outResult )
            {
                for ( StickTrack& track : inoutListTrack )
                {
                    if ( track._bChanged == false )
                        continue;
                    track._bChanged = false;
                    closeStickRun( inoutListBody, track, frameIndex, outResult );
                    track._arrValue[0] = track._arrPending[0];
                    track._arrValue[1] = track._arrPending[1];
                    track._runStart    = frameIndex;
                    track._bOpen       = track._arrValue[0] != 0.0f || track._arrValue[1] != 0.0f;
                }
            }

            static float32 toFraction( int32 value, uint32 size )
            {
                if ( size <= 1 )
                    return 0.0f;
                const float32 fraction = static_cast<float32>( value ) / static_cast<float32>( size - 1 );
                return fraction < 0.0f ? 0.0f : ( fraction > 1.0f ? 1.0f : fraction );
            }

            /** @brief 슬롯 하나의 누름 · 뗌 단계를 씁니다. 액션의 단추면 액션 단계로, 글로 못 쓰는 슬롯이면 건너뛴 이벤트로 셉니다. */
            static void writeSlotStep( XMLWriter& writer, const ScenarioRecordSettings& settings, ScenarioRecordOutput& outResult, const utf8* pKind, const InputSlot& slot )
            {
                string slotText;
                // 액션의 단추면 액션 단계로 쓴다 — 키 배치를 바꿔도 녹화가 산다.
                const hashed_string* pAction     = settings._pInputMap != nullptr ? findActionOfSlot( *settings._pInputMap, slot ) : nullptr;
                const utf8*          pDeviceName = toDeviceName( slot._deviceKind );
                if ( pAction != nullptr && pDeviceName != nullptr )
                {
                    writer.beginElement( string_view{ pKind } == "Press" ? "ActionPress" : "ActionRelease" );
                    writer.attribute( "name", pAction->view() );
                    writer.attribute( "device", pDeviceName );
                    writer.endElement();
                    ++outResult._writtenStepCount;
                    return;
                }
                if ( tryMakeSlotText( slot, slotText ) == false )
                {
                    ++outResult._skippedEventCount;
                    return;
                }
                writer.beginElement( pKind );
                writer.attribute( "slot", slotText );
                writer.endElement();
                ++outResult._writtenStepCount;
            }

            /** @brief 프레임 안에서 이벤트를 모아 프레임 끝에 한 단계로 쓰는 값(마우스 위치 · 휠 · 이동량)입니다. */
            struct FrameAccumulator
            {
                int32   _positionX{ 0 };
                int32   _positionY{ 0 };
                float32 _deltaX{ 0.0f };
                float32 _deltaY{ 0.0f };
                float32 _wheelX{ 0.0f };
                float32 _wheelY{ 0.0f };
                bool    _bHasPosition{ false };
                bool    _bHasDelta{ false };
                bool    _bHasWheel{ false };
            };

            /** @brief 액션 스틱이 아닌 패드 축 이벤트를 `GamepadAxis` 단계로 씁니다. */
            static void writeGamepadAxisStep( XMLWriter& writer, const RawInputEvent& event, ScenarioRecordOutput& outResult )
            {
                writer.beginElement( "GamepadAxis" );
                writer.attributeInt( "axis", event._payload._gamepadData._axisIndex );
                writeFloatAttribute( writer, "value", event._payload._gamepadData._axisValue );
                if ( event._deviceIndex != 0 )
                    writer.attributeInt( "pad", event._deviceIndex );
                writer.endElement();
                ++outResult._writtenStepCount;
            }

            /** @brief 글자 입력 이벤트를 `Text` 단계로 씁니다. */
            static void writeTextStep( XMLWriter& writer, const RawInputEvent& event, ScenarioRecordOutput& outResult )
            {
                const utf8* pText  = event._payload._textData._arrUTF8;
                size_t      length = 0;
                while ( length < constant::kMaxBuffer32 && pText[length] != '\0' )
                {
                    ++length;
                }
                writer.beginElement( "Text" );
                writer.attribute( "value", string_view{ pText, length } );
                writer.endElement();
                ++outResult._writtenStepCount;
            }

            /** @brief 위치와 이동량은 프레임 끝의 값 하나로 씁니다. 눌림보다 뒤에 오지만 같은 프레임이라 실행기는 한 번에 적용한다. */
            static void writeAccumulatedSteps( XMLWriter& writer, const FrameAccumulator& accumulator, const ScenarioRecordSettings& settings, ScenarioRecordOutput& outResult )
            {
                if ( accumulator._bHasPosition )
                {
                    writer.beginElement( "MousePosition" );
                    writeFloatAttribute( writer, "x", toFraction( accumulator._positionX, settings._clientWidth ) );
                    writeFloatAttribute( writer, "y", toFraction( accumulator._positionY, settings._clientHeight ) );
                    writer.endElement();
                    ++outResult._writtenStepCount;
                }
                if ( accumulator._bHasWheel )
                {
                    writer.beginElement( "Wheel" );
                    if ( accumulator._wheelX != 0.0f )
                        writeFloatAttribute( writer, "x", accumulator._wheelX );
                    writeFloatAttribute( writer, "y", accumulator._wheelY );
                    writer.endElement();
                    ++outResult._writtenStepCount;
                }
                if ( accumulator._bHasDelta )
                {
                    writer.beginElement( "MouseDelta" );
                    writeFloatAttribute( writer, "x", accumulator._deltaX );
                    writeFloatAttribute( writer, "y", accumulator._deltaY );
                    writer.endElement();
                    ++outResult._writtenStepCount;
                }
            }

            /**
             * @brief 한 프레임의 이벤트를 단계 글로 씁니다. 쓴 단계 수를 더하고, 못 쓴 이벤트를 셉니다.
             * @details Win32 는 두 번째 누름을 Down 대신 DoubleClick 으로 보낸다(Down · Up · DoubleClick · Up). 입력 관리자도 이를 버튼 눌림으로 다루므로 Press 로 쓴다.
             *          건너뛰면 두 번째 누름이 사라진다. 재생은 원시 Down 둘을 낸다. DoubleClick 이벤트 자체를 읽는 쪽은 없다 — 입력 관리자는 Down 으로 다루고,
             *          입력 맵(DoubleClicked)과 ImGui 는 누름 간격으로 스스로 판정한다. 그 이벤트를 읽는 코드가 생기면 VirtualInputScript 에 DoubleClick 단계를 더한다.
             */
            static void writeFrameSteps( XMLWriter& writer, const InputReplayFrame& frame, const ScenarioRecordSettings& settings, vector<StickTrack>& inoutListTrack,
                                         ScenarioRecordOutput& outResult )
            {
                FrameAccumulator accumulator{};
                for ( const RawInputEvent& event : frame._listRawEvent )
                {
                    if ( event._bSynthetic != SW_FALSE && settings._bIncludeSynthetic == false )
                        continue; // 시나리오 재생이 낸 입력이다
                    switch ( event._type )
                    {
                        case RawInputEventType::KeyDown:
                        {
                            if ( event._bRepeat == SW_FALSE )
                                writeSlotStep( writer, settings, outResult, "Press", InputSlot::fromKey( event._payload._keyData._key ) );
                            break;
                        }
                        case RawInputEventType::KeyUp:
                        {
                            writeSlotStep( writer, settings, outResult, "Release", InputSlot::fromKey( event._payload._keyData._key ) );
                            break;
                        }
                        case RawInputEventType::MouseButtonDown:
                        case RawInputEventType::MouseDoubleClick:
                        {
                            writeSlotStep( writer, settings, outResult, "Press", InputSlot::fromMouseButton( event._payload._mouseData._button ) );
                            break;
                        }
                        case RawInputEventType::MouseButtonUp:
                        {
                            writeSlotStep( writer, settings, outResult, "Release", InputSlot::fromMouseButton( event._payload._mouseData._button ) );
                            break;
                        }
                        case RawInputEventType::GamepadButtonDown:
                        {
                            writeSlotStep( writer, settings, outResult, "Press", InputSlot::fromGamepadButton( event._payload._gamepadData._button, event._deviceIndex ) );
                            break;
                        }
                        case RawInputEventType::GamepadButtonUp:
                        {
                            writeSlotStep( writer, settings, outResult, "Release", InputSlot::fromGamepadButton( event._payload._gamepadData._button, event._deviceIndex ) );
                            break;
                        }
                        case RawInputEventType::MouseMove:
                        {
                            if ( settings._clientWidth == 0 || settings._clientHeight == 0 )
                            {
                                ++outResult._skippedEventCount;
                                break;
                            }
                            accumulator._bHasPosition = true;
                            accumulator._positionX    = event._payload._mouseData._x;
                            accumulator._positionY    = event._payload._mouseData._y;
                            break;
                        }
                        case RawInputEventType::MouseRawDelta:
                        {
                            accumulator._bHasDelta = true;
                            accumulator._deltaX += event._payload._mouseData._rawDelta._x;
                            accumulator._deltaY += event._payload._mouseData._rawDelta._y;
                            break;
                        }
                        case RawInputEventType::MouseWheel:
                        {
                            accumulator._bHasWheel = true;
                            accumulator._wheelY += event._payload._mouseData._wheelDelta;
                            break;
                        }
                        case RawInputEventType::MouseWheelHorizontal:
                        {
                            accumulator._bHasWheel = true;
                            accumulator._wheelX += event._payload._mouseData._wheelDelta;
                            break;
                        }
                        case RawInputEventType::GamepadAxis:
                        {
                            // 액션의 스틱이면 값 구간을 ActionAxis 로 쓴다(advanceStickRuns) — 스틱 배치를 바꿔도 녹화가 산다.
                            if ( trackStickAxis( event, settings, inoutListTrack ) == false )
                                writeGamepadAxisStep( writer, event, outResult );
                            break;
                        }
                        case RawInputEventType::TextInput:
                        {
                            writeTextStep( writer, event, outResult );
                            break;
                        }
                        case RawInputEventType::None:
                        case RawInputEventType::GamepadConnectionChanged:
                        case RawInputEventType::TextComposition:
                        case RawInputEventType::FocusGained:
                        case RawInputEventType::FocusLost:
                        case RawInputEventType::PointerEntered:
                        case RawInputEventType::PointerLeft:
                        {
                            // 조합 중 글자는 확정 뒤 TextInput 으로 다시 오고, 포커스 · 포인터 출입 · 패드 연결은 창과 장치의 상태라
                            // 시나리오 단계로 낼 뜻이 없다(실행기는 창을 가진 채 입력만 넣는다).
                            ++outResult._skippedEventCount;
                            break;
                        }
                    }
                }
                writeAccumulatedSteps( writer, accumulator, settings, outResult );
            }
        };
    } // namespace

    void ScenarioRecorder::start()
    {
        _replay.startRecording( "scenario" );
        _bHasLastBeginFrame = false;
    }

    void ScenarioRecorder::update( const InputManager& input, float32 deltaTime )
    {
        if ( _replay.isRecording() == false )
            return;
        if ( _bHasLastBeginFrame && input.getBeginFrameCount() == _lastBeginFrameCount )
            return;
        _bHasLastBeginFrame  = true;
        _lastBeginFrameCount = input.getBeginFrameCount();
        _pInputMap           = &input.getInputMap();
        _replay.recordFrame( deltaTime, input.getLastFrameEvents() );
    }

    ScenarioRecordOutput ScenarioRecorder::stop( const ScenarioRecordSettings& settings )
    {
        if ( _replay.isRecording() == false )
            return {};
        _replay.stopRecording();
        ScenarioRecordSettings resolved = settings;
        if ( resolved._pInputMap == nullptr )
            resolved._pInputMap = _pInputMap;
        ScenarioRecordOutput result = toScenarioXML( _replay.getFrames(), resolved );
        _replay.clear();
        if ( result._skippedEventCount != 0 )
            SW_LOG_INFO( "[ScenarioRecorder] skipped %# input events that have no scenario step", result._skippedEventCount );
        return result;
    }

    ScenarioRecordOutput ScenarioRecorder::toScenarioXML( const vector<InputReplayFrame>& listFrame, const ScenarioRecordSettings& settings )
    {
        using Internal = ScenarioRecorderInternal;

        ScenarioRecordOutput          result;
        vector<unique_ptr<XMLWriter>> listFrameBody;
        listFrameBody.reserve( listFrame.size() );
        vector<Internal::StickTrack> listStickTrack;
        for ( size_t index = 0; index < listFrame.size(); ++index )
        {
            listFrameBody.push_back( make_unique<XMLWriter>() );
            Internal::writeFrameSteps( *listFrameBody[index], listFrame[index], settings, listStickTrack, result );
            Internal::advanceStickRuns( listFrameBody, listStickTrack, static_cast<uint32>( index ), result );
        }
        // 녹화가 끝날 때 기울어 있던 스틱은 마지막 녹화 프레임 다음에 놓는다(재생이 끝 뒤까지 기울인 채 남기지 않는다).
        for ( Internal::StickTrack& track : listStickTrack )
        {
            Internal::closeStickRun( listFrameBody, track, static_cast<uint32>( listFrame.size() ), result );
        }
        uint32 lastFrame = settings._firstFrame;
        for ( size_t index = 0; index < listFrameBody.size(); ++index )
        {
            if ( listFrameBody[index]->isEmpty() == false )
                lastFrame = settings._firstFrame + static_cast<uint32>( index );
        }
        // 검사점은 마지막 녹화 프레임(값을 읽은 프레임)에 둔다 — 재생도 그 프레임의 씬 틱 뒤에 같은 값을 내야 한다.
        const bool   bCheckpoints    = settings._listCheckpoint.empty() == false && listFrame.empty() == false;
        const uint32 checkpointFrame = settings._firstFrame + static_cast<uint32>( listFrame.size() ) - ( listFrame.empty() ? 0u : 1u );
        const uint32 passFrame       = bCheckpoints ? checkpointFrame + 1 : lastFrame + Internal::kTailFrameCount;

        XMLWriter writer{ result._xmlText };
        writer.declaration();
        writer.comment( bCheckpoints ? "녹화한 입력과 끝 프레임의 검사점입니다. 같은 입력이면 같은 값이 나와야 합니다(결정적 재생)."
                                     : "scenario.record 로 녹화한 입력입니다. 기대(Expect) 단계는 손으로 더합니다." );
        writer.whitespace( "\n" );
        writer.beginElement( "Scenario" );
        writer.attribute( "name", settings._name );
        Internal::writeFloatAttribute( writer, "fixedDelta", settings._fixedDelta > 0.0f ? settings._fixedDelta : 1.0f / 60.0f, Internal::kFixedDeltaDecimals );
        writer.attributeInt( "timeoutFrames", passFrame + Internal::kTimeoutMargin );
        writer.attribute( "startAfter", settings._bStartWhenScenePlaying ? "ScenePlaying" : "Immediately" );
        writer.attribute( "input", "exclusive" );
        // 단계는 적은 순서대로 돌고 시간은 `<Wait frames>` 만 흘린다 — 녹화한 프레임 사이의 간격을 `<Wait>` 로 옮긴다.
        uint32 frameCursor    = 0;
        auto   waitUntilFrame = [&writer, &frameCursor]( uint32 frame )
        {
            if ( frame <= frameCursor )
                return;
            writer.whitespace( "\n\t" );
            writer.beginElement( "Wait" );
            writer.attributeInt( "frames", frame - frameCursor );
            writer.endElement();
            frameCursor = frame;
        };
        for ( size_t index = 0; index < listFrameBody.size(); ++index )
        {
            if ( listFrameBody[index]->isEmpty() )
                continue;
            waitUntilFrame( settings._firstFrame + static_cast<uint32>( index ) );
            writer.whitespace( "\n\t" );
            // 프레임 본문은 따로 쓴 조각이다 — 스틱 구간은 앞 프레임의 본문에 나중에 닫힌다.
            writer.fragment( *listFrameBody[index] );
        }
        if ( bCheckpoints )
        {
            waitUntilFrame( checkpointFrame );
            for ( const ScenarioRecordCheckpoint& checkpoint : settings._listCheckpoint )
            {
                writer.whitespace( "\n\t" );
                writer.beginElement( "Expect" );
                writer.attribute( "watch", checkpoint._watch );
                if ( checkpoint._target.empty() == false )
                    writer.attribute( "target", checkpoint._target );
                if ( checkpoint._key.empty() == false )
                    writer.attribute( "key", checkpoint._key );
                writer.attribute( checkpoint._bText ? "is" : "near", checkpoint._value );
                if ( checkpoint._bText == false )
                    Internal::writeFloatAttribute( writer, "tolerance", settings._checkpointTolerance );
                writer.endElement();
            }
        }
        waitUntilFrame( passFrame );
        writer.whitespace( "\n\t" );
        writer.beginElement( "Pass" );
        writer.endElement();
        writer.whitespace( "\n" );
        writer.endElement();
        writer.whitespace( "\n" );
        SW_ASSERT( writer.isComplete() );
        return result;
    }

    bool ScenarioRecorder::readCheckpoint( string_view spec, const GameObjectManager* pManager, ScenarioRecordCheckpoint& outCheckpoint, string& outError )
    {
        outCheckpoint         = ScenarioRecordCheckpoint{};
        const size_t firstAt  = spec.find( '@' );
        const size_t secondAt = firstAt == string_view::npos ? string_view::npos : spec.find( '@', firstAt + 1 );
        outCheckpoint._watch  = string( StringUtil::trim( spec.substr( 0, firstAt ) ) );
        if ( firstAt != string_view::npos )
            outCheckpoint._target = string( StringUtil::trim( spec.substr( firstAt + 1, secondAt == string_view::npos ? string_view::npos : secondAt - firstAt - 1 ) ) );
        if ( secondAt != string_view::npos )
            outCheckpoint._key = string( StringUtil::trim( spec.substr( secondAt + 1 ) ) );
        const AutomationWatchRegistration* pRegistration = AutomationWatchRegistry::find( outCheckpoint._watch );
        if ( pRegistration == nullptr )
        {
            outError = "unknown watch '" + outCheckpoint._watch + "'";
            return false;
        }
        const AutomationWatchQuery query = AutomationWatchQuery::make( pManager, outCheckpoint._target, outCheckpoint._key );
        AutomationWatchValue       value{};
        if ( AutomationWatchRegistry::read( *pRegistration, query, value ) == false )
        {
            outError = string( spec ) + " has no value" + ( value._bText ? " (" + value._text + ")" : string() );
            return false;
        }
        outCheckpoint._bText = value._bText;
        if ( value._bText )
        {
            outCheckpoint._value = value._text;
            return true;
        }
        utf8 arrDigit[constant::kMaxBuffer64]{};
        (void)StringUtil::formatNumber( arrDigit, constant::kMaxBuffer64, value._number, 6 ); // 쓴 길이는 널 종료로 대신한다
        outCheckpoint._value = arrDigit;
        return true;
    }

    bool ScenarioRecorder::isAllowedOutputPath( string_view path )
    {
        using Internal          = ScenarioRecorderInternal;
        const size_t folderSize = Internal::kOutputFolder.size() + 1; // 뒤의 '/' 까지
        if ( path.size() <= folderSize + Internal::kScenarioSuffix.size() )
            return false;
        if ( path.substr( 0, folderSize - 1 ) != Internal::kOutputFolder || path[folderSize - 1] != '/' )
            return false;
        if ( path.substr( path.size() - Internal::kScenarioSuffix.size() ) != Internal::kScenarioSuffix )
            return false;
        const bool bUnsafeScenarioPath = path.find( ".." ) != string_view::npos || path.find( '\\' ) != string_view::npos || path.find( ':' ) != string_view::npos;
        if ( bUnsafeScenarioPath )
            return false;
        return true;
    }

    bool ScenarioRecorder::mountSavedDomain( string_view workingFolder )
    {
        const string savedFolder = FileUtil::joinPath( workingFolder, path::kSavedFolder );
        if ( FileUtil::ensureDirectoryExists( savedFolder ) == false )
        {
            SW_LOG_ERROR( "[Scenario] could not create '%#' for the '%#' domain", savedFolder.c_str(), kSavedDomain );
            return false;
        }
        return AssetPaths::mountDiskDomain( kSavedDomain, savedFolder ); // 거절하면 이유를 로그로 남긴다
    }

    string ScenarioRecorder::makeReplayID( string_view outputPath )
    {
        if ( isAllowedOutputPath( outputPath ) == false )
            return {};
        // `Saved/` 를 도메인 이름으로 바꾼다. 나머지는 표기 그대로 둔다(리눅스는 대소문자를 가린다 — `AssetPaths::findLooseFile` 이 원래 표기로 다시 찾는다).
        const size_t firstSlash = outputPath.find( '/' );
        return string( kSavedDomain ) + string( outputPath.substr( firstSlash ) );
    }
} // namespace sw
