/**
 * @file GamepadJoystick.h
 * @brief Linux 커널 조이스틱 API(/dev/input/jsN) 기반 게임패드 구현체 (GamepadDevice 상속)
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
     * @brief Linux 커널 조이스틱 API(/dev/input/jsN)를 통해 하드웨어 게임패드를 폴링하는 GamepadDevice 구현체
     *
     * @note 버튼/축 배치는 Xbox 호환 컨트롤러의 xpad 드라이버 규격을 기준으로 매핑합니다.
     *       다른 드라이버/컨트롤러는 배치가 다를 수 있어(SDL의 게임패드 매핑 DB 같은 보정이 없음)
     *       실제 하드웨어에서 검증 후 필요하면 GamepadJoystick.cpp의 kAxisXxx/버튼 인덱스를 조정하세요.
     * @note 커널 조이스틱 API 자체는 럼블(force feedback)을 지원하지 않습니다. setVibration()은
     *       같은 물리 장치의 evdev(/dev/input/eventN) 노드를 sysfs에서 찾아 EV_FF로 시도하고,
     *       찾지 못하거나 실패하면 조용히 false를 반환합니다.
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
        /** @brief /dev/input/js{_deviceIndex}를 열어봅니다 (실패하면 다음 poll에서 재시도 타이머로 재시도). */
        void tryOpenJoystick();
        /** @brief 열려 있는 조이스틱 fd를 닫고 연결 해제 상태로 전환합니다. */
        void closeJoystick();
        /** @brief non-blocking read로 대기 중인 js_event들을 모두 소진하며 버튼/축 상태를 갱신합니다. */
        void drainJoystickEvents();
        /** @brief sysfs에서 같은 장치의 evdev 노드를 찾아 force-feedback fd를 엽니다. */
        void tryOpenForceFeedback();
        /** @brief force-feedback fd를 닫고 관련 상태를 리셋합니다. */
        void closeForceFeedback();

        int32                  _fdJoystick;      /**< /dev/input/jsN 파일 디스크립터. 미연결이면 invalid_index::kInt32(POSIX open() 실패 규약과 동일한 -1). */
        int32                  _fdForceFeedback; /**< 대응하는 /dev/input/eventN 파일 디스크립터 (럼블용). 없으면 invalid_index::kInt32. */
        int16                  _ffEffectId;      /**< ioctl( EVIOCSFF )로 업로드한 FF_RUMBLE 이펙트 ID. 없으면 invalid_index::kInt16. */
        float32                _reconnectTimer;  /**< 미연결 상태에서 재오픈 시도까지 남은 시간(초). XInput과 동일하게 폴링 스팸을 막음. */
        uint8                  _bConnected        : 1;
        uint8                  _bHasForceFeedback : 1; /**< _fdForceFeedback가 유효하고 이펙트 업로드까지 성공했는지. */
        [[maybe_unused]] uint8 _reservedPad       : 6;
    };
} // namespace sw
