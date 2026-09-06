/**
 * @file MeshComponent.h
 * @brief SceneComponent that references a Mesh for FrameRenderer submission (3D Rendering)
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
    namespace generated
    {
        struct sw_MeshComponent_Registrar;
    } // namespace generated

    class GameObjectManager;
    class Material;
    class MaterialInstance;
    class Mesh;
    class PrimitiveRegistry;

    /**
     * @class MeshComponent
     * @brief Drawable mesh attached to a GameObject (world transform from SceneComponent)
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Mesh Component", Tooltip = "3D Static Mesh Renderer" )
    class SW_API MeshComponent : public SceneComponent
    {
        friend struct ::sw::generated::sw_MeshComponent_Registrar;
        /// 등록부 슬롯과 더티 플래그를 관리하는 유일한 주체.
        friend class PrimitiveRegistry;

    public:
        REFLECT_BODY();

        /** @brief 메시/머티리얼 없는 기본값. */
        MeshComponent();
        /** @brief 메시/머티리얼 참조를 끊습니다. */
        virtual ~MeshComponent() override = default;

        /** @brief 메시 참조를 이동합니다. */
        MeshComponent( MeshComponent&& ) noexcept = default;
        /** @brief 이동 대입입니다. */
        MeshComponent& operator=( MeshComponent&& ) noexcept = default;

        /** @brief 수명주기 초기화 */
        void onBeginPlay() override;

        /**
         * @brief `_meshId` 프리미티브를 GPU 메시로 해석합니다.
         * @details 이미 메시가 있으면 그대로 둡니다. 비어 있거나 "Cube"면 단위 큐브.
         */
        void resolveRuntimeMesh();

        /** @brief 메시를 설정합니다. */
        void setMesh( shared_ptr<Mesh> mesh );
        /** @brief 메시를 반환합니다. */
        const shared_ptr<Mesh>& getMesh() const { return _mesh; }
        /** @brief 원시 메시 포인터를 반환합니다. */
        Mesh* getRawMesh() const { return _mesh.get(); }

        /** @brief 머티리얼을 설정합니다. */
        void setMaterial( Material* pMaterial );
        /** @brief 머티리얼을 반환합니다. */
        Material* getMaterial() const { return _pMaterial; }

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

        /** @brief 바운드 반지름을 설정합니다. */
        void setBoundsRadius( float32 radius );
        /** @brief 바운드 반지름을 반환합니다. */
        float32 getBoundsRadius() const { return _boundsRadius; }

        /** @brief 가시 여부를 설정합니다. */
        void setVisible( bool bVisible );
        /** @brief 가시 여부를 반환합니다. */
        bool isVisible() const { return _bVisible == SW_TRUE; }

        /** @brief 등록되지 않은 프리미티브의 인덱스입니다. */
        static constexpr uint32 kInvalidPrimitiveIndex = 0xFFFFFFFFu;

        /**
         * @brief 렌더 스냅샷이 다시 읽어야 할 상태로 표시합니다.
         * @details 세터·PROPERTY 편집·월드 트랜스폼 갱신이 전부 여기로 모인다. 렌더러가 매 프레임
         *          전부 훑어 "뭐가 바뀌었나" 되묻는 대신, 바꾼 쪽이 알린다.
         */
        void markRenderStateDirty();
        /** @brief 프리미티브 등록부에 자기를 넣습니다. 여기서 타입이 한 번 확정된다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 프리미티브 등록부에서 자기를 뺍니다. 멱등입니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 인스펙터/직렬화가 PROPERTY 를 바꾸면 렌더 상태를 더티로 표시합니다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 월드 행렬이 다시 계산되면 렌더 상태를 더티로 표시합니다. */
        void onWorldTransformUpdated() override;

    private:
        /** @brief 등록부 슬롯. 등록부를 소유한 매니저만 만집니다. */
        uint32 getPrimitiveIndex() const { return _primitiveIndex; }
        /** @brief 등록부 슬롯을 설정합니다. */
        void setPrimitiveIndex( uint32 index ) { _primitiveIndex = index; }
        /** @brief 더티 목록에 이미 들어가 있으면 true. */
        bool isRenderStateDirty() const { return _bRenderDirty == SW_TRUE; }
        /** @brief 더티 목록 등재 여부를 설정합니다. */
        void setRenderStateDirty( bool bDirty ) { _bRenderDirty = bDirty ? SW_TRUE : SW_FALSE; }

        shared_ptr<Mesh>             _mesh;
        Material*                    _pMaterial;
        shared_ptr<MaterialInstance> _materialInstance;
        PROPERTY( Category = "Rendering", DisplayName = "Mesh Asset", AssetPath, AssetType = "Mesh", Tooltip = "Mesh asset name or path" )
        string _meshId;
        PROPERTY( Category = "Rendering", DisplayName = "Bounds Radius", Tooltip = "Bounding sphere radius", Min = 0.0, Meta = "Units=m" )
        float32 _boundsRadius;
        PROPERTY( Category = "Rendering", DisplayName = "Blend Mode", Tooltip = "RHI blend mode for rasterization" )
        RHIBlendMode _blendMode;
        /** @brief 등록 시점에 받은 등록부. 더티 표시는 여기로 바로 간다 — 소유자를 거치지 않는다. */
        PrimitiveRegistry* _pPrimitiveRegistry;
        /** @brief 등록부 슬롯. 미등록이면 kInvalidPrimitiveIndex. */
        uint32 _primitiveIndex;
        uint8  _bVisible     : 1;
        uint8  _bRenderDirty : 1;
        uint8  _reserved     : 6;
    };
} // namespace sw
