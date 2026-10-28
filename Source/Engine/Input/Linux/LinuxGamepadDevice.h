/**
 * @file LinuxGamepadDevice.h
 * @brief Linux 커널 조이스틱 API(/dev/input/jsN) 기반 게임패드 구현입니다(GamepadDevice 상속).
 */
#pragma once
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Input/Devices/GamepadDevice.h"

namespace sw
{
    /**
     * @class LinuxGamepadDevice
     * @brief Linux 커널 조이스틱 API(/dev/input/jsN)로 하드웨어 게임패드를 폴링하는 GamepadDevice 구현입니다.
     *
     * @note 버튼과 축 배치는 Xbox 호환 컨트롤러의 xpad 드라이버 규격을 기준으로 매핑합니다.
     *       다른 드라이버와 컨트롤러는 배치가 다를 수 있으므로(SDL 의 게임패드 매핑 DB 같은 보정이 없습니다)
     *       실제 하드웨어에서 확인한 뒤 필요하면 LinuxGamepadDevice.cpp 의 kAxisXxx 와 버튼 인덱스를 조정하십시오.
     * @note 커널 조이스틱 API 자체는 럼블(force feedback)을 지원하지 않습니다. setVibration() 은
     *       같은 물리 장치의 evdev(/dev/input/eventN) 노드를 sysfs 에서 찾아 EV_FF 로 시도하고,
     *       찾지 못하거나 실패하면 조용히 false 를 반환합니다.
     */
    class SW_API LinuxGamepadDevice : public GamepadDevice
    {
    public:
        explicit LinuxGamepadDevice( uint32 userIndex = 0 );
        virtual ~LinuxGamepadDevice() override;

        LinuxGamepadDevice( const LinuxGamepadDevice& )            = delete;
        LinuxGamepadDevice& operator=( const LinuxGamepadDevice& ) = delete;

        void poll( float32 deltaTime ) override;

        bool setVibration( float32 leftMotor, float32 rightMotor ) override;
        void stopVibration() override;

        /**
         * @brief joydev 버튼 맵(JSIOCGBTNMAP 의 커널 키 코드)이 게임패드나 조이스틱인지 판정합니다.
         * @details 조이스틱이나 게임패드 버튼(BTN_JOYSTICK 부터 BTN_DIGI 앞까지, BTN_TRIGGER_HAPPY 대역)이 하나라도 있어야 true 이다.
         *          joydev 는 절대 좌표 축만 있으면 마우스나 태블릿에도 jsN 을 만든다. 가상 머신의 절대 좌표 마우스(Hyper-V)는
         *          (0, 0) 에서 축이 -32767 로 고정되어 왼쪽 스틱이 늘 (-1, 1) 로 기운 패드가 된다. 그런 장치는 열지 않는다.
         *          플랫폼과 무관한 판정이라 모든 플랫폼에서 빌드하고 테스트한다.
         */
        static bool isGamepadButtonMap( const uint16* pButtonCode, uint32 buttonCount );

    protected:
        bool isHardwareConnected() const override { return _bConnected == SW_TRUE; }

    private:
        /** @brief /dev/input/js{_deviceIndex} 를 열어 봅니다. 실패하면 다음 poll 에서 재시도 타이머로 다시 시도합니다. */
        void tryOpenJoystick();
        /** @brief 열려 있는 조이스틱 fd를 닫고 연결 해제 상태로 전환합니다. */
        void closeJoystick();
        /** @brief non-blocking read 로 대기 중인 js_event 를 모두 꺼내며 버튼과 축 상태를 갱신합니다. */
        void drainJoystickEvents();
        /** @brief sysfs에서 같은 장치의 evdev 노드를 찾아 force-feedback fd를 엽니다. */
        void tryOpenForceFeedback();
        /** @brief force-feedback fd를 닫고 관련 상태를 리셋합니다. */
        void closeForceFeedback();

        int32                  _fdJoystick;        /**< /dev/input/jsN 파일 디스크립터입니다. 연결 전이면 invalid_index::kInt32(POSIX open() 실패 규약과 같은 -1)입니다. */
        int32                  _fdForceFeedback;   /**< 대응하는 /dev/input/eventN 파일 디스크립터입니다(럼블용). 없으면 invalid_index::kInt32 입니다. */
        int16                  _ffEffectID;        /**< ioctl( EVIOCSFF ) 로 업로드한 FF_RUMBLE 이펙트 ID 입니다. 없으면 invalid_index::kInt16 입니다. */
        uint16                 _ffStrongMagnitude; /**< 마지막으로 업로드한 강모터 세기입니다. 같은 값이면 다시 업로드하지 않습니다. */
        uint16                 _ffWeakMagnitude;   /**< 마지막으로 업로드한 약모터 세기입니다. */
        float32                _reconnectTimer;    /**< 연결되지 않은 상태에서 다시 열기까지 남은 시간(초)입니다. XInput 과 같이 폴링 남발을 막습니다. */
        uint8                  _bConnected        : 1;
        uint8                  _bHasForceFeedback : 1; /**< _fdForceFeedback 이 유효하고 이펙트 업로드까지 성공했는지 나타냅니다. */
        [[maybe_unused]] uint8 _reservedPad       : 6;
    };
} // namespace sw
