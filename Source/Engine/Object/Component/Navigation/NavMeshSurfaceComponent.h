/**
 * @file NavMeshSurfaceComponent.h
 * @brief 씬의 내비메시를 베이크하는 표면 — 어느 에이전트 종류를 · 어떤 기하(그리는 메시 · 물리 셰이프)로 · 어디까지(경계) 베이크하는지 정합니다.
 * @details 유니티 `NavMeshSurface` · 언리얼 `NavMeshBoundsVolume` 의 자리입니다. 씬에 이것이 있으면 쿠킹이 그 씬의 `.navmesh` 를 쓰고, Dev 는 플레이를
 *          시작한 첫 내비게이션 갱신에서 베이크합니다. 한 에이전트 종류는 표면 하나가 맡습니다(둘이면 처음 것만 쓰고 경고).
 *
 *          **모으는 규칙**(`SceneNavigation::collectGeometry`): 켜진 오브젝트의 보이는 `MeshComponent`(스킨드 메시 제외)와 Static `RigidBodyComponent` 의
 *          셰이프. 다음은 빼고 그 자식도 뺀다 — `NavMeshModifierComponent` 의 `_bIgnoreFromBuild`, `_listExcludeTag` 의 태그, 움직이는 것(에이전트 ·
 *          캐릭터 컨트롤러 · Static 이 아닌 강체 · 스킨드 메시가 든 오브젝트의 계층). 파괴 오브젝트는 자기 기하를 따로 낸다(`INavGeometrySource`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 표면이 모으는 기하입니다. */
    ENUM()
    enum class NavGeometrySource : uint8
    {
        RenderMeshes = 0, ///< 보이는 메시
        PhysicsColliders, ///< Static 강체의 셰이프
        Both,
    };
} // namespace sw

namespace sw
{
    /** @class NavMeshSurfaceComponent @brief 파일 머리말 참고. */
    REFLECT( Category = "Navigation", DisplayName = "NavMesh Surface", Tooltip = "Bakes the scene navmesh for agent types from render meshes or physics colliders" )
    class SW_API NavMeshSurfaceComponent : public Component
    {
    public:
        REFLECT_BODY();

        NavMeshSurfaceComponent();
        ~NavMeshSurfaceComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 이 표면이 맡는 에이전트 종류입니다. 비면 기본 종류 하나입니다. */
        const vector<hashed_string>& getAgentTypes() const { return _listAgentType; }
        void                         setAgentTypes( vector<hashed_string> listAgentType ) { _listAgentType = std::move( listAgentType ); }
        NavGeometrySource            getGeometrySource() const { return _geometrySource; }
        void                         setGeometrySource( NavGeometrySource source ) { _geometrySource = source; }
        const vector<TagID>&         getExcludeTags() const { return _listExcludeTag; }
        void                         setExcludeTags( vector<TagID> listTag ) { _listExcludeTag = std::move( listTag ); }
        /** @brief 베이크하는 경계의 반 크기(이 오브젝트 자리 둘레, 월드 축)입니다. 0 이면 모은 기하 전체입니다. */
        const float3& getBoundsHalfExtents() const { return _boundsHalfExtents; }
        void          setBoundsHalfExtents( const float3& halfExtents ) { _boundsHalfExtents = halfExtents; }
        /** @brief 이 표면이 @p agentType(빈 이름 = 기본 종류)을 맡으면 true 입니다. */
        bool coversAgentType( const hashed_string& agentType, const hashed_string& defaultAgentType ) const;

    private:
        PROPERTY( Category = "Surface", DisplayName = "Agent Types", Tooltip = "Agent kinds baked by this surface (empty = the default kind)" )
        vector<hashed_string> _listAgentType;
        PROPERTY( Category = "Surface", DisplayName = "Geometry", Tooltip = "Collect render meshes, static physics colliders or both" )
        NavGeometrySource _geometrySource;
        PROPERTY( Category = "Surface", DisplayName = "Exclude Tags", Tooltip = "Objects with one of these tags (and their children) are left out" )
        vector<TagID> _listExcludeTag;
        PROPERTY( Category = "Surface", DisplayName = "Bounds Half Extents", Tooltip = "Bake only inside this box around the object (0 = everything)", Meta = "Units=m" )
        float3 _boundsHalfExtents;
    };
} // namespace sw
