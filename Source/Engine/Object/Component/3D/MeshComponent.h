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
#include "Engine/Graphics/Shader/Binding/GpuSpriteInstanceData.h"
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
        /** @brief 읽은 메시 id · 머티리얼 참조를 자원으로 풉니다(`resolveRenderAssets`). 편집 중 되돌리기 · 프리팹 드래그로 다시 만든 메시가 그려진다. */
        void onPostLoad() override { resolveRenderAssets(); }
        /**
         * @brief `_meshId` 를 메시로 해석합니다. `.mesh` 경로면 메시 캐시(`MeshCache::acquire`), 아니면 내장 도형입니다.
         * @details 지금 메시가 지금 id 로 잡은 것이면 그대로 둡니다(`_resolvedMeshId` — 머티리얼의 `_acquiredMaterialPath` 와 같은 규칙).
         *          "메시가 있으면 그대로" 로 판정하면 id 를 바꿔도(인스펙터 · 붙여넣기 · 되돌리기) 옛 메시를 그린다. 비어 있으면 타입의
         *          기본(`getDefaultMeshId` — 메시는 단위 큐브, 스프라이트는 사각형)입니다.
         */
        void resolveRuntimeMesh();
        /** @brief 저장되는 메시 id 를 바꾸고 곧바로 해석합니다(프리미티브 이름 또는 `.mesh` 에셋 경로). */
        void setMeshId( string_view meshId );
        /** @brief 저장되는 메시 id 입니다(프리미티브 이름 또는 `.mesh` 에셋 경로). 비어 있으면 타입의 기본입니다. */
        const string& getMeshId() const { return _meshId; }

        /** @brief 메시를 설정합니다. 저장되지 않는 런타임 지정이고, 메시 id 가 바뀔 때까지 유지됩니다. */
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
         * @details 언리얼 `UMeshComponent::OverrideMaterials` · 유니티 `Renderer.sharedMaterials` 의 자리입니다. 런타임 지정(`setMaterial`)은
         *          저장되지 않으므로 씬 · 프리팹에 남길 머티리얼은 이 경로로 정합니다.
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

        /**
         * @brief GPU 인스턴스에 실을 스프라이트 프레임(UV 사각형) · 색입니다. 빌더가 `GpuInstance::_sprite` 로 옮깁니다.
         * @details 기본값(텍스처 전체 · 흰색)이면 아무 일도 하지 않습니다. 읽는 셰이더는 sprite2d.hlsl 이고, 값을 정하는 쪽은 파생
         *          (`SpriteComponent`)입니다 — 그래서 세터는 protected 입니다(읽는 셰이더가 없는 메시에 색을 줄 수 있는 것처럼 보이지 않게).
         */
        const GpuSpriteInstanceData& getSpriteInstanceData() const { return _spriteInstanceData; }
        /**
         * @brief 2D 픽셀 스냅 단위(자산 픽셀 하나의 월드 길이 = 1 / PPU, 0 = 끔)를 정합니다. 값이 달라졌을 때만 렌더 상태를 더티로 표시합니다.
         * @details 픽셀 퍼펙트 카메라가 줌이 바뀔 때 씬의 메시 모두에 알리고, 새로 등록되는 메시는 카메라 등록부의 값을 읽습니다. 읽는 셰이더는 sprite2d 입니다.
         */
        void setPixelSnapUnit( float32 unit );

        /**
         * @brief 투명 큐의 정렬 키입니다(`Render2DSettings::makeSortKey` — 정렬 레이어 · 레이어 안 순서). 0 은 `Default` 레이어 · 순서 0 입니다.
         * @details 투명 물체는 이 키가 작은 것부터 그려지고, 키가 같을 때만 깊이로 가립니다(유니티 `Renderer.sortingLayerID` · `sortingOrder`).
         *          저장하지 않습니다 — 저장되는 이름 · 순서는 파생(`SpriteComponent`)이 들고 여기로 풀어 넣습니다. 불투명은 이 키를 보지 않습니다.
         */
        void setSortKey( uint32 sortKey );
        /** @brief 투명 큐의 정렬 키입니다. 0 은 기본(`Default` 레이어 · 순서 0)입니다. */
        uint32 getSortKey() const { return _sortKey; }

        /** @brief 바운드 반지름(메시 공간)의 최소값을 설정합니다. 메시가 아는 경계보다 작으면 메시의 것이 쓰입니다(키우기만 한다). */
        void setBoundsRadius( float32 radius );
        /** @brief 바운드 반지름(메시 공간)입니다 — 적어 둔 값과 메시의 경계(`Mesh::getBoundingRadius`) 중 큰 쪽. 월드 반지름은 `getWorldBounds` 입니다. */
        float32 getBoundsRadius() const;
        /** @brief 월드 위치를 중심으로, 메시 반지름에 월드 행렬의 최대 축 스케일을 곱한 구입니다. GPU 컬링도 같은 값을 씁니다. */
        bool getWorldBounds( float3& outCenter, float32& outRadius ) const override;
        /** @brief 메시 공간의 경계(`Mesh::getLocalBoundsMin/Max`, 메시가 없으면 단위 상자)를 월드로 옮긴 상자입니다. */
        bool getWorldBox( AABB& outBox ) const override;

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
        /** @brief 스프라이트 프레임 · 색을 바꿉니다. 값이 달라졌을 때만 렌더 상태를 더티로 표시합니다(같은 프레임을 다시 넣는 애니메이터는 공짜). */
        void setSpriteInstanceData( const GpuSpriteInstanceData& data );

    private:
        /** @brief 등록부 슬롯입니다. 등록부(PrimitiveRegistry)만 만집니다. */
        uint32 getPrimitiveIndex() const { return _primitiveIndex; }
        /** @brief 등록부 슬롯을 설정합니다. */
        void                         setPrimitiveIndex( uint32 index );
        shared_ptr<Mesh>             _mesh;
        Material*                    _pMaterial;
        shared_ptr<MaterialInstance> _materialInstance;
        /** @brief 저장되는 메시 id 입니다. */
        PROPERTY( Category = "Rendering", DisplayName = "Mesh Asset", AssetPath, AssetType = "Mesh", Tooltip = "Mesh asset name or path" )
        string _meshId;
        /** @brief 저장되는 머티리얼 참조입니다. */
        PROPERTY( Category = "Rendering", DisplayName = "Material", AssetPath, AssetType = "Material", Tooltip = "Material asset; empty uses the scene default" )
        hashed_string _materialPath;
        hashed_string _acquiredMaterialPath; ///< 캐시에서 잡아 둔 경로(저장하지 않습니다). 인스펙터가 `_materialPath` 를 먼저 고쳐 써도 이것으로 놓습니다
        hashed_string _resolvedMeshId;       ///< `_mesh` 가 어느 메시 id 의 것인지(저장하지 않습니다). 지금 id 와 다르면 다시 잡습니다
        PROPERTY( Category = "Rendering", DisplayName = "Bounds Radius", Tooltip = "Bounding sphere radius", Min = 0.0, Meta = "Units=m" )
        float32 _boundsRadius;
        PROPERTY( Category = "Rendering", DisplayName = "Blend Mode", Tooltip = "RHI blend mode for rasterization" )
        RHIBlendMode _blendMode;
        PROPERTY( Category = "Rendering", DisplayName = "GPU Spin Seed", Tooltip = "Non-zero makes the GPU spin this instance; the seed picks its speed" )
        uint32 _gpuSpinSeed;
        /** @brief GPU 인스턴스의 스프라이트 칸입니다. 저장하지 않습니다 — 파생의 저장되는 값(프레임 · 색)에서 만듭니다. */
        GpuSpriteInstanceData _spriteInstanceData;
        /** @brief 투명 큐의 정렬 키입니다(0 = 기본). 저장하지 않습니다. */
        uint32 _sortKey;
        /** @brief 등록 시점에 받은 등록부입니다. 더티 표시는 소유자를 거치지 않고 여기로 바로 갑니다. */
        PrimitiveRegistry* _pPrimitiveRegistry;
        /** @brief 등록부 슬롯입니다. 등록되지 않았으면 kInvalidPrimitiveIndex 입니다. */
        uint32 _primitiveIndex;
        // "더티" 비트는 여기 없다. 등록부의 원자 플래그 하나가 기준이다. 주의: 더티를 이 비트필드에 두면 워커의 더티 쓰기가
        // `_bVisible` 과 같은 바이트를 읽고-고치고-쓰는 것이라, 이웃 비트를 만지는 스레드와 레이스다.
        /** @brief 그리는지 여부입니다. 저장됩니다(PROPERTY) — 숨긴 메시가 Stop · 되돌리기 · 씬 다시 열기 뒤에도 숨은 채로 남는다. */
        PROPERTY( Category = "Rendering", DisplayName = "Visible", Tooltip = "Draw this mesh" )
        uint8 _bVisible : 1;
        uint8 _reserved : 7;
    };
} // namespace sw
