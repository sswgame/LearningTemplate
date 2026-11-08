/**
 * @file ResolvedShot.h
 * @brief 무기 하나에 탄 하나와 수정자를 얹은 결과입니다. 장전 · 부착물이 바뀔 때 한 번 계산해 들고, 쏠 때마다 계산하지 않습니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"

namespace sw
{
    /**
     * @struct ResolvedShot
     * @brief 탄 값(`ProjectileDef`) → 무기 수정자 → 부착물 수정자를 얹은 한 발의 값입니다. 각도는 도, 시간은 초, 거리는 m 입니다.
     * @details 탄의 태그 · 맞음 동작 · 궤적은 수정자가 바꾸지 않고 그대로 옮긴다. 피해는 숫자뿐이라 쓰는 쪽이 어빌리티 피해 이펙트(SetByCaller)나 자기 체력으로 넘긴다.
     */
    struct SW_GF_API ResolvedShot
    {
        hashed_string               _projectileID{};
        TagContainer                _tags{};
        vector<ProjectileHitAction> _listHitAction{};
        float4                      _tracerColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float32                     _tracerWidth{ 0.0f };
        float32                     _damage{ 0.0f };
        float32                     _speed{ 0.0f };
        float32                     _gravityScale{ 0.0f };
        float32                     _drag{ 0.0f };
        float32                     _range{ 0.0f };
        float32                     _lifetime{ 0.0f };
        float32                     _radius{ 0.0f };
        float32                     _homingRate{ 0.0f }; ///< 도/초
        float32                     _restitution{ 1.0f };
        float32                     _blastRadius{ 0.0f };
        float32                     _falloffStart{ 0.0f };
        float32                     _falloffEnd{ 0.0f };
        float32                     _falloffMinScale{ 1.0f };
        float32                     _headshotMultiplier{ 1.0f };
        int32                       _pierceCount{ 0 };
        int32                       _bounceCount{ 0 };

        /**
         * @brief 탄 @p projectile 에 @p listWeaponModifier(무기의 `<Modifier>`)를, 그다음 @p listAttachment(부착물 · 버프)를 적힌 순서대로 얹습니다.
         * @details 얹은 값이 음수가 되는 칸(피해 · 속도 · 사거리 · 수명 · 반지름 · 관통 · 튕김)은 0 에서 멈춘다 — 수정자 합이 음수를 만들면 "없음" 이지 거꾸로 나는 탄이 아니다.
         */
        [[nodiscard]] static ResolvedShot resolve( const ProjectileDef& projectile, const vector<ShotModifierDef>& listWeaponModifier, const vector<ShotModifierDef>& listAttachment );

        /** @brief 광선(히트스캔)인지 나타냅니다. */
        bool isHitscan() const { return _speed <= 0.0f; }
        /** @brief 거리 감쇠 배율입니다. `_falloffStart` 까지 1, `_falloffEnd` 에서 `_falloffMinScale`, 그 사이는 선형입니다. */
        float32 computeFalloffScale( float32 distance ) const;
        /** @brief 한 발의 피해입니다. 기본 × 거리 감쇠 × (머리면 `_headshotMultiplier`) 입니다. */
        float32 computeDamage( float32 distance, bool bHeadshot ) const;
    };
} // namespace sw
