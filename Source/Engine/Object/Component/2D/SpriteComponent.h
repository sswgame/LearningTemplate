/**
 * @file SpriteComponent.h
 * @brief Sprite Mesh Rendering Component for 2D objects
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
	namespace generated
	{
		struct sw_SpriteComponent_Registrar;
	} // namespace generated

	REFLECT()
	class SW_API SpriteComponent : public MeshComponent
	{
		friend struct ::sw::generated::sw_SpriteComponent_Registrar;

	public:
		REFLECT_BODY();
		SpriteComponent();
		virtual ~SpriteComponent() override						 = default;
		SpriteComponent( SpriteComponent&& ) noexcept			 = default;
		SpriteComponent& operator=( SpriteComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		string getMeshName() const { return _meshName; }
		void   setMeshName( const string& mesh ) { _meshName = mesh; }

		string getMaterialName() const { return _materialName; }
		void   setMaterialName( const string& mtrl ) { _materialName = mtrl; }

		string getTextureName() const { return _textureName; }
		void   setTextureName( const string& tex ) { _textureName = tex; }

		string getSpriteName() const { return _spriteName; }
		void   setSpriteName( const string& sprite ) { _spriteName = sprite; }

	private:
		PROPERTY( Alias = "Mesh" )
		string _meshName;
		PROPERTY( Alias = "Material" )
		string _materialName;
		PROPERTY( Alias = "Texture" )
		string _textureName;
		PROPERTY( Alias = "SpriteName" )
		string _spriteName;
	};
} // namespace sw
