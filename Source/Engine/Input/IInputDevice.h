/**
 * @file IInputDevice.h
 * @brief 상용 엔진급 다형적 입력 장치 추상 인터페이스 및 범용 InputSlot 스키마
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/KeyCodes.h"

namespace sw
{
    /** @brief 입력 장치 종류 */
    enum class InputDeviceKind : uint8
    {
        Keyboard = 0,
        Mouse,
        Gamepad,
        Touch,
        Custom,
        Count
    };

    /**
     * @struct InputSlot
     * @brief 장치 종류, 장치 인덱스(4인 로컬 게임패드 등) 및 장치 내 컨트롤 인덱스를 통합 식별하는 무분기 입력 경로
     */
    struct InputSlot
    {
        InputDeviceKind _deviceKind{ InputDeviceKind::Keyboard };
        uint8           _deviceIndex{ 0 };
        uint16          _controlIndex{ 0 };

        static constexpr InputSlot fromKey( Key key ) noexcept
        {
            return InputSlot{ InputDeviceKind::Keyboard, 0, static_cast<uint16>( key ) };
        }

        static constexpr InputSlot fromMouseButton( MouseButton button ) noexcept
        {
            return InputSlot{ InputDeviceKind::Mouse, 0, static_cast<uint16>( button ) };
        }

        static constexpr InputSlot fromGamepadButton( GamepadButton button, uint8 padIndex = 0 ) noexcept
        {
            return InputSlot{ InputDeviceKind::Gamepad, padIndex, static_cast<uint16>( button ) };
        }

        static constexpr InputSlot fromCustom( InputDeviceKind kind, uint16 index, uint8 deviceIndex = 0 ) noexcept
        {
            return InputSlot{ kind, deviceIndex, index };
        }

        bool operator==( const InputSlot& other ) const noexcept
        {
            return _deviceKind == other._deviceKind && _deviceIndex == other._deviceIndex && _controlIndex == other._controlIndex;
        }

        bool operator!=( const InputSlot& other ) const noexcept
        {
            return !( *this == other );
        }

        bool operator<( const InputSlot& other ) const noexcept
        {
            if ( _deviceKind != other._deviceKind )
                return _deviceKind < other._deviceKind;
            if ( _deviceIndex != other._deviceIndex )
                return _deviceIndex < other._deviceIndex;
            return _controlIndex < other._controlIndex;
        }
    };

    /**
     * @class IInputDevice
     * @brief 모든 하드웨어 및 가상 입력 장치가 구현해야 하는 추상 기본 인터페이스
     */
    class SW_API IInputDevice
    {
    public:
        IInputDevice()          = default;
        virtual ~IInputDevice() = default;

        IInputDevice( const IInputDevice& )            = delete;
        IInputDevice& operator=( const IInputDevice& ) = delete;

        virtual InputDeviceKind getDeviceKind() const = 0;
        virtual string_view     getDeviceName() const = 0;
        virtual uint32          getDeviceIndex() const { return 0; }
        virtual bool            isConnected() const { return true; }

        /** @brief OS 원시 이벤트 또는 하드웨어 API 폴링을 수행합니다. */
        virtual void poll( float32 deltaTime ) = 0;
        /** @brief 새 프레임 시작 시 이번 프레임 임시 상태(Pressed/Released)를 준비합니다. */
        virtual void onFrameBegin( float32 deltaTime ) = 0;
        /** @brief 프레임 종료 시 정리를 수행합니다. */
        virtual void onFrameEnd() = 0;
        /** @brief 창 포커스 아웃 등으로 모든 눌림 상태를 강제 초기화합니다. */
        virtual void resetState() = 0;

        /** @brief 컨트롤이 현재 눌린 상태인지 반환합니다. */
        virtual bool isControlDown( uint16 controlIndex ) const = 0;
        /** @brief 컨트롤이 이번 프레임에 눌렸는지 반환합니다. */
        virtual bool wasControlPressed( uint16 controlIndex ) const = 0;
        /** @brief 컨트롤이 이번 프레임에 떼어졌는지 반환합니다. */
        virtual bool wasControlReleased( uint16 controlIndex ) const = 0;
        /** @brief 아날로그 축이나 압력 값을 반환합니다 (기본 0.0 ~ 1.0f). */
        virtual float32 getControlValue( uint16 controlIndex ) const { return isControlDown( controlIndex ) ? 1.0f : 0.0f; }
    };
} // namespace sw
