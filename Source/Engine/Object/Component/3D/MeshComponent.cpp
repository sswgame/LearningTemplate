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
		, _bVisible{ SW_TRUE }
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

} // namespace sw
