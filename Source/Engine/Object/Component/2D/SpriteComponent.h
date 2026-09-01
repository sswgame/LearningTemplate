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

	REFLECT( Category = "Rendering 2D", DisplayName = "Sprite Component", Tooltip = "2D Sprite rendering component" )
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

		const string& getMeshName() const { return _meshName; }
		void		  setMeshName( const string& mesh ) { _meshName = mesh; }

		const string& getMaterialName() const { return _materialName; }
		void		  setMaterialName( const string& mtrl ) { _materialName = mtrl; }

		const string& getTextureName() const { return _textureName; }
		void		  setTextureName( const string& tex ) { _textureName = tex; }

		const string& getSpriteName() const { return _spriteName; }
		void		  setSpriteName( const string& sprite ) { _spriteName = sprite; }

	private:
		PROPERTY( Category = "Rendering", DisplayName = "Mesh", AssetPath, AssetType = "Mesh", Tooltip = "Mesh asset name", Alias = "Mesh" )
		string _meshName;
		PROPERTY( Category = "Rendering", DisplayName = "Material", AssetPath, AssetType = "Material", Tooltip = "Material asset name", Alias = "Material" )
		string _materialName;
		PROPERTY( Category = "Rendering", DisplayName = "Texture", AssetPath, AssetType = "Texture", Tooltip = "Texture asset name", Alias = "Texture" )
		string _textureName;
		PROPERTY( Category = "Rendering", DisplayName = "Sprite Clip", Tooltip = "Sprite clip identifier", Alias = "SpriteName" )
		string _spriteName;
	};
} // namespace sw
