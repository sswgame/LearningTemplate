/**
 * @file InteractionSelector.h
 * @brief 상호작용 대상 고르기 — 거리 · 시야각 · 시야(가림) 안의 후보 중 우선도가 높고 가장 가까운 것입니다. 2D(XY 평면)와 3D 가 같은 계산입니다.
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

    /** @brief 거리 · 각을 재는 공간입니다. */
    ENUM()
    enum class InteractionSpace : uint8
    {
        Space3D = 0, ///< 3 축 거리, 시선은 앞 방향
        Space2D      ///< XY 평면(Z 는 그리기 순서) — 거리 · 각에서 Z 를 뺀다
    };
} // namespace sw

namespace sw
{
    /** @brief 고르는 쪽(눈)입니다. */
    struct InteractionViewer
    {
        float3           _position{};
        float3           _forward{ 0.0f, 0.0f, 1.0f }; ///< 시선(정규화하지 않아도 된다)
        uint64           _objectId{ 0 };
        InteractionSpace _space{ InteractionSpace::Space3D };
    };
} // namespace sw

namespace sw
{
    /** @brief 후보 하나입니다. */
    struct InteractionCandidate
    {
        float3  _position{};
        uint64  _objectId{ 0 };
        float32 _maxDistance{ 2.0f };
        float32 _maxAngle{ 0.0f }; ///< 라디안, 0 이면 보지 않는다
        int32   _priority{ 0 };    ///< 높을수록 먼저(같으면 가까운 것)
        uint8   _bRequiresLineOfSight{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 눈에서 대상이 보이는가를 묻는 창구입니다(물리 백엔드 · 시험 가짜). */
    class ILineOfSightQuery
    {
    public:
        ILineOfSightQuery()          = default;
        virtual ~ILineOfSightQuery() = default;

        ILineOfSightQuery( const ILineOfSightQuery& )            = default;
        ILineOfSightQuery& operator=( const ILineOfSightQuery& ) = default;

        virtual bool hasLineOfSight( const float3& from, const float3& to, uint64 viewerObjectId, uint64 targetObjectId ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 월드 질의(`WorldQuery` — 물리 백엔드 서비스 또는 `PhysicsWorld` 폴백)로 시야를 답합니다. */
    class SW_GF_API WorldLineOfSightQuery final : public ILineOfSightQuery
    {
    public:
        explicit WorldLineOfSightQuery( const GameObjectManager& manager );

        bool hasLineOfSight( const float3& from, const float3& to, uint64 viewerObjectId, uint64 targetObjectId ) const override;

    private:
        const GameObjectManager& _manager;
    };
} // namespace sw

namespace sw
{
    /** @struct InteractionSelector */
    struct SW_GF_API InteractionSelector
    {
        /** @brief 거리 · 각만 보고(시야는 빼고) 후보가 닿는가입니다. @p outDistance 는 공간에 맞춘 거리입니다. */
        static bool isInReach( const InteractionViewer& viewer, const InteractionCandidate& candidate, float32& outDistance );
        /**
         * @brief 닿고(`isInReach`) 보이는(@p pLineOfSight 가 있고 후보가 시야를 요구하면) 후보 중 우선도가 가장 높고, 같으면 가장 가까운 것의 번호입니다.
         * @details 시야 질의는 거리 · 각을 통과한 후보에만, 가까운 순으로 묻습니다(광선이 비싸다). 없으면 −1 입니다.
         */
        static int32 selectBest( const InteractionViewer& viewer, const vector<InteractionCandidate>& listCandidate, const ILineOfSightQuery* pLineOfSight );
    };
} // namespace sw
