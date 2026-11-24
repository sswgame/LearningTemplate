/**
 * @file ActionRoomDebugDraw.h
 * @brief 액션 룸의 디버그 그리기입니다 — 살아 있는 적과 탄을 공으로 그립니다. 룸을 도는 쪽(키트의 디렉터 · 룸을 빌려 쓰는 게임)의 클라이언트가 함께 씁니다.
 * @details 룸 규칙(`ActionRoom`)은 그리지 않는다(렌더 의존 없음). 룸의 자리는 절대 자리라 원점을 빼고 그린다. 공유 키트 폴더의 헤더라, 클라이언트 키트(`GF_Client_ActionCombat`)를 링크하지 못하는 게임 모듈의 클라이언트도 쓴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/GameObject/WorldOrigin.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionRoom.h"

namespace sw
{
    /** @brief 액션 룸 그리기입니다. 파일 머리말 참고. */
    struct ActionRoomDebugDraw
    {
        static constexpr float4  kHallActorColor   = float4( 1.0f, 0.55f, 0.2f, 1.0f );
        static constexpr float4  kBossActorColor   = float4( 1.0f, 0.25f, 0.2f, 1.0f );
        static constexpr float4  kProjectileColor  = float4( 1.0f, 0.9f, 0.2f, 1.0f );
        static constexpr float32 kActorHeight      = 0.5f; ///< 적 공을 띄우는 높이(m)
        static constexpr float32 kProjectileHeight = 0.4f; ///< 탄 공을 띄우는 높이(m)

        /** @brief 룸 평면 (x, y) 의 절대 자리를 땅 위 @p height 의 씬 자리로 옮깁니다. */
        static float3 toScene( const WorldOrigin& origin, const float2& position, float32 height ) { return origin.toLocal( float3( position._x, height, position._y ) ); }

        /** @brief 룸의 적과 탄을 그립니다. 싸움이 없는 룸(`ActionRoom::isActive`)은 그리지 않습니다. */
        static void draw( DebugDrawQueue& queue, const WorldOrigin& origin, const ActionRoom& room )
        {
            if ( room.isActive() == false )
                return;
            const float4 actorColor = room.getKind() == ActionRoomKind::Boss ? kBossActorColor : kHallActorColor;
            room.forEachLiveActor( [&queue, &origin, &actorColor]( const float2& position, float32 radius )
            { queue.drawSphere( toScene( origin, position, kActorHeight ), radius, actorColor ); } );
            for ( const ProjectileState& projectile : room.getProjectiles() )
            {
                queue.drawSphere( toScene( origin, float2{ projectile._position._x, projectile._position._y }, kProjectileHeight ), projectile._radius, kProjectileColor );
            }
        }
    };
} // namespace sw
