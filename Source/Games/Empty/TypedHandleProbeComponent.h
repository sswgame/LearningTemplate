/**
 * @file TypedHandleProbeComponent.h
 * @brief 인스펙터의 타입 컴포넌트 핸들 선택기를 화면에서 확인하는 시나리오(`editor.inspectorhandle`)가 쓰는 테스트용 컴포넌트입니다.
 *
 * @details 엔진과 키트의 컴포넌트 중에는 `TComponentHandle<T>` 를 PROPERTY 로 노출한 것이 없어서, 선택기를 그릴 프로퍼티가 이 컴포넌트에만 있습니다.
 *          새 게임을 시작할 때는 `BenchScene.*` 와 함께 지우는 파일이며, 이 컴포넌트를 쓰는 시나리오도 함께 지웁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class TypedHandleProbeComponent
     * @brief 메시 컴포넌트 하나를 타입 핸들로 가리킵니다. 값은 인스펙터의 선택기로만 정한다.
     */
    REFLECT()
    class TypedHandleProbeComponent : public Component
    {
    public:
        REFLECT_BODY();

        TypedHandleProbeComponent()                   = default;
        virtual ~TypedHandleProbeComponent() override = default;

        /** @brief CallInEditor 인자 선택기(프로퍼티와 같은 콤보) 확인용입니다 — 고른 메시 컴포넌트를 `_target` 에 넣어, 탐침 `Editor.SelectedHandle` 이 같은 값을 읽는다. */
        FUNCTION( Category = "Probe", DisplayName = "Assign Target", CallInEditor )
        void assignTarget( const TComponentHandle<MeshComponent>& mesh ) { _target = mesh; }

    private:
        PROPERTY( Category = "Probe", DisplayName = "Target Mesh", Tooltip = "Mesh component picked in the inspector" )
        TComponentHandle<MeshComponent> _target;
    };
} // namespace sw
