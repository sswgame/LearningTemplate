/**
 * @file SurfaceDeformMeshComponent.h
 * @brief 같은 오브젝트의 지면 메시에 씬의 표면 변형 텍스처와 창 값을 싣습니다 — 그 메시의 지면 머티리얼(`surface_deform.hlsl`)이 정점을 내리고 법선을 기울인다.
 * @details 변형 층(`SurfaceDeformSystem`)의 바인딩 번호가 바뀔 때만(텍스처를 만들 때 · 창이 옮겨 갈 때) 메시의 머티리얼 인스턴스에 `_deformMap` · `_deformParams` ·
 *          `_deformTexel` 을 다시 쓴다. 인스턴스가 없으면 메시 머티리얼에서 하나 만든다(`CharacterSurfaceBinding` 과 같은 길). 지면 메시는 정점이 촘촘해야
 *          자국이 보인다(내장 도형 `PlaneDense`). 메시가 없거나 머티리얼에 변형 칸이 없으면 오류를 한 번 남긴다 — 다른 머티리얼로 대신하지 않는다.
 *          언리얼에서 랜드스케이프 머티리얼이 런타임 가상 텍스처를 읽는 자리, 유니티에서 지면 머티리얼에 렌더 텍스처를 거는 자리다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class SurfaceDeformMeshComponent
     * @brief 지면 메시가 표면 변형 층을 읽게 머티리얼 값을 싣습니다(파일 머리말 참고).
     */
    REFLECT( Category = "Environment", DisplayName = "Surface Deform Mesh", Tooltip = "Feeds the scene's surface deform layer (footprints, tracks, whirls) to this object's ground mesh material" )
    class SW_API SurfaceDeformMeshComponent : public Component
    {
    public:
        REFLECT_BODY();

        SurfaceDeformMeshComponent();
        virtual ~SurfaceDeformMeshComponent() override = default;

        void onTick( float32 deltaTime ) override;

        /** @brief 마지막으로 머티리얼에 실은 변형 층 바인딩 번호입니다(0 = 아직 싣지 않았다). */
        uint32 getBoundRevision() const { return _boundRevision; }

    private:
        /** @brief 변형 층의 바인딩이 바뀌었으면 메시 머티리얼 인스턴스에 값을 싣습니다(틱 뒤 게임 스레드). */
        void bindMaterial( GameObjectManager& manager );

    private:
        uint32 _boundRevision;
    };
} // namespace sw
