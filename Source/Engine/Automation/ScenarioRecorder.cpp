#include "pch.h"

#include "Engine/Automation/ScenarioRecorder.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/RawInputEvent.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    namespace
    {
        struct ScenarioRecorderInternal
        {
            static constexpr string_view kOutputFolder   = "Saved/Automation/";
            static constexpr string_view kScenarioSuffix = ".scenario.xml";
            static constexpr uint32      kTailFrameCount = 10; ///< 마지막 입력 뒤 `Pass` 까지의 프레임입니다.
            static constexpr uint32      kTimeoutMargin  = 60;

            /** @brief XML 속성 값으로 쓸 수 있게 특수 문자를 바꿉니다. 제어 문자는 버립니다. */
            static void appendEscaped( string& outText, string_view text )
            {
                for ( const utf8 character : text )
                {
                    switch ( character )
                    {
                        case '&':
                        {
                            outText += "&amp;";
                            break;
                        }
                        case '<':
                        {
                            outText += "&lt;";
                            break;
                        }
                        case '>':
                        {
                            outText += "&gt;";
                            break;
                        }
                        case '"':
                        {
                            outText += "&quot;";
                            break;
                        }
                        default:
                        {
                            if ( static_cast<uint8>( character ) >= 0x20 )
                                outText.push_back( character );
                            break;
                        }
                    }
                }
            }

            static void appendFloat( string& outText, float32 value )
            {
                utf8 arrDigit[constant::kMaxBuffer32]{};
                (void)StringUtil::formatNumber( arrDigit, constant::kMaxBuffer32, value, 4 ); // 쓴 길이는 널 종료로 대신한다
                outText += arrDigit;
            }

            /** @brief 슬롯을 시나리오 글로 바꿉니다. 다시 읽을 수 없는 이름(엔진 서비스 없이 `Unknown` 등)이면 false 입니다. */
            [[nodiscard]] static bool tryMakeSlotText( const InputSlot& slot, string& outText )
            {
                outText = InputSlotUtil::toText( slot );
                InputSlot parsed{};
                return outText.empty() == false && InputSlotUtil::tryParse( outText, parsed );
            }

            static float32 toFraction( int32 value, uint32 size )
            {
                if ( size <= 1 )
                    return 0.0f;
                const float32 fraction = static_cast<float32>( value ) / static_cast<float32>( size - 1 );
                return fraction < 0.0f ? 0.0f : ( fraction > 1.0f ? 1.0f : fraction );
            }

            /** @brief 한 프레임의 이벤트를 단계 글로 씁니다. 쓴 단계 수를 더하고, 못 쓴 이벤트를 셉니다. */
            static void appendFrameSteps( string& outBody, const InputReplayFrame& frame, const ScenarioRecordSettings& settings, ScenarioRecordOutput& outResult )
            {
                bool    bHasPosition = false;
                int32   positionX    = 0;
                int32   positionY    = 0;
                bool    bHasDelta    = false;
                float32 deltaX       = 0.0f;
                float32 deltaY       = 0.0f;
                bool    bHasWheel    = false;
                float32 wheelX       = 0.0f;
                float32 wheelY       = 0.0f;
                string  slotText;

                const auto appendSlot = [&]( const utf8* pKind, const InputSlot& slot )
                {
                    if ( tryMakeSlotText( slot, slotText ) == false )
                    {
                        ++outResult._skippedEventCount;
                        return;
                    }
                    outBody += "<";
                    outBody += pKind;
                    outBody += " slot=\"";
                    appendEscaped( outBody, slotText );
                    outBody += "\"/>";
                    ++outResult._writtenStepCount;
                };

                for ( const RawInputEvent& event : frame._listRawEvent )
                {
                    if ( event._bSynthetic != SW_FALSE )
                        continue; // 시나리오 재생이 낸 입력이다
                    switch ( event._type )
                    {
                        case RawInputEventType::KeyDown:
                        {
                            if ( event._bRepeat == SW_FALSE )
                                appendSlot( "Press", InputSlot::fromKey( event._payload._keyData._key ) );
                            break;
                        }
                        case RawInputEventType::KeyUp:
                        {
                            appendSlot( "Release", InputSlot::fromKey( event._payload._keyData._key ) );
                            break;
                        }
                        case RawInputEventType::MouseButtonDown:
                        case RawInputEventType::MouseDoubleClick:
                        {
                            // Win32 는 두 번째 누름을 Down 대신 DoubleClick 으로 보낸다(Down · Up · DoubleClick · Up).
                            // 입력 관리자도 이를 버튼 눌림으로 다루므로 Press 로 쓴다. 건너뛰면 두 번째 누름이 사라진다.
                            // 재생은 원시 Down 둘을 내므로 DoubleClick 이벤트 자체에 기대는 코드는 재현되지 않는다.
                            appendSlot( "Press", InputSlot::fromMouseButton( event._payload._mouseData._button ) );
                            break;
                        }
                        case RawInputEventType::MouseButtonUp:
                        {
                            appendSlot( "Release", InputSlot::fromMouseButton( event._payload._mouseData._button ) );
                            break;
                        }
                        case RawInputEventType::GamepadButtonDown:
                        {
                            appendSlot( "Press", InputSlot::fromGamepadButton( event._payload._gamepadData._button, event._deviceIndex ) );
                            break;
                        }
                        case RawInputEventType::GamepadButtonUp:
                        {
                            appendSlot( "Release", InputSlot::fromGamepadButton( event._payload._gamepadData._button, event._deviceIndex ) );
                            break;
                        }
                        case RawInputEventType::MouseMove:
                        {
                            if ( settings._clientWidth == 0 || settings._clientHeight == 0 )
                            {
                                ++outResult._skippedEventCount;
                                break;
                            }
                            bHasPosition = true;
                            positionX    = event._payload._mouseData._x;
                            positionY    = event._payload._mouseData._y;
                            break;
                        }
                        case RawInputEventType::MouseRawDelta:
                        {
                            bHasDelta = true;
                            deltaX += event._payload._mouseData._rawDelta._x;
                            deltaY += event._payload._mouseData._rawDelta._y;
                            break;
                        }
                        case RawInputEventType::MouseWheel:
                        {
                            bHasWheel = true;
                            wheelY += event._payload._mouseData._wheelDelta;
                            break;
                        }
                        case RawInputEventType::MouseWheelHorizontal:
                        {
                            bHasWheel = true;
                            wheelX += event._payload._mouseData._wheelDelta;
                            break;
                        }
                        case RawInputEventType::GamepadAxis:
                        {
                            outBody += "<GamepadAxis axis=\"" + to_string( static_cast<uint32>( event._payload._gamepadData._axisIndex ) ) + "\" value=\"";
                            appendFloat( outBody, event._payload._gamepadData._axisValue );
                            outBody += "\"";
                            if ( event._deviceIndex != 0 )
                                outBody += " pad=\"" + to_string( static_cast<uint32>( event._deviceIndex ) ) + "\"";
                            outBody += "/>";
                            ++outResult._writtenStepCount;
                            break;
                        }
                        case RawInputEventType::TextInput:
                        {
                            const utf8* pText  = event._payload._textData._arrUtf8;
                            size_t      length = 0;
                            while ( length < constant::kMaxBuffer32 && pText[length] != '\0' )
                            {
                                ++length;
                            }
                            outBody += "<Text value=\"";
                            appendEscaped( outBody, string_view{ pText, length } );
                            outBody += "\"/>";
                            ++outResult._writtenStepCount;
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

                // 위치와 이동량은 프레임 끝의 값 하나로 쓴다. 눌림보다 뒤에 오지만 같은 프레임이라 실행기는 한 번에 적용한다.
                if ( bHasPosition )
                {
                    outBody += "<MousePosition x=\"";
                    appendFloat( outBody, toFraction( positionX, settings._clientWidth ) );
                    outBody += "\" y=\"";
                    appendFloat( outBody, toFraction( positionY, settings._clientHeight ) );
                    outBody += "\"/>";
                    ++outResult._writtenStepCount;
                }
                if ( bHasWheel )
                {
                    outBody += "<Wheel";
                    if ( wheelX != 0.0f )
                    {
                        outBody += " x=\"";
                        appendFloat( outBody, wheelX );
                        outBody += "\"";
                    }
                    outBody += " y=\"";
                    appendFloat( outBody, wheelY );
                    outBody += "\"/>";
                    ++outResult._writtenStepCount;
                }
                if ( bHasDelta )
                {
                    outBody += "<MouseDelta x=\"";
                    appendFloat( outBody, deltaX );
                    outBody += "\" y=\"";
                    appendFloat( outBody, deltaY );
                    outBody += "\"/>";
                    ++outResult._writtenStepCount;
                }
            }
        };
    } // namespace

    void ScenarioRecorder::start()
    {
        _replay.startRecording( "scenario" );
        _bHasLastBeginFrame = false;
    }

    void ScenarioRecorder::tick( const InputManager& input, float32 deltaTime )
    {
        if ( _replay.isRecording() == false )
            return;
        if ( _bHasLastBeginFrame && input.getBeginFrameCount() == _lastBeginFrameCount )
            return;
        _bHasLastBeginFrame  = true;
        _lastBeginFrameCount = input.getBeginFrameCount();
        _replay.recordFrame( deltaTime, input.getLastFrameEvents() );
    }

    ScenarioRecordOutput ScenarioRecorder::stop( const ScenarioRecordSettings& settings )
    {
        if ( _replay.isRecording() == false )
            return {};
        _replay.stopRecording();
        ScenarioRecordOutput result = toScenarioXML( _replay.getFrames(), settings );
        _replay.clear();
        if ( result._skippedEventCount != 0 )
            SW_LOG_INFO( "[ScenarioRecorder] skipped %# input events that have no scenario step", result._skippedEventCount );
        return result;
    }

    ScenarioRecordOutput ScenarioRecorder::toScenarioXML( const vector<InputReplayFrame>& listFrame, const ScenarioRecordSettings& settings )
    {
        using Internal = ScenarioRecorderInternal;

        ScenarioRecordOutput result;
        string               body;
        string               frameBody;
        uint32               lastFrame = settings._firstFrame;
        for ( size_t index = 0; index < listFrame.size(); ++index )
        {
            frameBody.clear();
            Internal::appendFrameSteps( frameBody, listFrame[index], settings, result );
            if ( frameBody.empty() )
                continue;
            const uint32 frame = settings._firstFrame + static_cast<uint32>( index );
            body += "\t<At frame=\"" + to_string( frame ) + "\">" + frameBody + "</At>\n";
            lastFrame = frame;
        }
        const uint32 passFrame = lastFrame + Internal::kTailFrameCount;

        string& text = result._xmlText;
        text         = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        text += "<!-- scenario.record 로 녹화한 입력입니다. 기대(Expect) 단계는 손으로 더합니다. -->\n";
        text += "<Scenario name=\"";
        Internal::appendEscaped( text, settings._name );
        text += "\" fixedDelta=\"";
        Internal::appendFloat( text, settings._fixedDelta > 0.0f ? settings._fixedDelta : 1.0f / 60.0f );
        text += "\" timeoutFrames=\"" + to_string( passFrame + Internal::kTimeoutMargin ) + "\" startAfter=\"Immediately\" input=\"exclusive\">\n";
        text += body;
        text += "\t<At frame=\"" + to_string( passFrame ) + "\"><Pass/></At>\n";
        text += "</Scenario>\n";
        return result;
    }

    bool ScenarioRecorder::isAllowedOutputPath( string_view path )
    {
        using Internal = ScenarioRecorderInternal;
        if ( path.size() <= Internal::kOutputFolder.size() + Internal::kScenarioSuffix.size() )
            return false;
        if ( path.substr( 0, Internal::kOutputFolder.size() ) != Internal::kOutputFolder )
            return false;
        if ( path.substr( path.size() - Internal::kScenarioSuffix.size() ) != Internal::kScenarioSuffix )
            return false;
        const bool bUnsafeScenarioPath = path.find( ".." ) != string_view::npos || path.find( '\\' ) != string_view::npos || path.find( ':' ) != string_view::npos;
        if ( bUnsafeScenarioPath )
            return false;
        return true;
    }
} // namespace sw
