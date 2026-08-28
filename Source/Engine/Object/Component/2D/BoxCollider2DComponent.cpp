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
	} // namespace

	BoxCollider2DComponent::BoxCollider2DComponent()
		: _colliderType{ 0 }
		, _offsetPos{}
		, _offsetScale{}
		, _cachedMin{ 0.0f, 0.0f }
		, _cachedMax{ 0.0f, 0.0f }
		, _physicsBody{}
	{
		setCanEverTick( true );
	}

	void BoxCollider2DComponent::onBeginPlay()
	{
		SceneComponent::onBeginPlay();
		setTickGroup( TickGroup::DuringPhysics );

		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			pGameObject->addTag( "Collider"_tag );

		syncPhysicsBody();
	}

	void BoxCollider2DComponent::onEndPlay()
	{
		unregisterPhysicsBody();
		SceneComponent::onEndPlay();
	}

	void BoxCollider2DComponent::onDestroy()
	{
		unregisterPhysicsBody();
		SceneComponent::onDestroy();
	}

	void BoxCollider2DComponent::onTick( float32 deltaTime )
	{
		SceneComponent::onTick( deltaTime );
		syncPhysicsBody();
	}

	int32 BoxCollider2DComponent::getColliderType() const
	{
		return _colliderType;
	}

	void BoxCollider2DComponent::setColliderType( int32 type )
	{
		_colliderType = type;
	}

	string BoxCollider2DComponent::getOffsetPos() const
	{
		return _offsetPos;
	}

	void BoxCollider2DComponent::setOffsetPos( const string& pos )
	{
		_offsetPos = pos;
	}

	string BoxCollider2DComponent::getOffsetScale() const
	{
		return _offsetScale;
	}

	void BoxCollider2DComponent::setOffsetScale( const string& scale )
	{
		_offsetScale = scale;
	}

	float2 BoxCollider2DComponent::getOffsetPosition() const
	{
		float2 result{ 0.0f, 0.0f };
		if ( _offsetPos.empty() == false )
		{
			float32 x{ 0.0f };
			float32 y{ 0.0f };
			if ( sscanf( _offsetPos.c_str(), "%f,%f", &x, &y ) >= 1 )
			{
				result._x = x;
				result._y = y;
			}
		}
		return result;
	}

	float2 BoxCollider2DComponent::getOffsetScaleVec() const
	{
		float2 result{ 0.0f, 0.0f };
		if ( _offsetScale.empty() == false )
		{
			float32 w{ 0.0f };
			float32 h{ 0.0f };
			if ( sscanf( _offsetScale.c_str(), "%f,%f", &w, &h ) >= 1 )
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
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr && pOwner->getManager() != nullptr && _physicsBody.isValid() && pOther->_physicsBody.isValid() )
			return pOwner->getManager()->getPhysicsWorld().overlaps( _physicsBody, pOther->_physicsBody );

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

	void BoxCollider2DComponent::unregisterPhysicsBody()
	{
		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr || pOwner->getManager() == nullptr || _physicsBody.isValid() == false )
			return;
		pOwner->getManager()->getPhysicsWorld().removeBody( _physicsBody );
		_physicsBody = ObjectHandle{};
	}

	void BoxCollider2DComponent::syncPhysicsBody()
	{
		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr || pOwner->getManager() == nullptr )
			return;

		GameObjectManager* pManager = pOwner->getManager();
		float2			   minB{};
		float2			   maxB{};
		getBounds( minB, maxB );
		_cachedMin = minB;
		_cachedMax = maxB;

		const AABB	box	  = makeColliderAabb( minB, maxB );
		const uint8 layer = static_cast<uint8>( _colliderType );

		if ( _physicsBody.isValid() )
		{
			pManager->getPhysicsWorld().setAabb( _physicsBody, box );
			return;
		}
		_physicsBody = pManager->getPhysicsWorld().addBody( box, layer, pOwner->getObjectId() );
	}

} // namespace sw
