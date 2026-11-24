/**
 * @file AsymmetricHorrorStage.h
 * @brief 비대칭 공포 판의 무대 배치(데이터)입니다. 시작 자리, 발전기, 갈고리, 탈출구, 판자, 창틀, 사물함, 해치의 자리와 무대의 크기를 정하고, 판(`AsymmetricHorrorMatch`)에 세웁니다.
 * @details 자리는 모두 절대 자리(월드 원점 0 기준)의 XZ 평면이다. 벽 충돌은 게임의 몫이라 무대는 바깥 경계만 막는다(`limitMove`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AsymmetricHorrorMatch;

    /** @brief 무대 하나입니다. 생존자 · 발전기 · 탈출구는 하나 이상 있어야 하고, 나머지는 비어 있을 수 있습니다(`populate`). */
    REFLECT()
    struct SW_KIT_API AsymmetricHorrorStage
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Start positions, one per survivor", Units = m )
        vector<float3> _listSurvivorStart{};
        PROPERTY( Tooltip = "Generators in the order the killer patrols them", Units = m )
        vector<float3> _listGenerator{};
        PROPERTY( Units = m )
        vector<float3> _listHook{};
        PROPERTY( Units = m )
        vector<float3> _listGate{};
        PROPERTY( Units = m )
        vector<float3> _listPallet{};
        PROPERTY( Units = m )
        vector<float3> _listWindow{};
        PROPERTY( Units = m )
        vector<float3> _listLocker{};
        PROPERTY( Units = m )
        float3 _killerStart{};
        PROPERTY( Units = m )
        float3 _hatch{};
        PROPERTY( Tooltip = "Half size of the walkable area around the origin (X, Z)", Units = m )
        float2 _halfExtent{ 0.0f, 0.0f };

        /**
         * @brief 배치를 @p inoutMatch 에 세웁니다(`initialize` 뒤, `start` 전).
         * @return 생존자 · 발전기 · 탈출구가 없거나 무대 크기가 0 이하이거나 판이 받지 않으면 이유를 남기고 false 입니다.
         */
        [[nodiscard]] bool populate( AsymmetricHorrorMatch& inoutMatch ) const;
        /** @brief @p position 에서 @p direction 으로 갈 때 무대 밖으로 나가는 축을 뺀 방향입니다. */
        float3 limitMove( const float3& position, const float3& direction ) const;
    };
} // namespace sw
