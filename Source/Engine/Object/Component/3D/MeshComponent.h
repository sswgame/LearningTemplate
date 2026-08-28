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

	class Mesh;
	class Material;
	class MaterialInstance;

	/**
	 * @class MeshComponent
	 * @brief Drawable mesh attached to a GameObject (world transform from SceneComponent)
	 */
	REFLECT( Category = "Rendering 3D", DisplayName = "Mesh Component", Tooltip = "3D Static Mesh Renderer" )
	class SW_API MeshComponent : public SceneComponent
	{
		friend struct ::sw::generated::sw_MeshComponent_Registrar;

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
		void setMesh( shared_ptr<Mesh> mesh ) { _mesh = std::move( mesh ); }
		/** @brief 메시를 반환합니다. */
		shared_ptr<Mesh> getMesh() const { return _mesh; }

		/** @brief 머티리얼을 설정합니다. */
		void setMaterial( Material* pMaterial ) { _pMaterial = pMaterial; }
		/** @brief 머티리얼을 반환합니다. */
		Material* getMaterial() const { return _pMaterial; }

		/** @brief 머티리얼 인스턴스를 설정합니다. */
		void setMaterialInstance( shared_ptr<MaterialInstance> instance ) { _materialInstance = std::move( instance ); }
		/** @brief 머티리얼 인스턴스를 반환합니다. */
		shared_ptr<MaterialInstance> getMaterialInstance() const { return _materialInstance; }

		/** @brief 블렌드 모드를 설정합니다. */
		void setBlendMode( RHIBlendMode mode ) { _blendMode = mode; }
		/** @brief 블렌드 모드를 반환합니다. */
		RHIBlendMode getBlendMode() const { return _blendMode; }

		/** @brief 바운드 반지름을 설정합니다. */
		void setBoundsRadius( float32 radius ) { _boundsRadius = radius; }
		/** @brief 바운드 반지름을 반환합니다. */
		float32 getBoundsRadius() const { return _boundsRadius; }

		/** @brief 가시 여부를 설정합니다. */
		void setVisible( bool bVisible ) { _bVisible = bVisible ? SW_TRUE : SW_FALSE; }
		/** @brief 가시 여부를 반환합니다. */
		bool isVisible() const { return _bVisible == SW_TRUE; }

	private:
		shared_ptr<Mesh>			 _mesh;
		Material*					 _pMaterial;
		shared_ptr<MaterialInstance> _materialInstance;
		PROPERTY( Category = "Rendering", DisplayName = "Mesh Asset", AssetPath, AssetType = "Mesh", Tooltip = "Mesh asset name or path" )
		string _meshId;
		PROPERTY( Category = "Rendering", DisplayName = "Bounds Radius", Tooltip = "Bounding sphere radius", Min = 0.0, Meta = "Units=m" )
		float32 _boundsRadius;
		PROPERTY( Category = "Rendering", DisplayName = "Blend Mode", Tooltip = "RHI blend mode for rasterization" )
		RHIBlendMode _blendMode;
		uint8		 _bVisible : 1;
		uint8		 _reserved : 7;
	};
} // namespace sw
