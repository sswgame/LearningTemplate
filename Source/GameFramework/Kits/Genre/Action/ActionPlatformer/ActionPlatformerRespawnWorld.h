/**
 * @file ActionPlatformerRespawnWorld.h
 * @brief 칸 지형(`PlatformTileMap`) 위의 부활 세계 질의입니다. 설 수 있는 자리는 발밑이 벽 · 한쪽 발판이고 몸 칸이 빈 곳입니다(위험 칸 위는 아니다).
 * @details 공통 부활 흐름(`RespawnFlow`)의 제자리 부활이 묻는다 — 낭떠러지에 떨어져 죽으면 반경 안 가장 가까운 설 자리(칸 가운데)로 세운다.
 *          스트리밍하지 않는 한 판 지형이라 자리 준비는 늘 됐다. 3D 게임이 내비 메시 · 캡슐 질의로 답하는 자리를 2D 칸으로 답한 것이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Respawn/RespawnFlow.h"

namespace sw
{
    class PlatformTileMap;

    /**
     * @class ActionPlatformerRespawnWorld
     * @brief 칸 지형과 몸 반 크기를 빌려 설 자리를 답합니다. 지형보다 오래 살지 않습니다(걸음마다 만들어 넘긴다).
     */
    class SW_KIT_API ActionPlatformerRespawnWorld final : public IRespawnWorld
    {
    public:
        ActionPlatformerRespawnWorld( const PlatformTileMap& map, const float2& halfExtents );

        /** @brief @p position(몸 가운데)이 설 자리면 그대로, 아니면 @p radius 안 가장 가까운 설 자리(칸 가운데, 발이 칸 위)입니다. */
        [[nodiscard]] optional<float3> findSafeLocation( const float3& position, float32 radius ) const override;
        [[nodiscard]] bool             isLocationReady( const float3& position ) const override;

    private:
        /** @brief 칸 (@p x, @p y) 가 몸이 설 칸인지(그 칸이 비고 아래 칸이 밟히는 땅인지) 봅니다. */
        bool isStandingCell( int32 x, int32 y ) const;

    private:
        const PlatformTileMap* _pMap; ///< 빌림(생성자가 받은 참조)
        float2                 _halfExtents;
    };
} // namespace sw
