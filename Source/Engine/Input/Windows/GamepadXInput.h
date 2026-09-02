/**
 * @file GamepadXInput.h
 * @brief Windows XInput 게임패드 구현체 (GamepadDevice 상속)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/Devices/GamepadDevice.h"

namespace sw
{
    /**
     * @class GamepadXInput
     * @brief Windows XInput API를 통해 하드웨어 게임패드를 폴링하고 진동을 제어하는 GamepadDevice 구현체
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
        float32                _reconnectTimer{ 1.0f };
        uint8                  _bConnected  : 1;
        [[maybe_unused]] uint8 _reservedPad : 7;
    };
} // namespace sw
