/**
 * @file CharacterHit.h
 * @brief 맞힘 도우미 모음입니다. 광선(3D, 2D)으로 맞은 오브젝트, 바디, 재질을 찾고, 히트 존(이름, 피해 배율)을 고르고, 맞은 오브젝트의 컴포넌트에 알립니다.
 * @details 근접 판정(`MeleeHitWindow.h` — 알림 테이블의 `HitWindow` 와 킷의 `MeleeHitboxComponent`)과 무기의 레이캐스트가 같은 경로를 쓴다(언리얼의 LineTrace, `FHitResult::BoneName`, Physics Asset 바디의 부위,
 *          `TakeDamage` 흐름에 해당한다). 쏘는 오브젝트의 바디는 모두 건너뛴다(`PhysicsQueryFilter::_ignoreUserData`, 래그돌 뼈와 든 무기). 맞은 바디는 사용자 값으로
 *          오브젝트가 되고, 히트 존은 그 오브젝트의 래그돌(물리 에셋 바디의 `_hitZone`)에서 먼저, 없으면 강체 컴포넌트의 히트 존 속성에서 찾는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
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
        hashed_string     _material{}; ///< 맞은 셰이프의 물리 재질 이름이다.
        float32           _distance{ 0.0f };
        bool              _bIs2D{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 맞힘 도우미입니다(전부 static, 게임 스레드에서 호출합니다. 물리 씬은 한 스레드에서만 다룹니다). */
    struct SW_API CharacterHitUtil
    {
        /**
         * @brief 3D 강체 씬에 광선을 쏩니다. @p ignoreObjectID 의 바디는 모두 건너뜁니다(0 이면 건너뛰지 않습니다). 3D 씬이 없으면 false 를 반환합니다.
         * @param layerMask 볼 레이어 비트입니다(기본은 모두). 트리거는 보지 않습니다.
         */
        static bool raycast3D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask, uint64 ignoreObjectID,
                               CharacterRayHit& outHit );
        /** @brief 2D 강체 씬(XY 평면)에 광선을 쏩니다. 출발점과 방향의 Z 는 보지 않습니다. */
        static bool raycast2D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask, uint64 ignoreObjectID,
                               CharacterRayHit& outHit );
        /** @brief 3D 강체 씬에 구를 쓸어 처음 맞은 것입니다(칼날 두께). */
        static bool sphereCast3D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 radius, uint32 layerMask,
                                  uint64 ignoreObjectID, CharacterRayHit& outHit );
        /** @brief 2D 강체 씬에 원을 쓸어 처음 맞은 것입니다. */
        static bool circleCast2D( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, float32 radius, uint32 layerMask,
                                  uint64 ignoreObjectID, CharacterRayHit& outHit );
        /**
         * @brief @p from 에서 @p to 까지 구(2D 는 원, @p radius 가 0 이면 광선)를 쓸어 닿은 바디를 **모두** @p outListHit 끝에 붙이고 그 수를 반환합니다.
         * @details 바디마다 가장 가까운 닿음 하나이고 가까운 순서다. 출발점에서 이미 겹친 바디도 들어온다(`shapeCastAll`). 근접 판정(`MeleeHitWindowUtil`)이
         *          칼날 한 토막에 걸린 것을 모두 찾는 데 쓴다 — 처음 닿은 것에서 멈추면 이미 맞힌 오브젝트가 뒤의 표적을 가린다. 길이가 거의 0 이거나 씬이 없으면 0 입니다.
         */
        static uint32 sweepAll( const GameObjectManager& manager, const float3& from, const float3& to, float32 radius, uint32 layerMask, uint64 ignoreObjectID, bool bIs2D,
                                vector<CharacterRayHit>& outListHit );
        /**
         * @brief 맞은 바디의 히트 존을 @p inoutHit 에 채웁니다(`_zone`, `_damageMultiplier`, `_bodyIndex`). 존이 없으면 이름은 비고 배율은 1 입니다.
         * @details 래그돌과 히트박스(`RagdollComponent`, 물리 에셋 바디의 `_hitZone`)가 먼저이고, 다음은 그 바디를 가진 강체 컴포넌트의 `_hitZone` 이다.
         */
        static void resolveHitZone( const GameObject& target, PhysicsBodyHandle body, HitInfo& inoutHit );
        /**
         * @brief 광선이 맞은 것(@p rayHit)과 때린 쪽 값으로 맞음을 만들고 히트 존까지 채웁니다. `_pObject` 가 없으면 히트 존은 비어 있습니다.
         * @details 무기 레이캐스트(`traceWeaponHit`)와 근접 판정(`MeleeHitWindowUtil::sweep`)이 같은 맞음을 이 한 곳에서 만든다. 피해 배율은 히트 존이 정한다.
         * @param direction 때린 방향입니다(길이는 상관없고, 거의 0 이면 방향 없음).
         */
        static HitInfo makeHitInfo( const CharacterRayHit& rayHit, GameObject* pInstigator, const float3& direction, float32 damage, float32 impulse );
        /** @brief 맞은 오브젝트의 켜진 컴포넌트마다 `onHitReceived` 를 호출합니다(목록을 복사해서 돌므로 처리 중에 컴포넌트를 붙이고 떼도 됩니다). */
        static void deliverHit( GameObject& target, const HitInfo& hit );
        /**
         * @brief 무기 한 발입니다. 광선을 쏘아 맞은 오브젝트와 히트 존을 찾고 맞음을 알립니다(레이캐스트, 바디, 히트 존, `onHitReceived` 순서).
         * @param pInstigator 쏜 오브젝트입니다(그 바디는 건너뜁니다). nullptr 이면 건너뛰지 않습니다.
         * @param outHit 알린 맞음입니다(맞지 않았으면 그대로입니다).
         * @return 오브젝트를 맞혔으면 true 입니다.
         */
        static bool traceWeaponHit( const GameObjectManager& manager, const float3& origin, const float3& direction, float32 maxDistance, uint32 layerMask,
                                    GameObject* pInstigator, float32 damage, float32 impulse, bool bIs2D, HitInfo& outHit );
    };
} // namespace sw
