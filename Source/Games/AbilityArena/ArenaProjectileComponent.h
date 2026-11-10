/**
 * @file ArenaProjectileComponent.h
 * @brief 날아가는 투사체 하나의 모습 — 디렉터의 발사체 시뮬레이션에서 자기 탄을 읽어 메시를 옮깁니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Foundation/Framework/GameModeBoundComponent.h"

namespace sw
{
    /**
     * @class ArenaProjectileComponent
     * @brief 투사체 프리팹의 컴포넌트입니다. 디렉터가 탄을 시뮬레이션에 넣은 뒤 모습을 세우고 `bind` 로 그 탄 id 를 줍니다.
     * @details 탄의 운동 · 맞음 · 이펙트는 디렉터의 `ProjectileSimulation` 이 한다(`ArenaGameModeComponent`). 이 컴포넌트는 기본 틱 그룹(`DuringPhysics`)에서
     *          디렉터가 `PrePhysics` 에 돌린 탄의 위치를 읽기만 하고 자기 메시를 옮긴다. 탄이 끝났으면(맞음 · 사거리 · 판 밖) 자기 오브젝트를 지운다.
     *          탄을 게임 오브젝트로 돌리지 않으므로 탄 수가 늘어도 규칙 비용은 시뮬레이션 하나다. 모습은 상태 저장 전에 디렉터가 걷는다.
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Projectile", Tooltip = "Shows one projectile of the arena game mode's projectile simulation" )
    class ArenaProjectileComponent : public GameModeBoundComponent
    {
    public:
        REFLECT_BODY();

        ArenaProjectileComponent();
        virtual ~ArenaProjectileComponent() override = default;

        void onTick( TickContext& context ) override;

        /** @brief 디렉터 @p gameMode 의 탄 @p projectileID 를 보여 줍니다. @p position 은 쏜 자리(발 높이)이고 모습은 `_visualLift` 만큼 위에 그린다. */
        void bind( GameObjectHandle gameMode, uint32 projectileID, const float3& position );

    private:
        void placeMesh( const float3& position ) const;
        void finish();

    private:
        PROPERTY( Category = "Projectile", DisplayName = "Visual Lift", Tooltip = "Drawn this high above the hit point (chest height)", Units = m )
        float32 _visualLift;

        uint32 _projectileID; ///< 디렉터 시뮬레이션의 탄 id(0 = 아직 없다)
        uint8  _bFinished : 1;
        uint8  _reserved  : 7;
    };
} // namespace sw
