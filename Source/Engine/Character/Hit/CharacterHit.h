/**
 * @file CharacterHit.h
 * @brief 맞힘 한 벌 — 광선(3D · 2D)으로 맞은 오브젝트 · 바디 · 재질을 찾고, 히트 존(이름 · 피해 배율)을 고르고, 맞은 오브젝트의 컴포넌트에 알립니다.
 * @details 근접 판정(알림 표의 `HitWindow`)과 무기의 레이캐스트가 같은 길을 씁니다(언리얼 LineTrace → `FHitResult::BoneName` → Physics Asset 바디의 부위 ·
 *          `TakeDamage`). 쏘는 오브젝트의 바디는 모두 건너뜁니다(`PhysicsQueryFilter::_ignoreUserData` — 래그돌 뼈 · 든 무기). 맞은 바디는 사용자 값으로
 *          오브젝트가 되고, 히트 존은 그 오브젝트의 래그돌(물리 에셋 바디의 `_hitZone`) → 강체 컴포넌트의 히트 존 속성 순으로 찾습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    struct HitInfo;

    class GameObject;
    class GameObjectManager;

    /** @brief 광선이 처음 맞은 것입니다. 2D 면 Z = 0 입니다. */
    struct CharacterRayHit
    {
        GameObject*       _pObject{ nullptr }; ///< 맞은 바디의 오브젝트(사용자 값이 오브젝트 id 가 아니면 nullptr)
        PhysicsBodyHandle _body{};
        float3            _point{};
        float3            _normal{};
        hashed_string     _material{}; ///< 맞은 셰이프의 물리 재질 이름
        float32           _distance{ 0.0f };
        bool              _bIs2D{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 맞힘 도우미입니다(전부 static, 게임 스레드 — 물리 씬은 한 스레드). */
    struct SW_API CharacterHitUtil
    {
        /**
         * @brief 3D 강체 씬에 광선을 쏩니다. @p ignoreObjectId 의 바디는 모두 건너뜁니다(0 이면 건너뛰지 않음). 3D 씬이 없으면 false 입니다.
         * @param layerMask 볼 레이어 비트(기본 모두). 트리거는 보지 않습니다.
         */
        static bool raycast3D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask, uint64 ignoreObjectId,
                               CharacterRayHit& outHit );
        /** @brief 2D 강체 씬(XY 평면)에 광선을 쏩니다. 출발 · 방향의 Z 는 보지 않습니다. */
        static bool raycast2D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask, uint64 ignoreObjectId,
                               CharacterRayHit& outHit );
        /** @brief 3D 강체 씬에 구를 쓸어 처음 맞은 것입니다(칼날 두께). */
        static bool sphereCast3D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 radius, uint32 layerMask,
                                  uint64 ignoreObjectId, CharacterRayHit& outHit );
        /** @brief 2D 강체 씬에 원을 쓸어 처음 맞은 것입니다. */
        static bool circleCast2D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 radius, uint32 layerMask,
                                  uint64 ignoreObjectId, CharacterRayHit& outHit );
        /**
         * @brief 맞은 바디의 히트 존을 @p inoutHit 에 채웁니다(`_zone` · `_damageMultiplier` · `_bodyIndex`). 존이 없으면 이름은 비고 배율 1 입니다.
         * @details 래그돌 · 히트박스(`RagdollComponent` — 물리 에셋 바디의 `_hitZone`)가 먼저, 다음은 그 바디를 가진 강체 컴포넌트의 `_hitZone` 입니다.
         */
        static void resolveHitZone( const GameObject& target, PhysicsBodyHandle body, HitInfo& inoutHit );
        /** @brief 맞은 오브젝트의 켜진 컴포넌트마다 `onHitReceived` 를 부릅니다(목록을 베껴 돌므로 처리 중에 컴포넌트를 붙이고 떼도 된다). */
        static void deliverHit( GameObject& target, const HitInfo& hit );
        /**
         * @brief 무기 한 발 — 광선을 쏘아 맞은 오브젝트 · 히트 존을 찾고 맞음을 알립니다(레이캐스트 → 바디 → 히트 존 → `onHitReceived`).
         * @param pInstigator 쏜 오브젝트(그 바디는 건너뛴다). nullptr 이면 건너뛰지 않습니다.
         * @param outHit 알린 맞음입니다(맞지 않았으면 그대로).
         * @return 오브젝트를 맞혔으면 true 입니다.
         */
        static bool traceWeaponHit( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask,
                                    GameObject* pInstigator, float32 damage, float32 impulse, bool bIs2D, HitInfo& outHit );
    };
} // namespace sw
