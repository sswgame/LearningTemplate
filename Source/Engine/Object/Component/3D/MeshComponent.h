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

namespace sw
{
	class Mesh;
	class Material;
	class MaterialInstance;

	/**
	 * @brief Pure ECS Data Struct for Mesh Rendering
	 */
	REFLECT()
	struct SW_API MeshData
	{
		REFLECT_BODY();
		shared_ptr<Mesh>			 _mesh;
		Material*					 _pMaterial;
		shared_ptr<MaterialInstance> _materialInstance;
		PROPERTY()
		string _meshId;
		float32						 _boundsRadius;
		RHIBlendMode				 _blendMode;
		uint8						 _bVisible : 1;
		uint8						 _reserved : 7;

		MeshData()
			: _mesh{}
			, _pMaterial{ nullptr }
			, _materialInstance{}
			, _meshId{}
			, _boundsRadius{ 0.866f }
			, _blendMode{ RHIBlendMode::Opaque }
			, _bVisible{ 1 }
			, _reserved{ 0 }
		{
		}
	};

	/**
	 * @class MeshComponent
	 * @brief Drawable mesh attached to a GameObject (world transform from SceneComponent)
	 */
	REFLECT()
	class SW_API MeshComponent : public SceneComponent
	{
	public:
		REFLECT_BODY();

		/** @brief 메시/머티리얼 없는 기본값. */
		MeshComponent() = default;
		/** @brief 메시/머티리얼 참조를 끊습니다. */
		virtual ~MeshComponent() override = default;

		/** @brief 메시 참조를 이동합니다. */
		MeshComponent( MeshComponent&& ) noexcept = default;
		/** @brief 이동 대입입니다. */
		MeshComponent& operator=( MeshComponent&& ) noexcept = default;

		/** @brief 수명주기 초기화 */
		void onBeginPlay() override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		/** @brief 메시 ECS 데이터. 없으면 nullptr. */
		MeshData* getMeshData() const;
		/** @brief 메시 ECS 데이터를 확보합니다. */
		MeshData* ensureMeshData();
		/**
		 * @brief `_meshId` 프리미티브를 GPU 메시로 해석합니다.
		 * @details 이미 메시가 있으면 그대로 둡니다. 비어 있거나 "Cube"면 단위 큐브.
		 */
		void resolveRuntimeMesh();

		/** @brief 메시를 설정합니다. */
		void setMesh( shared_ptr<Mesh> mesh );
		/** @brief 메시를 반환합니다. */
		shared_ptr<Mesh> getMesh() const;

		/** @brief 머티리얼을 설정합니다. */
		void setMaterial( Material* pMaterial );
		/** @brief 머티리얼을 반환합니다. */
		Material* getMaterial() const;

		/** @brief 머티리얼 인스턴스를 설정합니다. */
		void setMaterialInstance( shared_ptr<MaterialInstance> instance );
		/** @brief 머티리얼 인스턴스를 반환합니다. */
		shared_ptr<MaterialInstance> getMaterialInstance() const;

		/** @brief 블렌드 모드를 설정합니다. */
		void setBlendMode( RHIBlendMode mode );
		/** @brief 블렌드 모드를 반환합니다. */
		RHIBlendMode getBlendMode() const;

		/** @brief 바운드 반지름을 설정합니다. */
		void setBoundsRadius( float32 radius );
		/** @brief 바운드 반지름을 반환합니다. */
		float32 getBoundsRadius() const;

		/** @brief 가시 여부를 설정합니다. */
		void setVisible( bool bVisible );
		/** @brief 가시 여부를 반환합니다. */
		bool isVisible() const;
	};
} // namespace sw
