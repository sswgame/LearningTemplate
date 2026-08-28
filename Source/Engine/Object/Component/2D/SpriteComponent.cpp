#include "pch.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	SpriteComponent::SpriteComponent()
		: _meshName{}
		, _materialName{}
		, _textureName{}
		, _spriteName{}
	{
	}

	void SpriteComponent::onBeginPlay()
	{
		MeshComponent::onBeginPlay();
		setTickGroup( TickGroup::PrePhysics );

		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			pGameObject->addTag( "Sprite"_tag );
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
		return _meshName;
	}

	void SpriteComponent::setMeshName( const string& mesh )
	{
		_meshName = mesh;
	}

	string SpriteComponent::getMaterialName() const
	{
		return _materialName;
	}

	void SpriteComponent::setMaterialName( const string& mtrl )
	{
		_materialName = mtrl;
	}

	string SpriteComponent::getTextureName() const
	{
		return _textureName;
	}

	void SpriteComponent::setTextureName( const string& tex )
	{
		_textureName = tex;
	}

	string SpriteComponent::getSpriteName() const
	{
		return _spriteName;
	}

	void SpriteComponent::setSpriteName( const string& sprite )
	{
		_spriteName = sprite;
	}
} // namespace sw
