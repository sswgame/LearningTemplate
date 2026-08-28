#include "pch.h"

#include "Engine/Object/Component/3D/MeshComponent.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"

namespace sw
{
	MeshComponent::MeshComponent()
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

	void MeshComponent::onBeginPlay()
	{
		SceneComponent::onBeginPlay();
		resolveRuntimeMesh();
	}

	void MeshComponent::resolveRuntimeMesh()
	{
		if ( _mesh != nullptr )
			return;
		_mesh = Mesh::createPrimitive( _meshId );
	}

	void MeshComponent::setMesh( shared_ptr<Mesh> mesh )
	{
		_mesh = std::move( mesh );
	}

	shared_ptr<Mesh> MeshComponent::getMesh() const
	{
		return _mesh;
	}

	void MeshComponent::setMaterial( Material* pMaterial )
	{
		_pMaterial = pMaterial;
	}

	Material* MeshComponent::getMaterial() const
	{
		return _pMaterial;
	}

	void MeshComponent::setMaterialInstance( shared_ptr<MaterialInstance> instance )
	{
		_materialInstance = std::move( instance );
	}

	shared_ptr<MaterialInstance> MeshComponent::getMaterialInstance() const
	{
		return _materialInstance;
	}

	void MeshComponent::setBlendMode( RHIBlendMode mode )
	{
		_blendMode = mode;
	}

	RHIBlendMode MeshComponent::getBlendMode() const
	{
		return _blendMode;
	}

	void MeshComponent::setBoundsRadius( float32 radius )
	{
		_boundsRadius = radius;
	}

	float32 MeshComponent::getBoundsRadius() const
	{
		return _boundsRadius;
	}

	void MeshComponent::setVisible( bool bVisible )
	{
		_bVisible = bVisible ? SW_TRUE : SW_FALSE;
	}

	bool MeshComponent::isVisible() const
	{
		return _bVisible == SW_TRUE;
	}
} // namespace sw
