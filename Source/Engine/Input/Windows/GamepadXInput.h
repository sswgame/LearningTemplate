/**
 * @file GamepadXInput.h
 * @brief Windows XInput 게임패드 구현입니다(GamepadDevice 상속).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/Devices/GamepadDevice.h"

namespace sw
{
    /**
     * @class GamepadXInput
     * @brief Windows XInput API 로 하드웨어 게임패드를 폴링하고 진동을 제어하는 GamepadDevice 구현입니다.
     */
    class SW_API GamepadXInput : public GamepadDevice
    {
    public:
        explicit GamepadXInput( uint32 userIndex = 0 );
        virtual ~GamepadXInput() override = default;

        GamepadXInput( const GamepadXInput& )            = delete;
        GamepadXInput& operator=( const GamepadXInput& ) = delete;

        void poll( float32 deltaTime ) override;
        void pollUser( uint32 userIndex, float32 deltaTime = 0.016f );

        bool               isConnected() const override { return _bConnected == SW_TRUE; }
        GamepadBatteryInfo getBatteryInfo() const override;

        bool setVibration( float32 leftMotor, float32 rightMotor ) override;
        void setVibration( float32 leftMotor, float32 rightMotor, uint32 userIndex );
        void stopVibration() override;

    private:
        /**
         * @brief 연결이 끊긴 상태로 비웁니다(버튼 · 스틱 · 트리거). 이번에 끊겼으면(@p bWasConnected) 연결 콜백에 알립니다.
         * @details `XInputGetState` 를 못 찾았을 때 · 호출이 실패했을 때 · 비 Windows 스텁이 같은 여덟 줄을 각자 들고 있었습니다.
         */
        void markDisconnected( uint32 userIndex, bool bWasConnected );

        [[maybe_unused]] float32 _reconnectTimer;
        uint8                    _bConnected  : 1;
        [[maybe_unused]] uint8   _reservedPad : 7;
    };

} // namespace sw
