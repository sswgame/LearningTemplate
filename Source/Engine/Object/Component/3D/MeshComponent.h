/**
 * @file MeshComponent.h
 * @brief 메시를 그리는 SceneComponent 입니다(3D 렌더링). 프리미티브 등록부를 거쳐 GpuScene 으로 들어갑니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;
    class Material;
    class MaterialInstance;
    class Mesh;
    class PrimitiveRegistry;

    /**
     * @class MeshComponent
     * @brief GameObject 에 붙어 그려지는 메시입니다(월드 트랜스폼은 SceneComponent 에서 얻습니다).
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Mesh Component", Tooltip = "3D Static Mesh Renderer" )
    class SW_API MeshComponent : public SceneComponent
    {
        /// 등록부 슬롯과 더티 플래그를 관리하는 유일한 주체입니다.
        friend class PrimitiveRegistry;

    public:
        REFLECT_BODY();

        /** @brief 메시 · 머티리얼 없이 만듭니다. */
        MeshComponent();
        /** @brief 기본 소멸자입니다(메시 · 머티리얼 인스턴스의 shared_ptr 을 놓습니다). */
        virtual ~MeshComponent() override = default;

        /** @brief 수명주기 초기화 */
        void onBeginPlay() override;

        /**
         * @brief 렌더 에셋(메시 · 머티리얼)을 풉니다. 시작(`onBeginPlay`) · 씬 초기화 · 머티리얼 참조 변경이 부릅니다.
         * @details 파생이 덧붙입니다(스프라이트는 텍스처 인스턴스). 로드는 값만 채우므로 이것이 값을 자원으로 바꾸는 자리입니다.
         */
        virtual void resolveRenderAssets();
        /**
         * @brief `_meshId` 프리미티브를 GPU 메시로 해석합니다.
         * @details 이미 메시가 있으면 그대로 둡니다. 비어 있으면 타입의 기본(`getDefaultMeshId` — 메시는 단위 큐브, 스프라이트는 사각형).
         */
        void resolveRuntimeMesh();
        /** @brief 저장되는 메시 id 입니다(프리미티브 이름). 비어 있으면 타입의 기본입니다. */
        const string& getMeshId() const { return _meshId; }

        /** @brief 메시를 설정합니다. */
        void setMesh( shared_ptr<Mesh> mesh );
        /** @brief 메시를 반환합니다. */
        const shared_ptr<Mesh>& getMesh() const { return _mesh; }
        /** @brief 원시 메시 포인터를 반환합니다. */
        Mesh* getRawMesh() const { return _mesh.get(); }

        /** @brief 머티리얼을 설정합니다. **저장되지 않는** 런타임 지정입니다 — 저장되는 참조는 `setMaterialPath` 입니다. */
        void setMaterial( Material* pMaterial );
        /** @brief 머티리얼을 반환합니다. 없으면 렌더러가 씬 기본 머티리얼을 씁니다. */
        Material* getMaterial() const { return _pMaterial; }

        /**
         * @brief 머티리얼 에셋 경로를 바꾸고 그 머티리얼을 잡습니다. 저장되는 참조이고, 빈 경로는 씬 기본 머티리얼입니다.
         * @details 언리얼 `UMeshComponent::OverrideMaterials` · 유니티 `Renderer.sharedMaterials` 의 자리입니다. 예전에는 메시의 머티리얼이
         *          날 포인터뿐이라 저장되지 않아, 씬 · 프리팹을 다시 열면 모든 메시가 씬 기본 머티리얼이 됐습니다.
         */
        void setMaterialPath( string_view path );
        /** @brief 저장되는 머티리얼 에셋 경로입니다. 비어 있으면 씬 기본 머티리얼입니다. */
        hashed_string getMaterialPath() const { return _materialPath; }
        /**
         * @brief `_materialPath`(비어 있으면 타입의 기본 `getDefaultMaterialPath`)의 머티리얼을 캐시에서 잡아 겁니다. 이미 그 경로를 잡았으면 아무것도 하지 않습니다.
         * @details 로드는 값만 채우므로 시작(`onBeginPlay`) · 씬 초기화 · 프로퍼티 편집 · 세터가 부릅니다(`resolveRuntimeMesh` 와 같은 자리).
         *          컴포넌트는 디바이스를 모르므로 디바이스 없이 잡고 GPU 업로드를 캐시에 맡깁니다(`MaterialCache::requestInitialize`).
         *          새 것을 건 **뒤에** 옛 것을 놓습니다 — 놓는 순간 캐시가 지울 수 있습니다. 경로가 비면 런타임 지정(`setMaterial`)은 건드리지
         *          않습니다(잡아 둔 것이 있을 때만 놓고 비웁니다).
         */
        void resolveMaterialAsset();

        /** @brief 머티리얼 인스턴스를 설정합니다. */
        void setMaterialInstance( shared_ptr<MaterialInstance> instance );
        /** @brief 머티리얼 인스턴스를 반환합니다. */
        const shared_ptr<MaterialInstance>& getMaterialInstance() const { return _materialInstance; }
        /** @brief 원시 머티리얼 인스턴스 포인터를 반환합니다. */
        MaterialInstance* getRawMaterialInstance() const { return _materialInstance.get(); }

        /** @brief 블렌드 모드를 설정합니다. */
        void setBlendMode( RHIBlendMode mode );
        /** @brief 블렌드 모드를 반환합니다. */
        RHIBlendMode getBlendMode() const { return _blendMode; }

        /**
         * @brief GPU 회전 애니메이션 시드를 설정합니다(0 이면 애니메이션 없음).
         * @details 0 이 아니면 GPUScene 인스턴스에 실려 `instanceanim.hlsl` 이 이 값을 해시해
         *          **인스턴스마다 다른 각속도**로 회전을 얹습니다. CPU 는 매 프레임 트랜스폼을 다시 쓰지 않아도
         *          되고, 회전은 전적으로 컴퓨트가 만듭니다. 시드가 다르면 속도도 다르므로 보통 인덱스 + 1 을 줍니다.
         */
        void setGpuSpinSeed( uint32 seed );
        /** @brief setGpuSpinSeed 로 정한 값입니다(0 이면 GPU 회전 없음). */
        uint32 getGpuSpinSeed() const { return _gpuSpinSeed; }

        /** @brief 바운드 반지름(메시 공간)을 설정합니다. */
        void setBoundsRadius( float32 radius );
        /** @brief 바운드 반지름(메시 공간)을 반환합니다. 월드 반지름은 `getWorldBounds` 입니다. */
        float32 getBoundsRadius() const { return _boundsRadius; }
        /** @brief 월드 위치를 중심으로, 메시 반지름에 월드 행렬의 최대 축 스케일을 곱한 구입니다. GPU 컬링도 같은 값을 씁니다. */
        bool getWorldBounds( float3& outCenter, float32& outRadius ) const override;

        /** @brief 가시 여부를 설정합니다. */
        void setVisible( bool bVisible );
        /** @brief 가시 여부를 반환합니다. */
        bool isVisible() const { return _bVisible == SW_TRUE; }

        /** @brief 등록되지 않은 프리미티브의 인덱스입니다. */
        static constexpr uint32 kInvalidPrimitiveIndex = 0xFFFFFFFFu;

        /**
         * @brief 렌더 스냅샷이 다시 읽어야 할 상태로 표시합니다.
         * @details 세터 · PROPERTY 편집 · 월드 트랜스폼 갱신이 모두 여기로 모입니다. 렌더러가 매 프레임
         *          전부 훑어 "뭐가 바뀌었나" 되묻는 대신, 바꾼 쪽이 알립니다.
         */
        void markRenderStateDirty();
        /** @brief 프리미티브 등록부에 자기를 넣습니다. 여기서 타입이 한 번 확정됩니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 프리미티브 등록부에서 자기를 빼고, 잡아 둔 머티리얼 참조를 놓습니다. 멱등입니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 인스펙터/직렬화가 PROPERTY 를 바꾸면 렌더 상태를 더티로 표시합니다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /**
         * @brief 쓰지 않습니다(`final`). 메시는 월드가 바뀌면 트랜스폼 칸에 적힌 프리미티브 번호로 등록부에 바로 더티가 찍힙니다.
         * @details 생성자에서 이 훅의 알림을 끕니다 — 틱 뒤 적용이 움직인 메시마다 컴포넌트를 건너다니지 않게 하려는 것입니다. 그래서 파생이
         *          덮어써도 불리지 않으므로 아예 막습니다.
         */
        void onWorldTransformUpdated() final {}
        /** @brief 소유 오브젝트가 켜지거나 꺼지면 렌더 상태를 더티로 표시합니다(스냅샷 포함 여부가 바뀐다). */
        void onOwnerActiveInHierarchyChanged() override;

    protected:
        /** @brief `_meshId` 가 비었을 때의 메시 id 입니다. 빈 글이면 단위 큐브입니다(`MeshUtil::acquirePrimitive`). */
        virtual string_view getDefaultMeshId() const { return {}; }
        /** @brief `_materialPath` 가 비었을 때의 머티리얼 경로입니다. 빈 것이면 씬 기본 머티리얼입니다(렌더러가 고른다). */
        virtual hashed_string getDefaultMaterialPath() const { return {}; }

    private:
        /** @brief 등록부 슬롯입니다. 등록부(PrimitiveRegistry)만 만집니다. */
        uint32 getPrimitiveIndex() const { return _primitiveIndex; }
        /** @brief 등록부 슬롯을 설정합니다. */
        void                         setPrimitiveIndex( uint32 index );
        shared_ptr<Mesh>             _mesh;
        Material*                    _pMaterial;
        shared_ptr<MaterialInstance> _materialInstance;
        /** @brief 저장되는 메시 id 입니다. `_meshName` 은 스프라이트가 따로 들던(읽는 곳 없던) 옛 칸 이름입니다. */
        PROPERTY( Category = "Rendering", DisplayName = "Mesh Asset", AssetPath, AssetType = "Mesh", Tooltip = "Mesh asset name or path", Alias = "_meshName, Mesh" )
        string _meshId;
        /** @brief 저장되는 머티리얼 참조입니다. `_materialName` 은 스프라이트가 따로 들던(읽는 곳 없던) 옛 칸 이름입니다. */
        PROPERTY( Category = "Rendering", DisplayName = "Material", AssetPath, AssetType = "Material", Tooltip = "Material asset; empty uses the scene default",
                  Alias = "_materialName, Material" )
        hashed_string _materialPath;
        hashed_string _acquiredMaterialPath; ///< 캐시에서 잡아 둔 경로(저장하지 않습니다). 인스펙터가 `_materialPath` 를 먼저 고쳐 써도 이것으로 놓습니다
        PROPERTY( Category = "Rendering", DisplayName = "Bounds Radius", Tooltip = "Bounding sphere radius", Min = 0.0, Meta = "Units=m" )
        float32 _boundsRadius;
        PROPERTY( Category = "Rendering", DisplayName = "Blend Mode", Tooltip = "RHI blend mode for rasterization" )
        RHIBlendMode _blendMode;
        PROPERTY( Category = "Rendering", DisplayName = "GPU Spin Seed", Tooltip = "Non-zero makes the GPU spin this instance; the seed picks its speed" )
        uint32 _gpuSpinSeed;
        /** @brief 등록 시점에 받은 등록부입니다. 더티 표시는 소유자를 거치지 않고 여기로 바로 갑니다. */
        PrimitiveRegistry* _pPrimitiveRegistry;
        /** @brief 등록부 슬롯입니다. 등록되지 않았으면 kInvalidPrimitiveIndex 입니다. */
        uint32 _primitiveIndex;
        // "더티" 비트는 여기 없다. 등록부의 원자 플래그 하나가 기준이다. 비트필드였을 때는 워커의 더티 쓰기가
        // `_bVisible` 과 같은 바이트를 읽고-고치고-쓰는 것이라, 이웃 비트를 만지는 스레드와 형식상 레이스였다.
        uint8 _bVisible : 1;
        uint8 _reserved : 7;
    };
} // namespace sw
