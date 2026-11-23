/**
 * @file WeatherShelter.h
 * @brief 실내 차폐 — 한 자리의 하늘이 막혔는지(지붕 아래) · 얼마나 열렸는지를 3D 물리 광선으로 잽니다.
 * @details 비를 맞는 쪽(캐릭터 젖음 · 데칼 씻김)은 머리 위 광선 하나(`isSheltered`), 듣는 쪽(날씨 소리)은 하늘로 뿌린 광선의 열린 비율(`computeSkyOpenness` — 문가 · 처마 밑은 그 사이 값)을 본다.
 *          비 · 눈 입자는 입자마다 지붕에 부딪혀 사라지므로(`VFXCollisionDef`) 여기를 지나지 않는다 — 모두 같은 물리 바디를 지붕으로 본다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /** @struct WeatherShelter @brief 실내 차폐 판정 모음입니다(파일 머리말 참고). 게임 스레드, 틱 밖에서만 부릅니다(물리 질의). */
    struct SW_GF_API WeatherShelter
    {
        /** @brief 지붕을 찾는 기본 거리(m)입니다. */
        static constexpr float32 kShelterDistance = 20.0f;

        /** @brief @p position 바로 위 @p distance 안에 바디가 있으면 true 입니다. @p ignoreObjectID 의 바디는 건너뜁니다(0 이면 건너뛰지 않는다). 3D 물리가 없으면 false 입니다. */
        [[nodiscard]] static bool isSheltered( const GameObjectManager& manager, const float3& position, float32 distance, uint64 ignoreObjectID );
        /** @brief @p position 에서 하늘이 열린 비율(0 = 실내, 1 = 트인 하늘)입니다. 위 하나와 고도 45 도 네 방향을 쏘아 열린 광선의 비율을 냅니다. */
        [[nodiscard]] static float32 computeSkyOpenness( const GameObjectManager& manager, const float3& position, float32 distance );
    };
} // namespace sw
