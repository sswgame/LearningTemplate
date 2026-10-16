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

#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
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
     * @details 시작한 뒤 첫 물리 프레임에 소프트 바디를 만들고, 값이 바뀌거나 순간이동하면 다시 만듭니다. 정점 위치(`getVertices`)는 물리 프레임 끝의
     *          월드 위치이고 격자 순서(행 우선, 윗줄부터)입니다. 오브젝트 배율은 쓰지 않습니다(크기는 `_width` · `_height`). 2D 물리에는 없습니다.
     *          정점을 그리는 동적 메시는 아직 없다 — 지금은 물리 디버그 그리기(`drawDebug`)로 보인다.
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

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        /** @brief 스텝이 끝난 정점 위치를 읽어 둡니다. */
        void endPhysicsFrame( ScenePhysics& physics, float32 alpha ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
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
    };
} // namespace sw
