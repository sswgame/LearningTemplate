#include "pch.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/EcsDataUtil.h"

namespace sw
{
	void MeshComponent::onBeginPlay()
	{
		SceneComponent::onBeginPlay();
		ensureMeshData();
	}

	Component::EcsDataView MeshComponent::ensureEcsData()
	{
		MeshData* pData = ensureMeshData();
		return { pData, MeshData::StaticType() };
	}

	Component::EcsDataView MeshComponent::getEcsData() const
	{
		return { getMeshData(), MeshData::StaticType() };
	}

	MeshData* MeshComponent::getMeshData() const
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			return pGameObject->getComponent<MeshData>().get();
		return nullptr;
	}

	MeshData* MeshComponent::ensureMeshData()
	{
		return sw::ensureEcsData<MeshData>( getOwner(), getTypeInfo() );
	}

	void MeshComponent::setMesh( shared_ptr<Mesh> mesh )
	{
		MeshData* pData = ensureMeshData();
		if ( pData != nullptr )
			pData->_mesh = std::move( mesh );
	}

	shared_ptr<Mesh> MeshComponent::getMesh() const
	{
		const MeshData* pData = getMeshData();
		if ( pData != nullptr )
			return pData->_mesh;
		return {};
	}

	void MeshComponent::setMaterial( Material* pMaterial )
	{
		MeshData* pData = ensureMeshData();
		if ( pData != nullptr )
			pData->_pMaterial = pMaterial;
	}

	Material* MeshComponent::getMaterial() const
	{
		const MeshData* pData = getMeshData();
		if ( pData != nullptr )
			return pData->_pMaterial;
		return nullptr;
	}

	void MeshComponent::setMaterialInstance( shared_ptr<MaterialInstance> instance )
	{
		MeshData* pData = ensureMeshData();
		if ( pData != nullptr )
			pData->_materialInstance = std::move( instance );
	}

	shared_ptr<MaterialInstance> MeshComponent::getMaterialInstance() const
	{
		const MeshData* pData = getMeshData();
		if ( pData != nullptr )
			return pData->_materialInstance;
		return {};
	}

	void MeshComponent::setBlendMode( RHIBlendMode mode )
	{
		MeshData* pData = ensureMeshData();
		if ( pData != nullptr )
			pData->_blendMode = mode;
	}

	RHIBlendMode MeshComponent::getBlendMode() const
	{
		const MeshData* pData = getMeshData();
		if ( pData != nullptr )
			return pData->_blendMode;
		return RHIBlendMode::Opaque;
	}

	void MeshComponent::setBoundsRadius( float32 radius )
	{
		MeshData* pData = ensureMeshData();
		if ( pData != nullptr )
			pData->_boundsRadius = radius;
	}

	float32 MeshComponent::getBoundsRadius() const
	{
		const MeshData* pData = getMeshData();
		if ( pData != nullptr )
			return pData->_boundsRadius;
		return 0.866f;
	}

	void MeshComponent::setVisible( bool bVisible )
	{
		MeshData* pData = ensureMeshData();
		if ( pData != nullptr )
			pData->_bVisible = bVisible ? 1 : 0;
	}

	bool MeshComponent::isVisible() const
	{
		const MeshData* pData = getMeshData();
		if ( pData != nullptr )
			return pData->_bVisible != 0;
		return true;
	}
} // namespace sw
