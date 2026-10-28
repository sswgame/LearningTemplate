/**
 * @file TerrainColliderComponent.h
 * @brief 같은 오브젝트의 지형(`TerrainComponent`)을 정적 물리 바디로 만듭니다. 유니티의 TerrainCollider, 언리얼 랜드스케이프의 충돌 컴포넌트에 해당합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct PhysicsShapeDesc3D;
    struct TerrainChunkLayout;

    class TerrainHeightfield;
    class ValidationContext;

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
        /**
         * @brief 청크 (x, z) 의 셀만 같은 규칙으로 삼각형 메시로 만듭니다. 점은 그 청크의 샘플만이고 위치는 `makeCollisionMesh` 와 같은 기준(샘플 (0, 0))입니다.
         * @return 청크가 범위 밖이거나 모두 구멍이면 false 이고 @p outShape 는 비어 있습니다.
         */
        [[nodiscard]] static bool makeChunkCollisionMesh( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ,
                                                          PhysicsShapeDesc3D& outShape );
    };
} // namespace sw

namespace sw
{
    /**
     * @class TerrainColliderComponent
     * @brief 같은 오브젝트의 `TerrainComponent` 높이장을 렌더 청크마다 정적 바디 하나로 만듭니다(언리얼 랜드스케이프의 컴포넌트별 충돌과 같다).
     * @details 물리 프레임마다 청크 세대(`TerrainComponent::getChunkGenerations`)를 비교해 바뀐 청크의 바디만 다시 만든다 — `writeHeights` 로 작은 사각형을 쓰면
     *          그 사각형이 닿는 청크 몇 개만, `reloadTerrain` 이면 모두. 지형 전체를 한 바디로 두면 쓸 때마다 N² 점 메시 전체를 다시 짓는다
     *          (257² 에서 약 80 배 — `TerrainBenchTest.ColliderRebuildAfterBrushWrite`).
     *          바디는 플레이 중에만 있고(다른 물리 컴포넌트와 같다), 높이장 원점에 회전 없이 놓인다(지형이 오너의 회전과 크기를 보지 않는 것과 같다).
     *          충돌 레이어와 재질은 이 컴포넌트의 속성이다. 지형이 없거나 비었으면 바디를 만들지 않는다.
     */
    REFLECT( Category = "Environment", DisplayName = "Terrain Collider", Tooltip = "Static physics collision built from the terrain on the same object" )
    class SW_API TerrainColliderComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        TerrainColliderComponent();
        ~TerrainColliderComponent() override = default;

        /** @brief 살아 있는 청크 바디 수입니다(시작 전이거나 지형이 없으면 0). */
        uint32 getBodyCount() const;
        /** @brief 지금 청크 바디들의 삼각형 수 합입니다(테스트와 진단). */
        uint32 getCollisionTriangleCount() const { return _triangleCount; }
        /** @brief 바디를 만든(다시 만든) 청크 수의 누계입니다(테스트와 진단 — 높이 쓰기가 닿은 청크만 다시 만드는지 본다). */
        uint32 getChunkBuildCount() const { return _chunkBuildCount; }

        const hashed_string& getLayer() const { return _layer; }
        /** @brief 충돌 레이어 이름을 바꿉니다(바디를 다시 만든다). */
        void                 setLayer( const hashed_string& layer );
        const hashed_string& getMaterial() const { return _material; }
        /** @brief 물리 재질 이름을 바꿉니다(바디를 다시 만든다). */
        void setMaterial( const hashed_string& material );

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        void releasePhysics( ScenePhysics& physics ) override;
        /** @brief 청크마다 정적 바디 서술자 하나를 냅니다(`beginPhysicsFrame` 과 같은 메시 · 레이어 · 재질, 모두 구멍인 청크는 뺀다). */
        void appendStaticBodies3D( const ScenePhysics& physics, vector<PhysicsBodyDesc3D>& outListBody ) const override;

    private:
        /** @brief 청크 바디의 공통 서술자입니다(높이장 원점, 회전 없음, 정적, 이 컴포넌트의 레이어 · 재질). 셰이프 칸 하나는 부르는 쪽이 채운다. */
        PhysicsBodyDesc3D makeChunkBodyDesc( const ScenePhysics& physics, const float3& origin ) const;
        /** @brief `_layer` 가 물리 레이어 표에 있는지 봅니다(`ComponentNameValidation`). */
        void validateLayer( ValidationContext& context ) const;
        /** @brief `_material` 가 물리 재질 표에 있는지 봅니다(`ComponentNameValidation`). */
        void validateMaterial( ValidationContext& context ) const;

    private:
        PROPERTY( Category = "Collision", Tooltip = "Collision layer name from the physics settings", Validate = validateLayer )
        hashed_string _layer;
        PROPERTY( Category = "Collision", Tooltip = "Physics material name (empty: the first material)", Validate = validateMaterial )
        hashed_string _material;

        vector<PhysicsBodyHandle> _listBody;            ///< 청크마다 바디 하나(모두 구멍인 청크는 무효 핸들)
        vector<uint32>            _listBuiltGeneration; ///< 청크 바디를 만들 때의 청크 세대(`TerrainComponent::getChunkGenerations`)
        vector<uint32>            _listTriangleCount;   ///< 청크 바디의 삼각형 수
        uint32                    _triangleCount;
        uint32                    _chunkBuildCount;
    };
} // namespace sw
