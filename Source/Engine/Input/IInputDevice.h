/**
 * @file IInputDevice.h
 * @brief 입력 장치의 다형 추상 인터페이스와 범용 InputSlot 스키마입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/KeyCodeUtil.h"

namespace sw
{
    /** @brief 입력 장치 종류입니다. */
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
     * @brief 장치 종류 · 장치 인덱스(4인 로컬 게임패드 등) · 장치 안 컨트롤 인덱스를 하나로 묶어 식별하는 분기 없는 입력 경로입니다.
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
} // namespace sw

namespace sw
{
    /**
     * @class IInputDevice
     * @brief 모든 하드웨어 · 가상 입력 장치가 구현해야 하는 추상 기반 인터페이스입니다.
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

        /** @brief OS 원시 이벤트를 받거나 하드웨어 API 를 폴링합니다. */
        virtual void poll( float32 deltaTime ) = 0;
        /**
         * @brief 이번 프레임의 폴링이 끝났습니다(`onFrameBegin` → `poll` → 이것 순서).
         * @details 폴링으로 상태를 읽는 장치가 "리셋 직후 처음 읽은 값" 을 이전 값으로 삼아 가짜 엣지를 막는 자리입니다.
         */
        virtual void onPolled() {}
        /**
         * @brief 이번 프레임의 원시 이벤트를 모두 적용했습니다(`onPolled` 와 이벤트 적용 뒤, 프레임당 한 번).
         * @details 이벤트마다가 아니라 프레임에 모인 결과로 한 번 계산해야 하는 것(마우스 스무딩)을 여기서 합니다 — 이벤트마다 하면
         *          결과가 폴링 레이트에 따라 달라집니다.
         */
        virtual void onEventsDispatched( float32 deltaTime ) { (void)deltaTime; }
        /** @brief 새 프레임을 시작할 때 이번 프레임의 임시 상태(Pressed/Released)를 준비합니다. */
        virtual void onFrameBegin( float32 deltaTime ) = 0;
        /** @brief 프레임을 마칠 때 정리합니다. */
        virtual void onFrameEnd() = 0;
        /** @brief 창 포커스를 잃는 등의 이유로 모든 눌림 상태를 강제로 초기화합니다. */
        virtual void resetState() = 0;

        /** @brief 컨트롤이 현재 눌린 상태인지 반환합니다. */
        virtual bool isControlDown( uint16 controlIndex ) const = 0;
        /** @brief 컨트롤이 이번 프레임에 눌렸는지 반환합니다. */
        virtual bool wasControlPressed( uint16 controlIndex ) const = 0;
        /** @brief 컨트롤이 이번 프레임에 떼어졌는지 반환합니다. */
        virtual bool wasControlReleased( uint16 controlIndex ) const = 0;
        /** @brief 아날로그 축이나 압력 값을 반환합니다(기본 0.0 ~ 1.0). */
        virtual float32 getControlValue( uint16 controlIndex ) const { return isControlDown( controlIndex ) ? 1.0f : 0.0f; }

        /**
         * @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 콜백을 풉니다. 푼 수를 반환합니다.
         * @details 모듈 이미지를 내리기 전에 `InputManager` 가 장치마다 부릅니다(`IModuleUnloadListener`). 콜백을 드는 장치만 재정의합니다.
         */
        virtual uint32 releaseCodeWithin( const void* pBegin, const void* pEnd )
        {
            (void)pBegin;
            (void)pEnd;
            return 0;
        }
    };
} // namespace sw
