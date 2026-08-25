#include "pch.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"

#include "Engine/Object/Component/EcsDataUtil.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void SpriteComponent::onBeginPlay()
	{
		MeshComponent::onBeginPlay();
		setTickGroup( TickGroup::PrePhysics );

		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
		{
			pGameObject->addTag( "Sprite"_tag );
			if ( pGameObject->getComponent<SpriteData>() == nullptr )
				pGameObject->addComponent<SpriteData>();
		}
	}

	void SpriteComponent::onEndPlay()
	{
		MeshComponent::onEndPlay();
	}

	void SpriteComponent::onTick( float32 deltaTime )
	{
		MeshComponent::onTick( deltaTime );
	}

	string SpriteComponent::getMeshName() const
	{
		const SpriteData* pData = getSpriteData();
		if ( pData != nullptr )
			return pData->Mesh;
		return "";
	}

	void SpriteComponent::setMeshName( const string& mesh )
	{
		SpriteData* pData = getSpriteData();
		if ( pData != nullptr )
			pData->Mesh = mesh;
	}

	string SpriteComponent::getMaterialName() const
	{
		const SpriteData* pData = getSpriteData();
		if ( pData != nullptr )
			return pData->Material;
		return "";
	}

	void SpriteComponent::setMaterialName( const string& mtrl )
	{
		SpriteData* pData = getSpriteData();
		if ( pData != nullptr )
			pData->Material = mtrl;
	}

	string SpriteComponent::getTextureName() const
	{
		const SpriteData* pData = getSpriteData();
		if ( pData != nullptr )
			return pData->Texture;
		return "";
	}

	void SpriteComponent::setTextureName( const string& tex )
	{
		SpriteData* pData = getSpriteData();
		if ( pData != nullptr )
			pData->Texture = tex;
	}

	string SpriteComponent::getSpriteName() const
	{
		const SpriteData* pData = getSpriteData();
		if ( pData != nullptr )
			return pData->SpriteName;
		return "";
	}

	void SpriteComponent::setSpriteName( const string& sprite )
	{
		SpriteData* pData = getSpriteData();
		if ( pData != nullptr )
			pData->SpriteName = sprite;
	}

	SpriteData* SpriteComponent::getSpriteData() const
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			return pGameObject->getComponent<SpriteData>().get();
		return nullptr;
	}

	SpriteData* SpriteComponent::ensureSpriteData()
	{
		return sw::ensureEcsData<SpriteData>( getOwner(), getTypeInfo() );
	}

	Component::EcsDataView SpriteComponent::ensureEcsData()
	{
		SpriteData* pData = ensureSpriteData();
		return { pData, SpriteData::StaticType() };
	}

	Component::EcsDataView SpriteComponent::getEcsData() const
	{
		return { getSpriteData(), SpriteData::StaticType() };
	}
} // namespace sw
