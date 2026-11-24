/**
 * @file ActionRoomLook.h
 * @brief 액션 룸의 모습 값입니다 — 살아 있는 적과 탄을 어떤 높이 · 색의 공으로 보이는지. 룸을 도는 쪽(키트의 디렉터 · 룸을 빌려 쓰는 게임)의 클라이언트 뷰가 함께 씁니다.
 * @details 룸 규칙(`ActionRoom`)은 보이는 일을 모른다(렌더 의존 없음). 여기서는 무엇을 어디에 보일지만 정하고, 프리팹으로 보이는 일은 클라이언트 뷰가 한다 — 공유 코드는 클라이언트 헤더를 쓰지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionRoom.h"

namespace sw
{
    /** @brief 액션 룸의 모습 값입니다. 파일 머리말 참고. */
    REFLECT()
    struct SW_KIT_API ActionRoomLook
    {
        REFLECT_BODY();
        PROPERTY( DisplayName = "Hall Actor Color" )
        float4 _hallActorColor{ 1.0f, 0.55f, 0.2f, 1.0f };
        PROPERTY( DisplayName = "Boss Actor Color" )
        float4 _bossActorColor{ 1.0f, 0.25f, 0.2f, 1.0f };
        PROPERTY( DisplayName = "Projectile Color" )
        float4 _projectileColor{ 1.0f, 0.9f, 0.2f, 1.0f };
        PROPERTY( DisplayName = "Actor Height", Tooltip = "Height of an actor's center above the floor", Units = m )
        float32 _actorHeight{ 0.5f };
        PROPERTY( DisplayName = "Projectile Height", Tooltip = "Height of a projectile's center above the floor", Units = m )
        float32 _projectileHeight{ 0.4f };

        /** @brief 룸 평면 (x, y) 의 절대 자리를 땅 위 @p height 에 둡니다. */
        static AbsolutePosition makePosition( const float2& position, float32 height ) { return AbsolutePosition{ float3( position._x, height, position._y ) }; }

        /** @brief 보일 공마다 @p show( const AbsolutePosition&, float32 radius, const float4& color ) 를 부릅니다. 싸움이 없는 룸(`ActionRoom::isActive`)은 부르지 않습니다. */
        template <typename FN>
        void forEachSphere( const ActionRoom& room, const FN& show ) const
        {
            if ( room.isActive() == false )
                return;
            const float4& actorColor = room.getKind() == ActionRoomKind::Boss ? _bossActorColor : _hallActorColor;
            room.forEachLiveActor( [this, &show, &actorColor]( const float2& position, float32 radius )
            { show( makePosition( position, _actorHeight ), radius, actorColor ); } );
            for ( const ProjectileState& projectile : room.getProjectiles() )
            {
                show( makePosition( float2{ projectile._position._x, projectile._position._y }, _projectileHeight ), projectile._radius, _projectileColor );
            }
        }
    };
} // namespace sw
