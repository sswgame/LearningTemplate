/**
 * @file GamepadDevice.h
 * @brief 모든 플랫폼 게임패드 구현이 함께 쓰는 추상 기반 클래스입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"

namespace sw
{
    enum class GamepadBatteryType : uint8
    {
        Disconnected = 0,
        Wired,
        Alkaline,
        Nimh,
        Unknown
    };

    enum class GamepadBatteryLevel : uint8
    {
        Empty = 0,
        Low,
        Medium,
        Full
    };

    struct GamepadBatteryInfo
    {
        GamepadBatteryType  _type{ GamepadBatteryType::Unknown };
        GamepadBatteryLevel _level{ GamepadBatteryLevel::Empty };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GamepadDevice
     * @brief 게임패드 버튼 · 아날로그 스틱 · 트리거 압력 · 럼블 진동 인터페이스를 정의하는 추상 기반 클래스입니다.
     */
    class SW_API GamepadDevice : public IInputDevice
    {
    public:
        using GamepadConnectionDelegate = Delegate<void( uint32, bool )>;

        explicit GamepadDevice( uint32 deviceIndex = 0 );
        virtual ~GamepadDevice() override = default;

        GamepadDevice( const GamepadDevice& )            = delete;
        GamepadDevice& operator=( const GamepadDevice& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) IInputDevice 수명주기
        // ------------------------------------------------------------------------------
        InputDeviceKind            getDeviceKind() const override { return InputDeviceKind::Gamepad; }
        string_view                getDeviceName() const override { return "Gamepad"; }
        uint32                     getDeviceIndex() const override { return _deviceIndex; }
        virtual GamepadBatteryInfo getBatteryInfo() const { return GamepadBatteryInfo{}; }

        void onFrameBegin( float32 deltaTime ) override;
        void onPolled() override;
        void onFrameEnd() override;
        void resetState() override;
        /** @brief 연결 여부입니다. 가상 세션 동안은 가상 연결 사건이, 아니면 장치(폴링)가 정합니다(`isHardwareConnected`). */
        bool isConnected() const final { return _bVirtualSession == SW_TRUE ? _bVirtualConnected == SW_TRUE : isHardwareConnected(); }

        bool    isControlDown( uint16 controlIndex ) const override;
        bool    wasControlPressed( uint16 controlIndex ) const override;
        bool    wasControlReleased( uint16 controlIndex ) const override;
        float32 getControlValue( uint16 controlIndex ) const override;

        // ------------------------------------------------------------------------------
        // 2) 게임패드 전용 쿼리 · 햅틱 진동
        // ------------------------------------------------------------------------------
        bool isButtonDown( GamepadButton button ) const;
        bool wasButtonPressed( GamepadButton button ) const;
        bool wasButtonReleased( GamepadButton button ) const;
        bool wasAnyButtonPressed() const { return ( _buttonMask & ~_prevButtonMask ) != 0; }

        float2  getLeftStick() const { return _leftStick; }
        float2  getRightStick() const { return _rightStick; }
        float32 getLeftTrigger() const { return _leftTrigger; }
        float32 getRightTrigger() const { return _rightTrigger; }
        float32 getLeftMotorVibration() const { return _leftMotorSpeed; }
        float32 getRightMotorVibration() const { return _rightMotorSpeed; }

        virtual bool setVibration( float32 leftMotor, float32 rightMotor )
        {
            _leftMotorSpeed  = leftMotor;
            _rightMotorSpeed = rightMotor;
            return true;
        }
        virtual void stopVibration()
        {
            _bTimedVibrationActive  = SW_FALSE;
            _vibrationDurationTimer = 0.0f;
            setVibration( 0.0f, 0.0f );
        }

        bool playVibration( float32 leftMotor, float32 rightMotor, float32 durationSeconds )
        {
            if ( durationSeconds <= 0.0f )
            {
                stopVibration();
                return true;
            }
            const bool bOk = setVibration( leftMotor, rightMotor );
            if ( bOk )
            {
                _vibrationDurationTimer = durationSeconds;
                _bTimedVibrationActive  = SW_TRUE;
            }
            return bOk;
        }

        void    setTriggerDeadzone( float32 deadzone ) { _triggerDeadzone = deadzone; }
        float32 getTriggerDeadzone() const { return _triggerDeadzone; }
        bool    isLeftTriggerDown( float32 threshold = 0.5f ) const { return _leftTrigger >= threshold; }
        bool    isRightTriggerDown( float32 threshold = 0.5f ) const { return _rightTrigger >= threshold; }

        void setButtonDown( GamepadButton button, bool bDown );
        void setAxis( uint16 axisIndex, float32 value );
        void setConnectionCallback( GamepadConnectionDelegate callback ) { _onConnectionChanged = std::move( callback ); }

        /**
         * @brief 배타 가상 입력이 붙거나(true) 떨어졌습니다(false). 상태를 지웁니다.
         * @details 가상 세션 동안 연결 여부는 폴링이 아니라 가상 연결 사건(`setVirtualConnected`)이 정합니다 — 처음은 끊김입니다.
         */
        void setVirtualSession( bool bVirtual );
        /** @brief 가상 연결 사건을 적용합니다(`InputManager` 가 가상 원천의 `GamepadConnectionChanged` 를 재생할 때). */
        void setVirtualConnected( bool bConnected ) { _bVirtualConnected = bConnected ? SW_TRUE : SW_FALSE; }
        bool isInVirtualSession() const { return _bVirtualSession == SW_TRUE; }

        /** @brief 연결 콜백의 스텁이 범위 안이면 풉니다. */
        uint32 releaseCodeWithin( const void* pBegin, const void* pEnd ) override;

    protected:
        /** @brief 실제 장치가 연결돼 있는지입니다(플랫폼 백엔드가 폴링 결과로 답한다). */
        virtual bool isHardwareConnected() const { return true; }

        GamepadConnectionDelegate _onConnectionChanged;
        uint32                    _deviceIndex;               /**< 컨트롤러 슬롯 인덱스(로컬 멀티플레이어 0~3번 패드). */
        uint32                    _buttonMask;                /**< 이번 프레임의 디지털 버튼 눌림 비트마스크(GamepadButton 인덱스로 비트 조회). */
        uint32                    _prevButtonMask;            /**< 직전 프레임의 버튼 비트마스크. wasButtonPressed/Released 의 엣지 판정에 씀. */
        float2                    _leftStick;                 /**< 왼쪽 스틱 [-1.0, 1.0](데드존 미적용 원시값). */
        float2                    _rightStick;                /**< 오른쪽 스틱 [-1.0, 1.0]. */
        float32                   _leftTrigger;               /**< 왼쪽 트리거 압력 [0.0, 1.0]. 모든 입력 경로가 `setAxis` 로 넣어 _triggerDeadzone 을 거침. */
        float32                   _rightTrigger;              /**< 오른쪽 트리거 압력 [0.0, 1.0]. */
        float32                   _prevLeftTrigger;           /**< 직전 프레임의 왼쪽 트리거 값. wasControlPressed/Released 의 임계값(0.5) 판정에 씀. */
        float32                   _prevRightTrigger;          /**< 직전 프레임의 오른쪽 트리거 값. */
        float32                   _leftMotorSpeed;            /**< 마지막으로 설정한 왼쪽(저주파) 진동 모터 세기 [0.0, 1.0]. */
        float32                   _rightMotorSpeed;           /**< 마지막으로 설정한 오른쪽(고주파) 진동 모터 세기 [0.0, 1.0]. */
        float32                   _vibrationDurationTimer;    /**< playVibration() 으로 시작한 타이머 진동의 남은 시간(초). 0 이하가 되면 저절로 멈춤. */
        float32                   _triggerDeadzone;           /**< 트리거 축 노이즈를 거르는 데드존. 이 값 미만이면 0 으로 취급(디지털 눌림 판정용 0.5 임계값과는 별개). `setAxis` 가 적용함. */
        uint8                     _bTimedVibrationActive : 1; /**< playVibration() 으로 시작한 타이머 진동이 진행 중인지 여부. */
        uint8                     _bSuppressEdgeOnce     : 1; /**< resetState() 뒤 첫 폴링 값을 "직전 값" 으로 삼아, 누르고 있던 버튼이 새로 눌린 것처럼 보이지 않게 함. */
        uint8                     _bVirtualSession       : 1; /**< 배타 가상 입력 중 — 연결 여부를 `_bVirtualConnected` 가 정한다. */
        uint8                     _bVirtualConnected     : 1; /**< 가상 세션의 연결 여부(가상 연결 사건이 정한다). */
        [[maybe_unused]] uint8    _reserved              : 4;
    };
} // namespace sw
