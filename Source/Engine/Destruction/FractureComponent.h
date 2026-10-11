/**
 * @file FractureComponent.h
 * @brief 3D 파괴 가능 메시입니다. 오브젝트의 메시 옆 `.fracture`(모델 임포트가 쿠킹한 조각)를 Jolt 바디와 스킨드 조각 메시로 부숩니다. 동작은 `FractureComponentBase` 에 있습니다.
 * @details 오브젝트는 다음처럼 구성한다. 온전할 때 그릴 `MeshComponent`(`.mesh`), 충돌용 `RigidBodyComponent`(벽은 Static, 상자는 Dynamic), 그리고 이 컴포넌트다.
 *          파쇄 경로를 비우면 오브젝트 메시의 `_meshID`(`a/b.mesh`)에서 `a/b.fracture` 를 찾는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Destruction/FractureComponentBase.h"
#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class FractureComponent;

    /**
     * @class FractureNavGeometrySource
     * @brief 파괴 오브젝트의 내비메시 베이크 기하입니다. 온전하면 기본 규칙(메시, 강체)으로 모으게 두고, 쪼갠 뒤에는 붙어 있는 조각의 삼각형만 냅니다.
     *        떨어진 덩어리와 파편은 움직이므로 포함하지 않습니다. 붙은 조각이 바뀌면 컴포넌트가 그 경계의 타일을 다시 베이크하게 합니다.
     */
    class SW_API FractureNavGeometrySource final : public INavGeometrySource
    {
    public:
        explicit FractureNavGeometrySource( FractureComponent& fracture )
            : _fracture{ fracture }
        {
        }

        const GameObject* getNavGeometryOwner() const override;
        bool              overridesOwnerGeometry() const override;
        void              collectNavGeometry( NavMeshGeometry& outGeometry, uint8 area ) const override;

    private:
        FractureComponent& _fracture;
    };
} // namespace sw

namespace sw
{
    REFLECT( Category = "Physics", DisplayName = "Fracture", Tooltip = "Pre-fractured destructible mesh (.fracture): breaks into physics pieces under damage" )
    class SW_API FractureComponent : public FractureComponentBase
    {
    public:
        REFLECT_BODY();

        FractureComponent();
        ~FractureComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 파쇄 에셋 경로를 바꿉니다. 다음 물리 프레임에 읽으며, 쪼갠 뒤에는 현재 조각을 유지합니다. */
        void          setFracturePath( string_view path );
        const string& getFracturePath() const { return _fracturePath; }
        /** @brief 현재 쓰는(또는 쓸) 경로를 반환합니다. 비었으면 오브젝트 메시 옆 경로입니다. 없으면 빈 문자열입니다. */
        string resolveFracturePath() const;

    protected:
        bool                            uses2DPhysics() const override { return false; }
        shared_ptr<const FractureAsset> acquireFracture() override;
        bool                            isFractureStale() const override;
        void                            onAnchoredShapeChanged() override;

    private:
        PROPERTY( Category = "Fracture", DisplayName = "Fracture Asset", AssetPath, Tooltip = "The .fracture asset; empty uses the one beside the object's .mesh" )
        string                    _fracturePath;
        FractureNavGeometrySource _navGeometrySource;
        SceneNavigation*          _pSceneNavigation;   ///< 기하 소스를 등록한 씬의 내비게이션이다(등록하는 동안만 유효하다).
        uint64                    _acquiredGeneration; ///< 받았을 때의 캐시 세대다(핫 리로드 판정에 쓴다).
    };
} // namespace sw
