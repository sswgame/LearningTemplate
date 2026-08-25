/**
 * @file BoxCollider2DComponent.h
 * @brief 2D Box Collider Component
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
	/**
	 * @brief Pure ECS Data Struct for 2D Box Collider
	 */
	REFLECT()
	struct SW_API BoxCollider2DData
	{
		REFLECT_BODY();
		int32		 colliderType{ 0 };
		string		 offsetPos{ "" };
		string		 offsetScale{ "" };
		float2		 cachedMin{ 0.0f, 0.0f };
		float2		 cachedMax{ 0.0f, 0.0f };
		ObjectHandle physicsBody{};
	};

	REFLECT()
	class SW_API BoxCollider2DComponent : public SceneComponent
	{
	public:
		REFLECT_BODY();
		BoxCollider2DComponent()											   = default;
		virtual ~BoxCollider2DComponent() override							   = default;
		BoxCollider2DComponent( BoxCollider2DComponent&& ) noexcept			   = default;
		BoxCollider2DComponent& operator=( BoxCollider2DComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onDestroy() override;
		void onTick( float32 deltaTime ) override;

		int32 getColliderType() const;
		void  setColliderType( int32 type );

		string getOffsetPos() const;
		void   setOffsetPos( const string& pos );

		string getOffsetScale() const;
		void   setOffsetScale( const string& scale );

		float2 getOffsetPosition() const;
		float2 getOffsetScaleVec() const;

		void getBounds( float2& outMin, float2& outMax ) const;
		bool intersects( const BoxCollider2DComponent* pOther ) const;
		bool intersects( const float2& point ) const;
		bool intersects( const float2& minB, const float2& maxB ) const;

		BoxCollider2DData* getColliderData() const;
	};
} // namespace sw
