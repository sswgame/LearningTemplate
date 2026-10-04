/**
 * @file FractureComponent.h
 * @brief 3D 파괴 가능 메시 — 오브젝트의 메시 옆 `.fracture`(모델 임포트가 쿠킹한 조각)를 Jolt 바디 · 스킨드 조각 메시로 부숩니다. 동작은 `FractureComponentBase`.
 * @details 오브젝트 구성: 온전할 때 그릴 `MeshComponent`(`.mesh`) + 충돌용 `RigidBodyComponent`(벽은 Static, 상자는 Dynamic) + 이것. 파쇄 경로를 비우면
 *          오브젝트 메시의 `_meshId`(`a/b.mesh`)에서 `a/b.fracture` 를 찾습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Destruction/FractureComponentBase.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    REFLECT( Category = "Physics", DisplayName = "Fracture", Tooltip = "Pre-fractured destructible mesh (.fracture): breaks into physics pieces under damage" )
    class SW_API FractureComponent : public FractureComponentBase
    {
    public:
        REFLECT_BODY();

        FractureComponent();
        ~FractureComponent() override = default;

        /** @brief 파쇄 에셋 경로를 바꿉니다(다음 물리 프레임에 읽는다 — 쪼갠 뒤에는 지금 조각을 지킨다). */
        void          setFracturePath( string_view path );
        const string& getFracturePath() const { return _fracturePath; }
        /** @brief 지금 쓰는(또는 쓸) 경로입니다 — 비었으면 오브젝트 메시 옆입니다. 없으면 빈 글입니다. */
        string resolveFracturePath() const;

    protected:
        bool                            uses2DPhysics() const override { return false; }
        shared_ptr<const FractureAsset> acquireFracture() override;
        bool                            isFractureStale() const override;

    private:
        PROPERTY( Category = "Fracture", DisplayName = "Fracture Asset", AssetPath, Tooltip = "The .fracture asset; empty uses the one beside the object's .mesh" )
        string _fracturePath;
        uint64 _acquiredGeneration; ///< 받았을 때의 캐시 세대(핫 리로드 판정)
    };
} // namespace sw
