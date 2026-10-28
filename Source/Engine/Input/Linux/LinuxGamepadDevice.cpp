#include "pch.h"

#include "Engine/Input/Linux/LinuxGamepadDevice.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Input/GamepadButtonUtil.h"

namespace sw
{
    bool LinuxGamepadDevice::isGamepadButtonMap( const uint16* pButtonCode, uint32 buttonCount )
    {
        // linux/input-event-codes.h 의 값이다. 판정을 Windows 에서도 테스트하려고 헤더 없이 둔다.
        constexpr uint16 kButtonJoystickFirst     = 0x120; // BTN_JOYSTICK(BTN_TRIGGER): 조이스틱 0x120..0x12f, 게임패드 0x130..0x13e
        constexpr uint16 kButtonDigitizerFirst    = 0x140; // BTN_DIGI(BTN_TOOL_PEN): 여기부터는 태블릿과 터치
        constexpr uint16 kButtonTriggerHappyFirst = 0x2c0; // BTN_TRIGGER_HAPPY1
        constexpr uint16 kButtonTriggerHappyLast  = 0x2e7; // BTN_TRIGGER_HAPPY40
        if ( pButtonCode == nullptr )
            return false;
        for ( uint32 index = 0; index < buttonCount; ++index )
        {
            const uint16 code = pButtonCode[index];
            if ( code >= kButtonJoystickFirst && code < kButtonDigitizerFirst )
                return true;
            if ( code >= kButtonTriggerHappyFirst && code <= kButtonTriggerHappyLast )
                return true;
        }
        return false;
    }
} // namespace sw

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOSHeaders.h"

    #include <cerrno>
    #include <fcntl.h>
    #include <linux/input.h>
    #include <linux/joystick.h>
    #include <sys/ioctl.h>

namespace sw
{
    namespace
    {
        /**
         * @brief Xbox 호환(xpad 드라이버) 조이스틱의 축과 버튼 인덱스 상수입니다.
         * @note 실제 하드웨어에 따라 인덱스가 다를 수 있습니다. SDL 의 게임패드 매핑 DB 같은
         *       기기별 보정은 하지 않고, 가장 흔한 xpad 배치를 기준으로 한 최선의 추정치입니다.
         */
        struct LinuxGamepadDeviceInternal
        {
            static constexpr uint8 kAxisLeftX        = 0;
            static constexpr uint8 kAxisLeftY        = 1;
            static constexpr uint8 kAxisLeftTrigger  = 2;
            static constexpr uint8 kAxisRightX       = 3;
            static constexpr uint8 kAxisRightY       = 4;
            static constexpr uint8 kAxisRightTrigger = 5;
            static constexpr uint8 kAxisDPadX        = 6;
            static constexpr uint8 kAxisDPadY        = 7;

            static constexpr uint8                  kButtonA             = 0;
            static constexpr uint8                  kButtonB             = 1;
            static constexpr uint8                  kButtonX             = 2;
            static constexpr uint8                  kButtonY             = 3;
            static constexpr uint8                  kButtonLeftShoulder  = 4;
            static constexpr uint8                  kButtonRightShoulder = 5;
            static constexpr uint8                  kButtonBack          = 6;
            static constexpr uint8                  kButtonStart         = 7;
            [[maybe_unused]] static constexpr uint8 kButtonGuide         = 8; ///< GamepadButton 에 대응하는 값이 없어 무시합니다.
            static constexpr uint8                  kButtonLeftThumb     = 9;
            static constexpr uint8                  kButtonRightThumb    = 10;

            /** @brief EVIOCGBIT 이 채운 커널 비트맵에서 특정 기능 비트가 켜져 있는지 봅니다. */
            static bool testFeatureBit( const uintptr_t* pBits, uint32 bitIndex )
            {
                constexpr uint32 kBitsPerWord = static_cast<uint32>( sizeof( uintptr_t ) * 8 );
                return ( ( pBits[bitIndex / kBitsPerWord] >> ( bitIndex % kBitsPerWord ) ) & uintptr_t{ 1 } ) != 0;
            }

            /** @brief js_event 축 값(-32767~32767)을 [-1, 1] 스틱 축으로 정규화합니다. bInvert 면 부호를 뒤집습니다. */
            static float32 normalizeStickAxis( int16 rawValue, bool bInvert )
            {
                float32 normalized = static_cast<float32>( rawValue ) / 32767.0f;
                if ( bInvert )
                    normalized = -normalized;
                return MathUtil::clamp( normalized, -1.0f, 1.0f );
            }

            /** @brief js_event 축 값(-32767~32767)을 [0, 1] 트리거 압력으로 정규화합니다(쉬는 값 -32767 가정). */
            static float32 normalizeTriggerAxis( int16 rawValue )
            {
                const float32 normalized = ( static_cast<float32>( rawValue ) + 32767.0f ) / 65534.0f;
                return MathUtil::clamp( normalized, 0.0f, 1.0f );
            }

            /** @brief js_event 버튼 인덱스를 GamepadButton 으로 바꿉니다. 대응값이 없으면 Count 입니다. */
            static GamepadButton mapJsButtonIndex( uint8 jsButtonIndex )
            {
                switch ( jsButtonIndex )
                {
                    case kButtonA:
                        return GamepadButton::A;
                    case kButtonB:
                        return GamepadButton::B;
                    case kButtonX:
                        return GamepadButton::X;
                    case kButtonY:
                        return GamepadButton::Y;
                    case kButtonLeftShoulder:
                        return GamepadButton::LeftShoulder;
                    case kButtonRightShoulder:
                        return GamepadButton::RightShoulder;
                    case kButtonBack:
                        return GamepadButton::Back;
                    case kButtonStart:
                        return GamepadButton::Start;
                    case kButtonLeftThumb:
                        return GamepadButton::LeftThumb;
                    case kButtonRightThumb:
                        return GamepadButton::RightThumb;
                    default:
                        return GamepadButton::Count; // Guide 등 대응하는 값이 없다
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LinuxGamepadDevice::LinuxGamepadDevice( uint32 userIndex )
        : GamepadDevice{ userIndex }
        , _fdJoystick{ invalid_index::kInt32 }
        , _fdForceFeedback{ invalid_index::kInt32 }
        , _ffEffectID{ invalid_index::kInt16 }
        , _ffStrongMagnitude{ 0 }
        , _ffWeakMagnitude{ 0 }
        , _reconnectTimer{ 1.0f }
        , _bConnected{ SW_FALSE }
        , _bHasForceFeedback{ SW_FALSE }
        , _reservedPad{ 0 }
    {
    }

    LinuxGamepadDevice::~LinuxGamepadDevice()
    {
        closeForceFeedback();
        closeJoystick();
    }

    void LinuxGamepadDevice::tryOpenJoystick()
    {
        StringBuilder<constant::kMaxPathSize> pathBuilder;
        pathBuilder.appendFormat( "/dev/input/js%#", _deviceIndex );

        const int32 fd = open( pathBuilder.c_str(), O_RDONLY | O_NONBLOCK );
        if ( fd < 0 )
        {
            _bConnected = SW_FALSE;
            return;
        }

        // 마우스나 태블릿에 joydev 가 만든 노드는 패드가 아니다(버튼 맵에 조이스틱이나 게임패드 버튼이 없다).
        uint8 buttonCount = 0;
        if ( ioctl( fd, JSIOCGBUTTONS, &buttonCount ) < 0 )
            buttonCount = 0;
        uint16     arrButtonCode[KEY_MAX - BTN_MISC + 1]{};
        const bool bInvalidButtonMap = buttonCount == 0 || ioctl( fd, JSIOCGBTNMAP, arrButtonCode ) < 0 || isGamepadButtonMap( arrButtonCode, buttonCount ) == false;
        if ( bInvalidButtonMap ) // 버튼 수(uint8) < 맵 크기
        {
            close( fd );
            _bConnected = SW_FALSE;
            return;
        }

        _fdJoystick = fd;
        _bConnected = SW_TRUE;
        tryOpenForceFeedback();

        if ( _onConnectionChanged.isBound() )
            _onConnectionChanged( _deviceIndex, true );
    }

    void LinuxGamepadDevice::closeJoystick()
    {
        if ( _fdJoystick >= 0 )
        {
            close( _fdJoystick );
            _fdJoystick = invalid_index::kInt32;
        }
    }

    void LinuxGamepadDevice::tryOpenForceFeedback()
    {
        // /sys/class/input/jsN/device/ 는 실제 입력 장치 디렉터리를 가리키고, 같은 물리 컨트롤러의
        // evdev 노드(eventM)가 그 안에 형제 항목으로 있다. 그것을 찾아 /dev/input/eventM 을 연다.
        StringBuilder<constant::kMaxPathSize> sysfsPathBuilder;
        sysfsPathBuilder.appendFormat( "/sys/class/input/js%#/device", _deviceIndex );

        DIR* pDir = opendir( sysfsPathBuilder.c_str() );
        if ( pDir == nullptr )
            return;

        string eventName;
        bool   bFoundEvent = false;
        while ( dirent* pEntry = readdir( pDir ) )
        {
            if ( StringUtil::startsWith( pEntry->d_name, "event" ) )
            {
                eventName   = pEntry->d_name;
                bFoundEvent = true;
                break;
            }
        }
        closedir( pDir );

        if ( bFoundEvent == false )
            return;

        const string eventPath = FileUtil::joinPath( "/dev/input", eventName );

        const int32 fd = open( eventPath.c_str(), O_RDWR | O_NONBLOCK );
        if ( fd < 0 )
            return;

        // 이 evdev 노드가 실제로 force-feedback(EV_FF)을 지원하는지 확인한다.
        // EVIOCGBIT 커널 비트맵 규격상 네이티브 워드 폭 배열이 필요해 uintptr_t 를 쓴다.
        uintptr_t arrFeatureBits[( FF_MAX + 1 ) / ( sizeof( uintptr_t ) * 8 ) + 1]{};
        if ( ioctl( fd, EVIOCGBIT( EV_FF, sizeof( arrFeatureBits ) ), arrFeatureBits ) < 0 )
        {
            close( fd );
            return;
        }

        // 비트맵을 검사하지 않으면 럼블이 없는 노드도 통과해 setVibration 이 매번 EVIOCSFF 에서 실패한다.
        // 지원 여부는 여기서 한 번에 걸러 낸다.
        if ( LinuxGamepadDeviceInternal::testFeatureBit( arrFeatureBits, FF_RUMBLE ) == false )
        {
            close( fd );
            return;
        }

        _fdForceFeedback = fd;
    }

    void LinuxGamepadDevice::closeForceFeedback()
    {
        if ( _fdForceFeedback >= 0 )
        {
            if ( _ffEffectID >= 0 )
            {
                ioctl( _fdForceFeedback, EVIOCRMFF, _ffEffectID );
                _ffEffectID = invalid_index::kInt16;
            }
            close( _fdForceFeedback );
            _fdForceFeedback = invalid_index::kInt32;
        }
        _ffStrongMagnitude = 0;
        _ffWeakMagnitude   = 0;
        _bHasForceFeedback = SW_FALSE;
    }

    void LinuxGamepadDevice::poll( float32 deltaTime )
    {
        const bool bWasConnected = ( _bConnected == SW_TRUE );

        if ( bWasConnected == false )
        {
            _reconnectTimer += deltaTime;
            if ( _reconnectTimer < 1.0f )
                return;
            _reconnectTimer = 0.0f;

            tryOpenJoystick();
            if ( _bConnected == SW_FALSE )
                return;
        }

        drainJoystickEvents();
    }

    void LinuxGamepadDevice::drainJoystickEvents()
    {
        using Internal = LinuxGamepadDeviceInternal;

        js_event joystickEvent{};
        for ( ;; )
        {
            const ssize_t bytesRead = read( _fdJoystick, &joystickEvent, sizeof( joystickEvent ) );
            if ( bytesRead != static_cast<ssize_t>( sizeof( joystickEvent ) ) )
            {
                if ( bytesRead < 0 && errno != EAGAIN )
                {
                    // 장치가 뽑혔거나(ENODEV) 그 밖의 읽기 오류다. 연결 해제로 전환한다.
                    closeForceFeedback();
                    closeJoystick();
                    _bConnected     = SW_FALSE;
                    _reconnectTimer = 0.0f;
                    if ( _onConnectionChanged.isBound() )
                        _onConnectionChanged( _deviceIndex, false );
                }
                break;
            }

            const uint8 type = joystickEvent.type & static_cast<uint8>( ~JS_EVENT_INIT );
            if ( type == JS_EVENT_BUTTON )
            {
                const GamepadButton button = Internal::mapJsButtonIndex( joystickEvent.number );
                if ( button != GamepadButton::Count )
                    setButtonDown( button, joystickEvent.value != 0 );
            }
            else if ( type == JS_EVENT_AXIS )
            {
                switch ( joystickEvent.number )
                {
                    case Internal::kAxisLeftX:
                    {
                        setAxis( 0, Internal::normalizeStickAxis( joystickEvent.value, false ) );
                        break;
                    }
                    case Internal::kAxisLeftY:
                    {
                        setAxis( 1, Internal::normalizeStickAxis( joystickEvent.value, true ) );
                        break;
                    }
                    case Internal::kAxisRightX:
                    {
                        setAxis( 2, Internal::normalizeStickAxis( joystickEvent.value, false ) );
                        break;
                    }
                    case Internal::kAxisRightY:
                    {
                        setAxis( 3, Internal::normalizeStickAxis( joystickEvent.value, true ) );
                        break;
                    }
                    case Internal::kAxisLeftTrigger:
                    {
                        setAxis( 4, Internal::normalizeTriggerAxis( joystickEvent.value ) );
                        break;
                    }
                    case Internal::kAxisRightTrigger:
                    {
                        setAxis( 5, Internal::normalizeTriggerAxis( joystickEvent.value ) );
                        break;
                    }
                    case Internal::kAxisDPadX:
                    {
                        setButtonDown( GamepadButton::DPadLeft, joystickEvent.value < 0 );
                        setButtonDown( GamepadButton::DPadRight, joystickEvent.value > 0 );
                        break;
                    }
                    case Internal::kAxisDPadY:
                    {
                        setButtonDown( GamepadButton::DPadUp, joystickEvent.value < 0 );
                        setButtonDown( GamepadButton::DPadDown, joystickEvent.value > 0 );
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
        }
    }

    bool LinuxGamepadDevice::setVibration( float32 leftMotor, float32 rightMotor )
    {
        _leftMotorSpeed  = leftMotor;
        _rightMotorSpeed = rightMotor;

        if ( _fdForceFeedback < 0 )
            return true;

        const uint16 strongMagnitude = static_cast<uint16>( MathUtil::clamp( leftMotor, 0.0f, 1.0f ) * 0xFFFFu );
        const uint16 weakMagnitude   = static_cast<uint16>( MathUtil::clamp( rightMotor, 0.0f, 1.0f ) * 0xFFFFu );

        // 같은 세기를 다시 요청하면 할 일이 없다. 게임이 매 프레임 같은 값으로 부르는 경우가 흔한데,
        // 아래 경로는 호출마다 ioctl 두 번과 write 한 번을 쓰므로 그대로 두면 프레임마다 그 비용을 낸다.
        // (XInput 의 XInputSetState 는 값싼 호출이라 Windows 에는 없던 문제다.)
        const bool bForceFeedbackUnchanged = _bHasForceFeedback == SW_TRUE && strongMagnitude == _ffStrongMagnitude && weakMagnitude == _ffWeakMagnitude;
        if ( bForceFeedbackUnchanged )
            return true;

        // evdev FF 이펙트는 세기를 직접 갱신하는 API 가 없어, 지우고 다시 만든다.
        if ( _ffEffectID >= 0 )
        {
            ioctl( _fdForceFeedback, EVIOCRMFF, _ffEffectID );
            _ffEffectID = invalid_index::kInt16;
        }

        if ( strongMagnitude == 0 && weakMagnitude == 0 )
        {
            _ffStrongMagnitude = 0;
            _ffWeakMagnitude   = 0;
            _bHasForceFeedback = SW_FALSE;
            return true;
        }

        ff_effect effect{};
        effect.type                      = FF_RUMBLE;
        effect.id                        = -1; // -1 = 새 이펙트 슬롯 할당 요청
        effect.u.rumble.strong_magnitude = strongMagnitude;
        effect.u.rumble.weak_magnitude   = weakMagnitude;
        effect.replay.length             = 0; // 0 = stop 이벤트를 받을 때까지 계속 재생
        effect.replay.delay              = 0;

        if ( ioctl( _fdForceFeedback, EVIOCSFF, &effect ) < 0 )
        {
            _bHasForceFeedback = SW_FALSE;
            return false;
        }
        _ffEffectID = effect.id;

        input_event playEvent{};
        playEvent.type  = EV_FF;
        playEvent.code  = static_cast<uint16>( _ffEffectID );
        playEvent.value = 1; // 재생 시작
        if ( write( _fdForceFeedback, &playEvent, sizeof( playEvent ) ) < 0 )
        {
            _bHasForceFeedback = SW_FALSE;
            return false;
        }

        _ffStrongMagnitude = strongMagnitude;
        _ffWeakMagnitude   = weakMagnitude;
        _bHasForceFeedback = SW_TRUE;
        return true;
    }

    void LinuxGamepadDevice::stopVibration()
    {
        GamepadDevice::stopVibration();

        if ( _fdForceFeedback >= 0 && _ffEffectID >= 0 )
        {
            ioctl( _fdForceFeedback, EVIOCRMFF, _ffEffectID );
            _ffEffectID = invalid_index::kInt16;
        }
        _ffStrongMagnitude = 0;
        _ffWeakMagnitude   = 0;
        _bHasForceFeedback = SW_FALSE;
    }
} // namespace sw

#endif
