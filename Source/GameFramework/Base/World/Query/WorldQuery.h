/**
 * @file WorldQuery.h
 * @brief 게임플레이가 월드에 묻는 기하 질의(광선과 시야)의 좁은 인터페이스를 정의합니다. 물리 백엔드를 몰라도 쓸 수 있습니다.
 * @details 상호작용 시야(line of sight), 기믹 레이저, 포탑 조준이 이것만 부른다.
 *          게임이 `IWorldQuery` 를 게임 서비스로 걸면 그것을 쓴다.
 *          없으면 씬의 물리(`ScenePhysicsWorldQuery`)에 묻는다. Jolt 3D 와 Box2D 2D 씬에 광선을 쏘고 트리거는 제외한다.
 *          2D 콜라이더(`BoxCollider2DComponent`)도 2D 씬의 바디라 같은 광선이 맞히며, 2D 씬은 XY 평면이라 광선의 Z 와 상관없이 맞힌다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 광선이 처음 맞은 것입니다. */
    struct WorldRayHit
    {
        float3  _point{};
        float32 _fraction{ 1.0f }; ///< 시작(0)..끝(1) 사이 맞은 위치
        uint64  _objectID{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 광선 질의 인터페이스입니다. 물리 백엔드가 구현하며, 여러 스레드에서 동시에 불릴 수 있으므로 읽기만 해야 합니다. */
    class IWorldQuery
    {
    public:
        IWorldQuery()          = default;
        virtual ~IWorldQuery() = default;

        IWorldQuery( const IWorldQuery& )            = default;
        IWorldQuery& operator=( const IWorldQuery& ) = default;

        /** @brief @p from 에서 @p to 까지의 선분이 처음 맞는 막는 바디를 구합니다. @p ignoreObjectID 의 바디는 건너뛰며, 맞으면 true 입니다. */
        virtual bool raycast( const float3& from, const float3& to, uint64 ignoreObjectID, WorldRayHit& outHit ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScenePhysicsWorldQuery
     * @brief 씬의 강체 물리(3D 씬과 2D 씬)에 대한 광선 질의입니다.
     * @details 무시할 오브젝트의 바디는 모두 건너뛴다. 래그돌 뼈와 든 무기가 그 예이다. 3D 를 먼저 보고, 2D 는 XY 평면에서 본다.
     */
    class SW_GF_API ScenePhysicsWorldQuery final : public IWorldQuery
    {
    public:
        explicit ScenePhysicsWorldQuery( const GameObjectManager& manager );

        bool raycast( const float3& from, const float3& to, uint64 ignoreObjectID, WorldRayHit& outHit ) const override;

    private:
        const GameObjectManager& _manager;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WorldQuery
     * @brief 등록된 서비스(`IWorldQuery`)가 있으면 그것으로 묻고, 없으면 씬의 물리(`ScenePhysicsWorldQuery`)로 묻습니다.
     */
    struct SW_GF_API WorldQuery
    {
        static bool raycast( const GameObjectManager& manager, const float3& from, const float3& to, uint64 ignoreObjectID, WorldRayHit& outHit );
        /**
         * @brief @p from 에서 @p to 가 보이면 true 입니다. 보는 쪽(@p viewerObjectID)은 건너뛰고, 처음 맞은 것이 없거나 대상(@p targetObjectID) 자신이면 보이는 것으로 봅니다.
         */
        static bool hasLineOfSight( const GameObjectManager& manager, const float3& from, const float3& to, uint64 viewerObjectID, uint64 targetObjectID );
    };
} // namespace sw
