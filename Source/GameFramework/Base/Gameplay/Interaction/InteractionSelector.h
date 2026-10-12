/**
 * @file InteractionSelector.h
 * @brief 상호작용 대상 고르기입니다. 거리, 시야각, 시야(가림)를 만족하는 후보 중 우선도가 가장 높고 가장 가까운 것을 고릅니다. 2D(XY 평면)와 3D 가 같은 계산을 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 거리와 각을 측정하는 공간입니다. */
    ENUM()
    enum class InteractionSpace : uint8
    {
        Space3D = 0, ///< 3 축 거리를 쓰고, 시선은 앞 방향이다
        Space2D      ///< XY 평면이다. Z 는 그리기 순서이므로 거리와 각에서 Z 를 뺀다
    };
} // namespace sw

namespace sw
{
    /** @brief 고르는 쪽(눈)입니다. */
    struct InteractionView
    {
        float3           _position{};
        float3           _forward{ 0.0f, 0.0f, 1.0f }; ///< 시선. 정규화하지 않아도 된다
        uint64           _objectID{ 0 };
        InteractionSpace _space{ InteractionSpace::Space3D };
    };
} // namespace sw

namespace sw
{
    /** @brief 후보 하나입니다. */
    struct InteractionCandidate
    {
        float3  _position{};
        uint64  _objectID{ 0 };
        float32 _maxDistance{ 2.0f };
        float32 _maxAngle{ 0.0f }; ///< 라디안이며, 0 이면 검사하지 않는다
        int32   _priority{ 0 };    ///< 높을수록 먼저 고른다. 같으면 가까운 것을 고른다
        uint8   _bRequiresLineOfSight{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 눈에서 대상이 보이는지 묻는 인터페이스입니다. 물리 백엔드와 테스트용 가짜 구현이 있습니다. */
    class ILineOfSightQuery
    {
    public:
        ILineOfSightQuery()          = default;
        virtual ~ILineOfSightQuery() = default;

        ILineOfSightQuery( const ILineOfSightQuery& )            = default;
        ILineOfSightQuery& operator=( const ILineOfSightQuery& ) = default;

        virtual bool hasLineOfSight( const float3& from, const float3& to, uint64 viewerObjectID, uint64 targetObjectID ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 월드 질의로 시야를 판정합니다. 물리 백엔드 서비스(`WorldQuery`)를 쓰고, 없으면 `PhysicsWorld` 폴백을 씁니다. */
    class SW_GF_API WorldLineOfSightQuery final : public ILineOfSightQuery
    {
    public:
        explicit WorldLineOfSightQuery( const GameObjectManager& manager );

        bool hasLineOfSight( const float3& from, const float3& to, uint64 viewerObjectID, uint64 targetObjectID ) const override;

    private:
        const GameObjectManager& _manager;
    };
} // namespace sw

namespace sw
{
    /** @struct InteractionSelector */
    struct SW_GF_API InteractionSelector
    {
        /** @brief 거리와 각만 보고 후보가 닿는지 판정합니다. 시야는 보지 않습니다. @p outDistance 는 공간에 맞춘 거리입니다. */
        static bool isInReach( const InteractionView& viewer, const InteractionCandidate& candidate, float32& outDistance );
        /**
         * @brief 닿고(`isInReach`) 보이는 후보 중 우선도가 가장 높고, 같으면 가장 가까운 것의 번호입니다. 보이는지는 @p pLineOfSight 가 있고 후보가 시야를 요구할 때만 검사합니다.
         * @details 시야 질의는 거리와 각을 통과한 후보에만 가까운 순으로 한다. 광선 검사가 비싸기 때문이다. 고를 후보가 없으면 −1 이다.
         */
        static int32 selectBest( const InteractionView& viewer, const vector<InteractionCandidate>& listCandidate, const ILineOfSightQuery* pLineOfSight );
    };
} // namespace sw
