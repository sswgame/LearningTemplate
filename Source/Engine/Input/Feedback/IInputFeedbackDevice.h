/**
 * @file IInputFeedbackDevice.h
 * @brief 진동을 내는 입력 장치의 출력 인터페이스입니다(게임패드 럼블 — 터치 기기 진동도 같은 자리).
 * @details 언리얼 `IInputInterface::SetForceFeedbackChannelValues` · 유니티 `IDualMotorRumble::SetMotorSpeeds` · Godot `Input.start_joy_vibration` 의 두 모터 모델이다.
 *          시간에 따른 진동(`*.haptic.xml`)은 장치가 든 `HapticPlayback` 이 매 프레임 이 출력으로 보낸다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class IInputFeedbackDevice
     * @brief 저주파(왼쪽 · 강) · 고주파(오른쪽 · 약) 두 모터의 세기를 받는 출력입니다.
     */
    class SW_API IInputFeedbackDevice
    {
    public:
        IInputFeedbackDevice()          = default;
        virtual ~IInputFeedbackDevice() = default;

        IInputFeedbackDevice( const IInputFeedbackDevice& )            = delete;
        IInputFeedbackDevice& operator=( const IInputFeedbackDevice& ) = delete;

        /** @brief 두 모터의 세기 [0, 1] 를 지금 값으로 둡니다. 장치가 진동을 낼 수 없으면 false 입니다. */
        virtual bool setVibration( float32 lowFrequency, float32 highFrequency ) = 0;
        /** @brief 진동과 진행 중인 재생을 멈춥니다. */
        virtual void stopVibration() = 0;
    };
} // namespace sw
