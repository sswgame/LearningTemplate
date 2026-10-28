/**
 * @file ClothComponent.h
 * @brief 천 — 사각 격자 소프트 바디(Jolt `SoftBody`)를 오브젝트 자리에 걸고, 고정한 가장자리가 오브젝트를 따라 움직입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct RHIVertex;

    class ValidationContext;

    /** @brief 천 격자의 어느 정점을 오브젝트에 고정하는가입니다. */
    ENUM()
    enum class ClothPinMode : uint8
    {
        None = 0,   ///< 고정하지 않는다(떨어지는 천)
        TopEdge,    ///< 윗변 전체(커튼 · 망토)
        TopCorners, ///< 윗변 양 끝(깃발 · 해먹)
    };
} // namespace sw

namespace sw
{
    /**
     * @class ClothComponent
     * @brief 물리 천입니다(유니티 `Cloth` · 언리얼 Chaos Cloth 의 자리). 오브젝트 로컬의 XY 평면에 폭 × 높이 격자를 걸고(윗변이 오브젝트 자리,
     *        아래로 −Y), 고정 정점(`_pinMode`)은 프레임마다 오브젝트의 월드 자세를 따르며 나머지는 중력 · 충돌 · 구속으로 움직입니다.
     *        `_listPinBone` 을 정하면 이 오브젝트나 조상의 스켈레탈 메시 뼈를 따릅니다 — 하나면 그 뼈를 단단히, 둘 이상이면 윗변을 왼쪽부터 그 뼈들에
     *        나눠 가중한 스킨 구속(Jolt `Skinned`, 최대 거리 0)으로(망토: 왼 어깨 · 등 · 오른 어깨).
     * @details 시작한 뒤 첫 물리 프레임에 소프트 바디를 만들고, 값이 바뀌거나 순간이동하면 다시 만듭니다. 정점 위치(`getVertices`)는 물리 프레임 끝의
     *          월드 위치이고 격자 순서(행 우선, 윗줄부터)입니다. 오브젝트 배율은 쓰지 않습니다(크기는 `_width` · `_height`). 2D 물리에는 없습니다.
     *
     *          그리기: 같은 오브젝트에 `MeshComponent` 가 있으면 물리 프레임마다 정점으로 양면 삼각형 메시를 새로 만들어 건다(`makeRenderVertices` — 노멀은 격자
     *          이웃 면의 합, UV 는 격자 0..1, 정점 색 1). 머티리얼은 그 메시 컴포넌트가 정한다. 메시는 동적(`Mesh::setDynamic`)이라 업로드 큐를 거치지 않고
     *          씬 정점 풀이 그 구간만 올린다(정점 버퍼를 프레임마다 만들지 않는다). 메시 컴포넌트가 없으면 그리지 않는다(물리만 — 물리 디버그 그리기로 보인다).
     * @note 프레임마다 정점 (열 − 1) × (행 − 1) × 12 개를 풀의 새 구간으로 올린다(기본 11 × 11 은 1200 개). 옛 구간은 앞 프레임이 끝난 뒤 돌아간다.
     */
    REFLECT( Category = "Physics", DisplayName = "Cloth", Tooltip = "Grid soft body hanging from this object; pinned vertices follow the transform", Meta = "Icon=Physics" )
    class SW_API ClothComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        ClothComponent();
        ~ClothComponent() override = default;

        /** @brief 마지막 물리 프레임의 정점 월드 위치입니다(격자 순서 — 행 우선, 윗줄부터). 천이 없으면 비어 있습니다. */
        const vector<float3>& getVertices() const { return _listVertex; }
        /** @brief 천이 만들어졌는지입니다. */
        bool isClothCreated() const { return _softBody.isValid(); }
        /** @brief 가로 정점 수입니다. */
        uint32 getColumnCount() const { return _columnCount; }
        /** @brief 세로 정점 수입니다. */
        uint32 getRowCount() const { return _rowCount; }
        /** @brief 고정 방식을 바꿉니다. 다음 물리 프레임에 천을 다시 만듭니다. */
        void setPinMode( ClothPinMode pinMode );
        /**
         * @brief 이 컴포넌트의 값으로 만든 소프트 바디 서술자입니다(테스트 · 도구가 본다).
         * @param position @p rotation 천을 걸 월드 자세(윗변 가운데)입니다.
         */
        PhysicsSoftBodyDesc makeSoftBodyDesc( const float3& position, const quaternion& rotation, uint8 layer ) const;
        /**
         * @brief 격자 정점(월드, `getVertices` 순서)으로 그릴 양면 삼각형 목록을 만듭니다. 위치와 노멀은 @p worldToLocal 로 메시 공간에 옮깁니다.
         * @details 앞면은 격자 삼각형의 감김(윗줄이 위인 정면에서 +Z), 뒷면은 감김과 노멀을 뒤집는다 — 이 엔진의 앞면 규약(바깥을 향한 감김)이다.
         * @return 정점 수가 열 × 행이 아니면 false 이고 @p outListVertex 는 비어 있습니다.
         */
        [[nodiscard]] bool makeRenderVertices( const vector<float3>& listWorldVertex, const float4x4& worldToLocal, vector<RHIVertex>& outListVertex ) const;

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        /** @brief 스텝이 끝난 정점 위치를 읽어 둡니다. */
        void endPhysicsFrame( ScenePhysics& physics, float32 alpha ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    public:
        /** @brief 고정 정점이 따를 본들을 정합니다(`_listPinBone`). 다음 물리 프레임에 천을 다시 만듭니다. */
        void setPinBones( const vector<hashed_string>& listBoneName );
        /**
         * @brief 천 몸이 서는 월드 자세입니다. 본이 하나면 그 본(고정 정점이 본을 단단히 따른다), 없거나 둘 이상이면 오브젝트 자세입니다.
         * @return 정한 본 가운데 스켈레탈 메시나 본이 없는 것이 있으면 false 입니다(천을 만들지 않는다 — 오브젝트 자세로 대신하지 않는다).
         */
        [[nodiscard]] bool findPinPose( float3& outPosition, quaternion& outRotation ) const;
        /** @brief 본이 둘 이상이고 고정 정점이 있으면 true 입니다 — 고정 정점이 뼈 가중 스킨 구속(Jolt `Skinned`)이 됩니다. */
        bool isSkinnedPin() const;
        /** @brief 윗변 열 @p column 의 고정 정점이 따를 뼈 가중입니다(뼈 i 는 열 비율 i / (뼈 수 - 1) 자리, 이웃한 두 뼈 사이를 선형으로). 본이 둘 이상일 때만 뜻이 있습니다. */
        void computePinSkinWeights( uint32 column, PhysicsSoftBodySkinnedVertex& outSkinned ) const;

    private:
        /** @brief 이 오브젝트부터 조상으로 올라가 처음 만난 스켈레탈 메시의 본 @p boneName 의 월드 행렬입니다. 없으면 false 입니다. */
        [[nodiscard]] bool findBoneWorld( const hashed_string& boneName, float4x4& outBoneWorld ) const;
        /** @brief `_listPinBone` 순서대로 본 월드 행렬을 채웁니다. 하나라도 없으면 false 입니다. */
        [[nodiscard]] bool findPinBoneWorlds( vector<float4x4>& outListBoneWorld ) const;
        /** @brief 스킨 구속에 이번 뼈 자세를 줍니다. @p bHardSkinAll 이면 스킨 위치에 바로 둡니다(만든 프레임). */
        void applySkinningPose( ScenePhysics& physics, bool bHardSkinAll );
        /** @brief 같은 오브젝트의 `MeshComponent` 에 이번 정점으로 만든 동적 메시를 겁니다. 메시 컴포넌트나 정점이 없으면 아무것도 하지 않습니다. */
        void updateRenderMesh();
        /** @brief `_layer` 가 물리 레이어 표에 있는지 봅니다(`ComponentNameValidation`). */
        void validateLayer( ValidationContext& context ) const;

        PROPERTY( Category = "Cloth", Min = 0.01, Tooltip = "Width along the local X axis", Units = m )
        float32 _width;
        PROPERTY( Category = "Cloth", Min = 0.01, Tooltip = "Height hanging down the local -Y axis", Units = m )
        float32 _height;
        PROPERTY( Category = "Cloth", Min = 2, Max = 64, Tooltip = "Vertices along the width" )
        uint32 _columnCount;
        PROPERTY( Category = "Cloth", Min = 2, Max = 64, Tooltip = "Vertices along the height" )
        uint32 _rowCount;
        PROPERTY( Category = "Cloth", Tooltip = "Which vertices follow this object" )
        ClothPinMode _pinMode;
        PROPERTY( Category = "Cloth", Tooltip = "Skeletal mesh bones the pinned vertices follow: none follows the object, one follows that bone rigidly, two or more skin the top edge across them (left to right)" )
        vector<hashed_string> _listPinBone;
        PROPERTY( Category = "Cloth", Min = 0.001, Tooltip = "Total mass of the free vertices", Units = kg )
        float32 _mass;
        PROPERTY( Category = "Stiffness", Min = 0.0, Tooltip = "Edge stretch compliance (inverse stiffness); 0 does not stretch" )
        float32 _stretchCompliance;
        PROPERTY( Category = "Stiffness", Min = 0.0, Tooltip = "Diagonal shear compliance" )
        float32 _shearCompliance;
        PROPERTY( Category = "Stiffness", Min = 0.0, Tooltip = "Bend compliance between neighbouring triangles; larger folds more easily" )
        float32 _bendCompliance;
        PROPERTY( Category = "Simulation", Min = 0.0, Tooltip = "Linear velocity damping" )
        float32 _linearDamping;
        PROPERTY( Category = "Simulation", Min = 0.0, Tooltip = "Friction against other bodies" )
        float32 _friction;
        PROPERTY( Category = "Simulation", Tooltip = "Multiplier on scene gravity" )
        float32 _gravityFactor;
        PROPERTY( Category = "Simulation", Min = 0.0, Tooltip = "Collision radius of each vertex", Units = m )
        float32 _vertexRadius;
        PROPERTY( Category = "Simulation", Min = 1, Max = 32, Tooltip = "Constraint solver iterations per step" )
        uint32 _iterationCount;
        PROPERTY( Category = "Simulation", Tooltip = "Collision layer name from the physics settings", Validate = validateLayer )
        hashed_string _layer;

        vector<float3>        _listVertex; ///< 마지막 물리 프레임 끝의 정점 월드 위치(게임 스레드가 쓴다)
        PhysicsSoftBodyHandle _softBody;
        uint8                 _bPinBoneErrorReported; ///< 설정 오류(본 없음 · 스켈레탈 메시 덮어쓰기)를 이미 냈다(본을 바꾸면 다시 낸다)
    };
} // namespace sw
