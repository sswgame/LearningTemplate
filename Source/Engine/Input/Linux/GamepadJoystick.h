/**
 * @file GamepadJoystick.h
 * @brief Linux 커널 조이스틱 API(/dev/input/jsN) 기반 게임패드 구현입니다(GamepadDevice 상속).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Input/Devices/GamepadDevice.h"

namespace sw
{
    /**
     * @class GamepadJoystick
     * @brief Linux 커널 조이스틱 API(/dev/input/jsN)로 하드웨어 게임패드를 폴링하는 GamepadDevice 구현입니다.
     *
     * @note 버튼 · 축 배치는 Xbox 호환 컨트롤러의 xpad 드라이버 규격을 기준으로 매핑합니다.
     *       다른 드라이버 · 컨트롤러는 배치가 다를 수 있으므로(SDL 의 게임패드 매핑 DB 같은 보정이 없습니다)
     *       실제 하드웨어에서 확인한 뒤 필요하면 GamepadJoystick.cpp 의 kAxisXxx · 버튼 인덱스를 조정하십시오.
     * @note 커널 조이스틱 API 자체는 럼블(force feedback)을 지원하지 않습니다. setVibration() 은
     *       같은 물리 장치의 evdev(/dev/input/eventN) 노드를 sysfs 에서 찾아 EV_FF 로 시도하고,
     *       찾지 못하거나 실패하면 조용히 false 를 반환합니다.
     */
    class SW_API GamepadJoystick : public GamepadDevice
    {
    public:
        explicit GamepadJoystick( uint32 userIndex = 0 );
        virtual ~GamepadJoystick() override;

        GamepadJoystick( const GamepadJoystick& )            = delete;
        GamepadJoystick& operator=( const GamepadJoystick& ) = delete;

        void poll( float32 deltaTime ) override;

        bool isConnected() const override { return _bConnected == SW_TRUE; }

        bool setVibration( float32 leftMotor, float32 rightMotor ) override;
        void stopVibration() override;

    private:
        /** @brief /dev/input/js{_deviceIndex} 를 열어 봅니다(실패하면 다음 poll 에서 재시도 타이머로 다시 시도합니다). */
        void tryOpenJoystick();
        /** @brief 열려 있는 조이스틱 fd를 닫고 연결 해제 상태로 전환합니다. */
        void closeJoystick();
        /** @brief non-blocking read 로 대기 중인 js_event 를 모두 꺼내며 버튼 · 축 상태를 갱신합니다. */
        void drainJoystickEvents();
        /** @brief sysfs에서 같은 장치의 evdev 노드를 찾아 force-feedback fd를 엽니다. */
        void tryOpenForceFeedback();
        /** @brief force-feedback fd를 닫고 관련 상태를 리셋합니다. */
        void closeForceFeedback();

        int32                  _fdJoystick;        /**< /dev/input/jsN 파일 디스크립터. 연결 전이면 invalid_index::kInt32(POSIX open() 실패 규약과 같은 -1). */
        int32                  _fdForceFeedback;   /**< 대응하는 /dev/input/eventN 파일 디스크립터(럼블용). 없으면 invalid_index::kInt32. */
        int16                  _ffEffectId;        /**< ioctl( EVIOCSFF ) 로 올린 FF_RUMBLE 이펙트 ID. 없으면 invalid_index::kInt16. */
        uint16                 _ffStrongMagnitude; /**< 마지막으로 올린 강모터 세기. 같은 값이면 다시 올리지 않음. */
        uint16                 _ffWeakMagnitude;   /**< 마지막으로 올린 약모터 세기. */
        float32                _reconnectTimer;    /**< 연결되지 않은 상태에서 다시 열기까지 남은 시간(초). XInput 과 같이 폴링 남발을 막음. */
        uint8                  _bConnected        : 1;
        uint8                  _bHasForceFeedback : 1; /**< _fdForceFeedback 가 유효하고 이펙트 업로드까지 성공했는지 여부. */
        [[maybe_unused]] uint8 _reservedPad       : 6;
    };
} // namespace sw
