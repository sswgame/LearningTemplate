/**
 * @file FrontierTownData.h
 * @brief 두 디렉터가 같은 판을 여는 공유 상태 설정과 게임이 쓰는 이름입니다 — 어느 디렉터가 먼저 열어도 같은 값.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

namespace sw
{
    /**
     * @struct FrontierTownData
     * @brief 땅 32 × 32(칸 1 m) 하나를 도시(GF_CityBuilder)와 RTS(GF_RealTimeStrategy)가 나눈다. 하루(= 도시의 한 달)는 실제 24 초.
     * @details 땅 크기는 두 키트의 격자 크기이기도 하다 — 도시 셀 (0, 0) 과 RTS 셀 (0, 0) 이 땅 셀 (0, 0) 이다(`bindLand` 의 원점).
     */
    struct FrontierTownData
    {
        static constexpr const utf8* kFastForwardAction = "Frontier.FastForward"; ///< 누르는 동안 빨리 감기
        static constexpr int32       kLandSize          = 32;

        static SessionStateSettings makeStateSettings()
        {
            SessionStateSettings settings;
            settings._clock._secondsPerDay = 24.0f;
            settings._clock._startHour     = 6.0f;
            settings._landWidth            = kLandSize;
            settings._landHeight           = kLandSize;
            return settings;
        }
    };
} // namespace sw
