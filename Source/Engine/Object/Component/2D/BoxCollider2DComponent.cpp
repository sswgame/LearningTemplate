#include "pch.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/AABB.h"
#include "Engine/Physics/PhysicsWorld.h"

namespace sw
{
	namespace
	{
		AABB makeColliderAabb( const float2& minB, const float2& maxB )
		{
			AABB box;
			box._min = float3( minB._x, minB._y, 0.0f );
			box._max = float3( maxB._x, maxB._y, 0.0f );
			return box;
		}

		void unregisterPhysicsBody( BoxCollider2DData* pData, GameObjectManager* pManager )
		{
			if ( pData == nullptr || pManager == nullptr || pData->physicsBody.isValid() == false )
				return;
			pManager->getPhysicsWorld().removeBody( pData->physicsBody );
			pData->physicsBody = ObjectHandle{};
		}

		void syncPhysicsBody( BoxCollider2DComponent& collider, BoxCollider2DData* pData )
		{
			if ( pData == nullptr )
				return;
			GameObject* pOwner = collider.getOwner();
			if ( pOwner == nullptr || pOwner->getManager() == nullptr )
				return;

			GameObjectManager* pManager = pOwner->getManager();
			float2			   minB{};
			float2			   maxB{};
			collider.getBounds( minB, maxB );
			pData->cachedMin = minB;
			pData->cachedMax = maxB;

			const AABB	box	  = makeColliderAabb( minB, maxB );
			const uint8 layer = static_cast<uint8>( pData->colliderType );

			if ( pData->physicsBody.isValid() )
			{
				pManager->getPhysicsWorld().setAabb( pData->physicsBody, box );
				return;
			}
			pData->physicsBody = pManager->getPhysicsWorld().addBody( box, layer, pOwner->getEntityId() );
		}
	} // namespace

	void BoxCollider2DComponent::onBeginPlay()
	{
		SceneComponent::onBeginPlay();
		setTickGroup( TickGroup::DuringPhysics );

		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
		{
			pGameObject->addTag( "Collider"_tag );
			if ( pGameObject->getComponent<BoxCollider2DData>() == nullptr )
				pGameObject->addComponent<BoxCollider2DData>();
		}

		syncPhysicsBody( *this, getColliderData() );
	}

	void BoxCollider2DComponent::onEndPlay()
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			unregisterPhysicsBody( getColliderData(), pGameObject->getManager() );
		SceneComponent::onEndPlay();
	}

	void BoxCollider2DComponent::onDestroy()
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			unregisterPhysicsBody( getColliderData(), pGameObject->getManager() );
		SceneComponent::onDestroy();
	}

	void BoxCollider2DComponent::onTick( float32 deltaTime )
	{
		SceneComponent::onTick( deltaTime );
		syncPhysicsBody( *this, getColliderData() );
	}

	int32 BoxCollider2DComponent::getColliderType() const
	{
		const BoxCollider2DData* pData = getColliderData();
		if ( pData != nullptr )
			return pData->colliderType;
		return 0;
	}

	void BoxCollider2DComponent::setColliderType( int32 type )
	{
		BoxCollider2DData* pData = getColliderData();
		if ( pData != nullptr )
			pData->colliderType = type;
	}

	string BoxCollider2DComponent::getOffsetPos() const
	{
		const BoxCollider2DData* pData = getColliderData();
		if ( pData != nullptr )
			return pData->offsetPos;
		return "";
	}

	void BoxCollider2DComponent::setOffsetPos( const string& pos )
	{
		BoxCollider2DData* pData = getColliderData();
		if ( pData != nullptr )
			pData->offsetPos = pos;
	}

	string BoxCollider2DComponent::getOffsetScale() const
	{
		const BoxCollider2DData* pData = getColliderData();
		if ( pData != nullptr )
			return pData->offsetScale;
		return "";
	}

	void BoxCollider2DComponent::setOffsetScale( const string& scale )
	{
		BoxCollider2DData* pData = getColliderData();
		if ( pData != nullptr )
			pData->offsetScale = scale;
	}

	float2 BoxCollider2DComponent::getOffsetPosition() const
	{
		float2		 result{ 0.0f, 0.0f };
		const string pos = getOffsetPos();
		if ( pos.empty() == false )
		{
			float32 x{ 0.0f };
			float32 y{ 0.0f };
			if ( sscanf( pos.c_str(), "%f,%f", &x, &y ) >= 1 )
			{
				result._x = x;
				result._y = y;
			}
		}
		return result;
	}

	float2 BoxCollider2DComponent::getOffsetScaleVec() const
	{
		float2		 result{ 0.0f, 0.0f };
		const string scale = getOffsetScale();
		if ( scale.empty() == false )
		{
			float32 w{ 0.0f };
			float32 h{ 0.0f };
			if ( sscanf( scale.c_str(), "%f,%f", &w, &h ) >= 1 )
			{
				result._x = w;
				result._y = h;
			}
		}
		return result;
	}

	void BoxCollider2DComponent::getBounds( float2& outMin, float2& outMax ) const
	{
		const float3 worldPos = getWorldPosition();
		const float2 offset	  = getOffsetPosition();
		const float2 scale	  = getOffsetScaleVec();
		const float2 center{ worldPos._x + offset._x, worldPos._y + offset._y };
		const float2 halfSize{ scale._x * 0.5f, scale._y * 0.5f };

		outMin = float2{ center._x - halfSize._x, center._y - halfSize._y };
		outMax = float2{ center._x + halfSize._x, center._y + halfSize._y };
	}

	bool BoxCollider2DComponent::intersects( const BoxCollider2DComponent* pOther ) const
	{
		if ( pOther == nullptr )
			return false;
		const BoxCollider2DData* pSelfData	= getColliderData();
		const BoxCollider2DData* pOtherData = pOther->getColliderData();
		GameObject*				 pOwner		= getOwner();
		if ( pSelfData != nullptr && pOtherData != nullptr && pOwner != nullptr && pOwner->getManager() != nullptr &&
			 pSelfData->physicsBody.isValid() && pOtherData->physicsBody.isValid() )
			return pOwner->getManager()->getPhysicsWorld().overlaps( pSelfData->physicsBody, pOtherData->physicsBody );

		float2 aMin{}, aMax{}, bMin{}, bMax{};
		getBounds( aMin, aMax );
		pOther->getBounds( bMin, bMax );
		return ( aMin._x <= bMax._x && aMax._x >= bMin._x &&
				 aMin._y <= bMax._y && aMax._y >= bMin._y );
	}

	bool BoxCollider2DComponent::intersects( const float2& point ) const
	{
		float2 aMin{}, aMax{};
		getBounds( aMin, aMax );
		return ( point._x >= aMin._x && point._x <= aMax._x &&
				 point._y >= aMin._y && point._y <= aMax._y );
	}

	bool BoxCollider2DComponent::intersects( const float2& minB, const float2& maxB ) const
	{
		float2 aMin{}, aMax{};
		getBounds( aMin, aMax );
		return ( aMin._x <= maxB._x && aMax._x >= minB._x &&
				 aMin._y <= maxB._y && aMax._y >= minB._y );
	}

	BoxCollider2DData* BoxCollider2DComponent::getColliderData() const
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			return pGameObject->getComponent<BoxCollider2DData>().get();
		return nullptr;
	}
} // namespace sw
