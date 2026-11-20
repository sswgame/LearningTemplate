/**
 * @file InstancedMeshComponent.h
 * @brief 같은 메시 · 머티리얼의 인스턴스 여럿을 한 배치로 그리는 씬 컴포넌트입니다(언리얼 `UInstancedStaticMeshComponent` 자리).
 * @details 인스턴스 자리는 이 컴포넌트에 상대적인 TRS 목록이다(`_listInstance`). 배치는 `MeshInstanceBatch` 하나이고 `PrimitiveRegistry` 에 등록되어 메시 컴포넌트와 같은 길로 모인다.
 *          쿠커가 Packed Level 을 구울 때 씬 인스턴스의 정적 메시 엔티티를 이 컴포넌트로 묶는다(`SceneCooker::packStaticMeshes`) — 에디터는 원본 엔티티를 편집한다.
 *          그리기만 묶는다: 충돌 · 상호작용이 필요한 엔티티(다른 컴포넌트를 가진 것)는 묶이지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;
    class Material;
    class MeshInstanceBatch;
    class PrimitiveRegistry;

    /** @brief 인스턴스 하나의 자리입니다(컴포넌트 공간 — 위치 · 오일러 회전(라디안, `SceneComponent` 와 같은 규칙) · 스케일). */
    REFLECT()
    struct SW_API InstancedMeshTransform
    {
        REFLECT_BODY();

        PROPERTY( Units = m )
        float3 _position{};
        PROPERTY()
        float3 _rotation{};
        PROPERTY()
        float3 _scale{ 1.0f, 1.0f, 1.0f };
    };

    /**
     * @class InstancedMeshComponent
     * @brief 파일 머리말 참고.
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Instanced Mesh Component", Tooltip = "Draws many copies of one mesh in a single batch (packed level instances)", Meta = "Icon=Cube" )
    class SW_API InstancedMeshComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        InstancedMeshComponent();
        ~InstancedMeshComponent() override;

        /** @brief 레지스트리를 잡고 배치를 짓습니다(메시가 읽히는 중이면 도착한 뒤). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 배치 · 머티리얼을 놓습니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 읽은 값으로 배치를 다시 짓습니다(워커에서 불리면 틱 뒤로 미룬다). */
        void onPostLoad() override;
        /** @brief 메시 · 머티리얼 · 인스턴스 목록이 바뀌면 배치를 다시 짓습니다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 컴포넌트가 움직이면(앵커 이동 · 원점 이동) 항목의 월드 행렬을 다시 씁니다. */
        void onWorldTransformUpdated() override;
        /** @brief 소유 오브젝트를 켜고 끄면 항목을 더티로 표시합니다(빌더가 다시 확인한다). */
        void onOwnerActiveInHierarchyChanged() override;

        /** @brief 메시 · 머티리얼 · 구간 · 인스턴스를 한 번에 정합니다(쿠커가 묶을 때). 등록되어 있으면 배치를 다시 짓습니다. */
        void setInstances( string_view meshID, string_view materialPath, hashed_string sectionName, vector<InstancedMeshTransform> listInstance );
        /** @brief 인스턴스 목록입니다. */
        const vector<InstancedMeshTransform>& getInstances() const { return _listInstance; }
        /** @brief 저장되는 메시 id 입니다(내장 도형 이름 또는 `.mesh` 경로). */
        const string& getMeshID() const { return _meshID.path(); }
        /** @brief 저장되는 머티리얼 경로입니다(비면 씬 기본). */
        const string& getMaterialPath() const { return _materialPath.path(); }
        /** @brief 그릴 메시 구간입니다(비면 메시 전체). */
        hashed_string getSectionName() const { return _sectionName; }
        /** @brief 배치를 지었으면 true 입니다(메시가 오기 전 · 등록 전에는 false). */
        bool isBatchBuilt() const { return _pBatch != nullptr; }

        /** @brief 인스턴스 자리 @p transform 의 컴포넌트 공간 행렬입니다(스케일 → 회전 → 이동, 행 벡터). */
        [[nodiscard]] static float4x4 makeLocalMatrix( const InstancedMeshTransform& transform );

    private:
        /** @brief 배치를 놓고 다시 짓습니다. 메시가 읽히는 중이면 도착을 기다린다(`updateMeshArrival`). */
        void rebuildBatch();
        /** @brief 배치 · 잡은 머티리얼을 놓습니다. */
        void releaseBatch();
        /** @brief 항목마다 월드 행렬(인스턴스 행렬 × 컴포넌트 월드)을 씁니다. */
        void writeEntryWorlds();
        /** @brief 씬 프레임의 도착 단계가 부릅니다 — 메시가 오면 배치를 짓고 기다리기를 끝냅니다. */
        static bool updateMeshArrival( Component& component, float32 deltaSeconds );

    private:
        PROPERTY( Category = "Rendering", DisplayName = "Mesh Asset", Tooltip = "Mesh asset path or built-in shape name" )
        AssetRef<MeshAssetFormat> _meshID;
        PROPERTY( Category = "Rendering", DisplayName = "Material", Tooltip = "Material asset; empty uses the scene default" )
        AssetRef<Material> _materialPath;
        PROPERTY( Category = "Rendering", DisplayName = "Section", Tooltip = "Mesh section to draw; empty draws the whole mesh" )
        hashed_string _sectionName;
        PROPERTY( Category = "Rendering", DisplayName = "Instances", Tooltip = "Placement of each copy relative to this component" )
        vector<InstancedMeshTransform> _listInstance;

        unique_ptr<MeshInstanceBatch> _pBatch;
        MeshHandle                    _meshLoad;             ///< 읽는 중인 메시(`.mesh` 경로일 때)
        Material*                     _pMaterial;            ///< 잡은 머티리얼(빌림 — `_acquiredMaterialPath` 로 놓는다)
        hashed_string                 _acquiredMaterialPath; ///< 머티리얼 캐시에서 잡은 경로(놓을 때 쓴다)
        PrimitiveRegistry*            _pPrimitiveRegistry;   ///< 등록된 동안의 레지스트리(빌림)
    };
} // namespace sw
