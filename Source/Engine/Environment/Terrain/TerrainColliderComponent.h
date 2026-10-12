/**
 * @file TerrainColliderComponent.h
 * @brief 같은 오브젝트의 지형(`TerrainComponent`)을 정적 물리 바디로 만듭니다. 유니티의 TerrainCollider, 언리얼 랜드스케이프의 충돌 컴포넌트에 해당합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct PhysicsShapeDesc3D;

    class TerrainHeightfield;

    /**
     * @struct TerrainCollisionUtil
     * @brief 지형 높이장을 물리 셰이프로 옮기는 어댑터입니다. 렌더 메시(LOD 0)와 높이 질의(`findHeightAt`)와 같은 삼각형을 냅니다.
     */
    struct SW_API TerrainCollisionUtil
    {
        /**
         * @brief 높이장을 삼각형 메시 셰이프(`PhysicsShapeType3D::TriangleMesh`)로 만듭니다.
         * @details 점은 샘플 (0, 0) 기준의 로컬 위치이고(바디는 `getOrigin()` 에 둔다), 행 우선으로 N² 개다. 셀은 대각선 (x, z)–(x+1, z+1) 로 나눈
         *          삼각형 둘이며, 구멍 셀은 삼각형을 내지 않는다. 감김은 `(b−a)×(c−a)` 가 위(+Y)를 향한다(Jolt · Recast 가 같은 식으로 앞면을 정한다).
         *          Jolt `HeightFieldShape` 는 구멍을 샘플 단위로만 받아 이웃 셀까지 뚫리므로, 셀 단위 구멍을 정확히 옮기는 이 메시를 쓴다.
         * @return 높이장이 비었거나 삼각형이 하나도 없으면 false 이고 @p outShape 는 비어 있습니다.
         */
        [[nodiscard]] static bool makeCollisionMesh( const TerrainHeightfield& heightfield, PhysicsShapeDesc3D& outShape );
    };
} // namespace sw

namespace sw
{
    /**
     * @class TerrainColliderComponent
     * @brief 같은 오브젝트의 `TerrainComponent` 높이장으로 정적 바디 하나를 만듭니다. 지형이 다시 읽히면(`reloadTerrain`) 다음 물리 프레임에 바디를 다시 만듭니다.
     * @details 바디는 플레이 중에만 있고(다른 물리 컴포넌트와 같다), 높이장 원점에 회전 없이 놓인다(지형이 오너의 회전과 크기를 보지 않는 것과 같다).
     *          충돌 레이어와 재질은 이 컴포넌트의 속성이다. 지형이 없거나 비었으면 바디를 만들지 않는다.
     */
    REFLECT( Category = "Environment", DisplayName = "Terrain Collider", Tooltip = "Static physics collision built from the terrain on the same object" )
    class SW_API TerrainColliderComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        TerrainColliderComponent();
        ~TerrainColliderComponent() override = default;

        /** @brief 지형 바디 핸들입니다(시작 전이거나 지형이 없으면 무효). */
        PhysicsBodyHandle getBodyHandle() const { return _body; }
        /** @brief 마지막으로 만든 충돌 메시의 삼각형 수입니다(테스트와 진단). */
        uint32 getCollisionTriangleCount() const { return _triangleCount; }

        const hashed_string& getLayer() const { return _layer; }
        /** @brief 충돌 레이어 이름을 바꿉니다(바디를 다시 만든다). */
        void                 setLayer( const hashed_string& layer );
        const hashed_string& getMaterial() const { return _material; }
        /** @brief 물리 재질 이름을 바꿉니다(바디를 다시 만든다). */
        void setMaterial( const hashed_string& material );

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        PROPERTY( Category = "Collision", Tooltip = "Collision layer name from the physics settings" )
        hashed_string _layer;
        PROPERTY( Category = "Collision", Tooltip = "Physics material name (empty: the first material)" )
        hashed_string _material;

        PhysicsBodyHandle _body;
        uint32            _builtGeneration; ///< 바디를 만들 때 지형의 높이장 세대(`TerrainComponent::getHeightfieldGeneration`)
        uint32            _triangleCount;
    };
} // namespace sw
