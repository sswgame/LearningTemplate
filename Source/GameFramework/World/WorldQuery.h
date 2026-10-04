/**
 * @file WorldQuery.h
 * @brief 게임플레이가 월드에 묻는 기하 질의(광선 · 시야)의 좁은 창구 — 물리 백엔드를 모르고 씁니다.
 * @details 상호작용 시야(line of sight) · 기믹 레이저 · 포탑 조준이 이것만 부릅니다. 게임이 `IWorldQuery` 를 게임 서비스로 걸면(Jolt 백엔드의
 *          광선 질의) 그것을 쓰고, 없으면 엔진 `PhysicsWorld` 의 AABB 바디를 훑는 폴백(`PhysicsWorldQuery`)을 씁니다. 폴백은 트리거 바디를
 *          막는 것으로 치지 않고 레이어 0 의 충돌 행렬로 거릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class PhysicsWorld;

    /** @brief 광선이 처음 맞은 것입니다. */
    struct WorldRayHit
    {
        float3  _point{};
        float32 _fraction{ 1.0f }; ///< 시작(0)..끝(1) 사이 맞은 자리
        uint64  _objectId{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 광선 질의 한 벌입니다. 물리 백엔드가 구현합니다. 여러 스레드에서 동시에 불릴 수 있으니 읽기만 합니다. */
    class IWorldQuery
    {
    public:
        IWorldQuery()          = default;
        virtual ~IWorldQuery() = default;

        IWorldQuery( const IWorldQuery& )            = default;
        IWorldQuery& operator=( const IWorldQuery& ) = default;

        /** @brief @p from → @p to 선분이 처음 맞는 막는 바디입니다. @p ignoreObjectId 의 바디는 건너뜁니다. 맞으면 true 입니다. */
        virtual bool raycast( const float3& from, const float3& to, uint64 ignoreObjectId, WorldRayHit& outHit ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 엔진 `PhysicsWorld`(AABB 바디 · 겹침 그리드) 위의 폴백 질의입니다. 선분을 아주 얇은 상자로 쓸어 맞은 바디를 고릅니다. */
    class SW_GF_API PhysicsWorldQuery final : public IWorldQuery
    {
    public:
        explicit PhysicsWorldQuery( const PhysicsWorld& physicsWorld );

        bool raycast( const float3& from, const float3& to, uint64 ignoreObjectId, WorldRayHit& outHit ) const override;

    private:
        const PhysicsWorld& _physicsWorld;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WorldQuery
     * @brief 걸린 서비스(`IWorldQuery`)가 있으면 그것, 없으면 매니저의 `PhysicsWorld` 폴백으로 묻습니다.
     */
    struct SW_GF_API WorldQuery
    {
        static bool raycast( const GameObjectManager& manager, const float3& from, const float3& to, uint64 ignoreObjectId, WorldRayHit& outHit );
        /**
         * @brief @p from 에서 @p to 가 보이는가입니다 — 보는 쪽(@p viewerObjectId)은 건너뛰고, 처음 맞은 것이 없거나 대상(@p targetObjectId) 자신이면 보입니다.
         */
        static bool hasLineOfSight( const GameObjectManager& manager, const float3& from, const float3& to, uint64 viewerObjectId, uint64 targetObjectId );
    };
} // namespace sw
