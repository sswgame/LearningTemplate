/**
 * @file MeshSectionComponent.h
 * @brief 같은 오브젝트의 주 메시 컴포넌트가 그리는 **메시 객체의 한 구간**을 자기 머티리얼로 그립니다(언리얼 Skeletal Mesh 의 Section 머티리얼, 유니티 SubMesh 의 자리).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class MeshSectionComponent
     * @brief 주 메시 컴포넌트(같은 오브젝트에서 구간 컴포넌트가 아닌 첫 `MeshComponent` — 보통 `SkeletalMeshComponent`)의 메시를 그대로 빌려 구간(`_sectionName`) 하나를 그립니다.
     * @details 메시 객체가 같아서 스킨 · 모프 결과도 한 벌이다(구간마다 스키닝하지 않는다). 포즈 · 얼굴 리그 · 후처리 리그는 주 컴포넌트에 붙는다.
     *          자기 `_meshID` 는 쓰지 않는다. 주 컴포넌트가 메시를 바꾸면(군중 · VAT · 다시 읽기) 그쪽이 알려 다시 빌린다(`MeshComponent::notifyMeshFollowers`).
     *          주 컴포넌트가 없으면 아무것도 그리지 않고 해석할 때 오류를 남긴다.
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Mesh Section", Tooltip = "Draws one section of the object's main mesh with its own material", Meta = "Icon=Cube" )
    class SW_API MeshSectionComponent : public MeshComponent
    {
    public:
        REFLECT_BODY();
        MeshSectionComponent()                   = default;
        virtual ~MeshSectionComponent() override = default;

        /** @brief 머티리얼을 잡고 주 컴포넌트의 메시를 빌립니다(자기 `_meshID` 는 해석하지 않는다). */
        void resolveRenderAssets() override;
        /** @brief 주 컴포넌트의 지금 메시를 빌립니다. 주 컴포넌트가 없으면 메시 없이 두고 오류를 남깁니다. */
        void followSourceMesh();
        /** @brief 이 구간이 따르는 주 메시 컴포넌트입니다. 없으면 nullptr 입니다. */
        MeshComponent* findSourceMeshComponent() const;
    };
} // namespace sw
