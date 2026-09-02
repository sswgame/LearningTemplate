#include "pch.h"

#include "Engine/Input/Linux/GamepadJoystick.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Input/GamepadButtons.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"

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
         * @brief Xbox 호환(xpad 드라이버) 조이스틱 축/버튼 인덱스 상수.
         * @note 실제 하드웨어에 따라 인덱스가 다를 수 있습니다 — SDL의 게임패드 매핑 DB 같은
         *       기기별 보정은 하지 않는, 가장 흔한 xpad 배치를 기준으로 한 최선 추정치입니다.
         */
        struct GamepadJoystickInternal
        {
            static constexpr uint8 kAxisLeftX        = 0;
            static constexpr uint8 kAxisLeftY        = 1;
            static constexpr uint8 kAxisLeftTrigger  = 2;
            static constexpr uint8 kAxisRightX       = 3;
            static constexpr uint8 kAxisRightY       = 4;
            static constexpr uint8 kAxisRightTrigger = 5;
            static constexpr uint8 kAxisDPadX        = 6;
            static constexpr uint8 kAxisDPadY        = 7;

            static constexpr uint8 kButtonA             = 0;
            static constexpr uint8 kButtonB             = 1;
            static constexpr uint8 kButtonX             = 2;
            static constexpr uint8 kButtonY             = 3;
            static constexpr uint8 kButtonLeftShoulder  = 4;
            static constexpr uint8 kButtonRightShoulder = 5;
            static constexpr uint8 kButtonBack          = 6;
            static constexpr uint8 kButtonStart         = 7;
            static constexpr uint8 kButtonGuide         = 8; ///< GamepadButton에 대응값 없음 (무시).
            static constexpr uint8 kButtonLeftThumb     = 9;
            static constexpr uint8 kButtonRightThumb    = 10;

            /** @brief js_event 축 값(-32767~32767)을 [-1, 1] 스틱 축으로 정규화합니다. bInvertY면 부호를 뒤집습니다. */
            static float32 normalizeStickAxis( int16 rawValue, bool bInvert )
            {
                float32 normalized = static_cast<float32>( rawValue ) / 32767.0f;
                if ( bInvert )
                    normalized = -normalized;
                return MathUtil::clamp( normalized, -1.0f, 1.0f );
            }

            /** @brief js_event 축 값(-32767~32767)을 [0, 1] 트리거 압력으로 정규화합니다 (rest=-32767 가정). */
            static float32 normalizeTriggerAxis( int16 rawValue )
            {
                const float32 normalized = ( static_cast<float32>( rawValue ) + 32767.0f ) / 65534.0f;
                return MathUtil::clamp( normalized, 0.0f, 1.0f );
            }

            /** @brief GamepadButton::A/B/X/Y/... enum에 대응하는 js_event 버튼 인덱스인지 변환합니다. */
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
                        return GamepadButton::Count; // Guide 등 대응 없음
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GamepadJoystick::GamepadJoystick( uint32 userIndex )
        : GamepadDevice{ userIndex }
        , _fdJoystick{ invalid_index::kInt32 }
        , _fdForceFeedback{ invalid_index::kInt32 }
        , _ffEffectId{ invalid_index::kInt16 }
        , _reconnectTimer{ 1.0f }
        , _bConnected{ SW_FALSE }
        , _bHasForceFeedback{ SW_FALSE }
        , _reservedPad{ 0 }
    {
    }

    GamepadJoystick::~GamepadJoystick()
    {
        closeForceFeedback();
        closeJoystick();
    }

    void GamepadJoystick::tryOpenJoystick()
    {
        StringBuilder<constant::kMaxPathSize> pathBuilder;
        pathBuilder.appendFormat( "/dev/input/js%#", _deviceIndex );

        const int32 fd = open( pathBuilder.c_str(), O_RDONLY | O_NONBLOCK );
        if ( fd < 0 )
        {
            _bConnected = SW_FALSE;
            return;
        }

        _fdJoystick = fd;
        _bConnected = SW_TRUE;
        tryOpenForceFeedback();

        if ( _onConnectionChanged.isBound() )
            _onConnectionChanged( _deviceIndex, true );
    }

    void GamepadJoystick::closeJoystick()
    {
        if ( _fdJoystick >= 0 )
        {
            close( _fdJoystick );
            _fdJoystick = invalid_index::kInt32;
        }
    }

    void GamepadJoystick::tryOpenForceFeedback()
    {
        // /sys/class/input/jsN/device/ 는 실제 입력 장치 디렉터리를 가리키고, 같은 물리 컨트롤러의
        // evdev 노드(eventM)가 그 안에 형제 항목으로 존재합니다. 이를 찾아 /dev/input/eventM을 엽니다.
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

        // 이 evdev 노드가 실제로 force-feedback(EV_FF)을 지원하는지 확인합니다.
        // EVIOCGBIT 커널 비트맵 규격상 네이티브 word 폭 배열이 필요해 uintptr_t를 씁니다.
        uintptr_t arrFeatureBits[( FF_MAX + 1 ) / ( sizeof( uintptr_t ) * 8 ) + 1]{};
        if ( ioctl( fd, EVIOCGBIT( EV_FF, sizeof( arrFeatureBits ) ), arrFeatureBits ) < 0 )
        {
            close( fd );
            return;
        }

        _fdForceFeedback = fd;
    }

    void GamepadJoystick::closeForceFeedback()
    {
        if ( _fdForceFeedback >= 0 )
        {
            if ( _ffEffectId >= 0 )
            {
                ioctl( _fdForceFeedback, EVIOCRMFF, _ffEffectId );
                _ffEffectId = invalid_index::kInt16;
            }
            close( _fdForceFeedback );
            _fdForceFeedback = invalid_index::kInt32;
        }
        _bHasForceFeedback = SW_FALSE;
    }

    void GamepadJoystick::poll( float32 deltaTime )
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

    void GamepadJoystick::drainJoystickEvents()
    {
        using Internal = GamepadJoystickInternal;

        js_event evt{};
        for ( ;; )
        {
            const ssize_t bytesRead = read( _fdJoystick, &evt, sizeof( evt ) );
            if ( bytesRead != static_cast<ssize_t>( sizeof( evt ) ) )
            {
                if ( bytesRead < 0 && errno != EAGAIN )
                {
                    // 장치가 뽑혔거나(ENODEV) 그 밖의 읽기 오류 — 연결 해제로 전환합니다.
                    closeForceFeedback();
                    closeJoystick();
                    _bConnected     = SW_FALSE;
                    _reconnectTimer = 0.0f;
                    if ( _onConnectionChanged.isBound() )
                        _onConnectionChanged( _deviceIndex, false );
                }
                break;
            }

            const uint8 type = evt.type & static_cast<uint8>( ~JS_EVENT_INIT );
            if ( type == JS_EVENT_BUTTON )
            {
                const GamepadButton button = Internal::mapJsButtonIndex( evt.number );
                if ( button != GamepadButton::Count )
                    setButtonDown( button, evt.value != 0 );
            }
            else if ( type == JS_EVENT_AXIS )
            {
                switch ( evt.number )
                {
                    case Internal::kAxisLeftX:
                        setAxis( 0, Internal::normalizeStickAxis( evt.value, false ) );
                        break;
                    case Internal::kAxisLeftY:
                        setAxis( 1, Internal::normalizeStickAxis( evt.value, true ) );
                        break;
                    case Internal::kAxisRightX:
                        setAxis( 2, Internal::normalizeStickAxis( evt.value, false ) );
                        break;
                    case Internal::kAxisRightY:
                        setAxis( 3, Internal::normalizeStickAxis( evt.value, true ) );
                        break;
                    case Internal::kAxisLeftTrigger:
                        setAxis( 4, Internal::normalizeTriggerAxis( evt.value ) );
                        break;
                    case Internal::kAxisRightTrigger:
                        setAxis( 5, Internal::normalizeTriggerAxis( evt.value ) );
                        break;
                    case Internal::kAxisDPadX:
                        setButtonDown( GamepadButton::DPadLeft, evt.value < 0 );
                        setButtonDown( GamepadButton::DPadRight, evt.value > 0 );
                        break;
                    case Internal::kAxisDPadY:
                        setButtonDown( GamepadButton::DPadUp, evt.value < 0 );
                        setButtonDown( GamepadButton::DPadDown, evt.value > 0 );
                        break;
                    default:
                        break;
                }
            }
        }
    }

    bool GamepadJoystick::setVibration( float32 leftMotor, float32 rightMotor )
    {
        _leftMotorSpeed  = leftMotor;
        _rightMotorSpeed = rightMotor;

        if ( _fdForceFeedback < 0 )
            return true;

        // 매번 기존 이펙트를 지우고 새 세기로 다시 업로드합니다 (evdev FF 이펙트는 세기를 직접
        // 갱신하는 API가 없어, 지우고 다시 만드는 편이 가장 단순하고 안전합니다).
        if ( _ffEffectId >= 0 )
        {
            ioctl( _fdForceFeedback, EVIOCRMFF, _ffEffectId );
            _ffEffectId = invalid_index::kInt16;
        }

        const bool bBothZero = ( leftMotor <= 0.0f && rightMotor <= 0.0f );
        if ( bBothZero )
        {
            _bHasForceFeedback = SW_FALSE;
            return true;
        }

        ff_effect effect{};
        effect.type                      = FF_RUMBLE;
        effect.id                        = -1; // -1 = 새 이펙트 슬롯 할당 요청
        effect.u.rumble.strong_magnitude = static_cast<uint16>( MathUtil::clamp( leftMotor, 0.0f, 1.0f ) * 0xFFFFu );
        effect.u.rumble.weak_magnitude   = static_cast<uint16>( MathUtil::clamp( rightMotor, 0.0f, 1.0f ) * 0xFFFFu );
        effect.replay.length             = 0; // 0 = stop 이벤트를 받을 때까지 재생 지속
        effect.replay.delay              = 0;

        if ( ioctl( _fdForceFeedback, EVIOCSFF, &effect ) < 0 )
        {
            _bHasForceFeedback = SW_FALSE;
            return false;
        }
        _ffEffectId = effect.id;

        input_event playEvent{};
        playEvent.type  = EV_FF;
        playEvent.code  = static_cast<uint16>( _ffEffectId );
        playEvent.value = 1; // 재생 시작
        if ( write( _fdForceFeedback, &playEvent, sizeof( playEvent ) ) < 0 )
        {
            _bHasForceFeedback = SW_FALSE;
            return false;
        }

        _bHasForceFeedback = SW_TRUE;
        return true;
    }

    void GamepadJoystick::stopVibration()
    {
        GamepadDevice::stopVibration();

        if ( _fdForceFeedback >= 0 && _ffEffectId >= 0 )
        {
            ioctl( _fdForceFeedback, EVIOCRMFF, _ffEffectId );
            _ffEffectId = invalid_index::kInt16;
        }
        _bHasForceFeedback = SW_FALSE;
    }
} // namespace sw

#endif
