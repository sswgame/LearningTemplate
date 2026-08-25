/**
 * @file SpriteComponent.h
 * @brief Sprite Mesh Rendering Component for 2D objects
 */
#pragma once
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	/**
	 * @brief Pure ECS Data Struct for Sprite Rendering
	 */
	REFLECT()
	struct SW_API SpriteData
	{
		REFLECT_BODY();
		string Mesh{ "" };
		string Material{ "" };
		string Texture{ "" };
		string SpriteName{ "" };
	};

	REFLECT()
	class SW_API SpriteComponent : public MeshComponent
	{
	public:
		REFLECT_BODY();
		SpriteComponent()										 = default;
		virtual ~SpriteComponent() override						 = default;
		SpriteComponent( SpriteComponent&& ) noexcept			 = default;
		SpriteComponent& operator=( SpriteComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		string getMeshName() const;
		void   setMeshName( const string& mesh );

		string getMaterialName() const;
		void   setMaterialName( const string& mtrl );

		string getTextureName() const;
		void   setTextureName( const string& tex );

		string getSpriteName() const;
		void   setSpriteName( const string& sprite );

		SpriteData* getSpriteData() const;
		SpriteData* ensureSpriteData();

		Component::EcsDataView ensureEcsData() override;
		Component::EcsDataView getEcsData() const override;
	};
} // namespace sw
