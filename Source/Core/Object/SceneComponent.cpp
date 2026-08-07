/**
 * @file SceneComponent.cpp
 * @brief SceneComponent 트랜스폼·계층 구현
 */
#include "pch.h"
#include "SceneComponent.h"

namespace sw
{

	SceneComponent::SceneComponent()
		: _bIsTransformDirty{ 1 }
		, _reservedFlags{ 0 }
	{
	}

	SceneComponent::~SceneComponent()
	{
		detachFromComponent();

		std::vector<SceneComponent*> childrenCopy = _children;
		for ( SceneComponent* child : childrenCopy )
		{
			if ( child != nullptr )
			{
				child->detachFromComponent();
			}
		}
		_children.clear();
	}

	void SceneComponent::setLocalPosition( const float3& pos )
	{
		_localPosition = pos;
		markTransformDirty();
	}

	void SceneComponent::setLocalRotation( const float3& rot )
	{
		_localRotation = rot;
		markTransformDirty();
	}

	void SceneComponent::setLocalScale( const float3& scale )
	{
		_localScale = scale;
		markTransformDirty();
	}

	void SceneComponent::markTransformDirty()
	{
		_bIsTransformDirty = 1;
		for ( SceneComponent* child : _children )
		{
			if ( child != nullptr && child->_bIsTransformDirty == 0 )
			{
				child->markTransformDirty();
			}
		}
	}

	float3 SceneComponent::getWorldPosition() const
	{
		if ( _bIsTransformDirty != 0 )
		{
			getWorldMatrix();
		}
		return _cachedWorldPosition;
	}

	double3 SceneComponent::getWorldPositionLWC() const
	{
		if ( _bIsTransformDirty != 0 )
		{
			getWorldMatrix();
		}
		return _cachedWorldPositionLWC;
	}

	float4x4 SceneComponent::getWorldMatrix() const
	{
		if ( _bIsTransformDirty != 0 )
		{
			float4x4 localTrans = float4x4::createTranslation( _localPosition );
			double3	 localPos64( static_cast<float64>( _localPosition._x ),
								 static_cast<float64>( _localPosition._y ),
								 static_cast<float64>( _localPosition._z ) );

			if ( _parent != nullptr )
			{
				_cachedWorldMatrix		= localTrans * _parent->getWorldMatrix();
				_cachedWorldPositionLWC = _parent->getWorldPositionLWC() + localPos64;
				_cachedWorldPosition	= float3( static_cast<float32>( _cachedWorldPositionLWC._x ),
												  static_cast<float32>( _cachedWorldPositionLWC._y ),
												  static_cast<float32>( _cachedWorldPositionLWC._z ) );
			}
			else
			{
				_cachedWorldMatrix		= localTrans;
				_cachedWorldPositionLWC = localPos64;
				_cachedWorldPosition	= _localPosition;
			}
			_bIsTransformDirty = 0;
		}
		return _cachedWorldMatrix;
	}

	float4x4 SceneComponent::getCameraRelativeWorldMatrix( const double3& cameraWorldPos ) const
	{
		double3 relativePos64 = getWorldPositionLWC() - cameraWorldPos;
		float3	relativePos32( static_cast<float32>( relativePos64._x ),
							   static_cast<float32>( relativePos64._y ),
							   static_cast<float32>( relativePos64._z ) );

		float4x4 rotScaleMat = float4x4::createScale( _localScale ) *
							   float4x4::createFromYawPitchRoll( _localRotation._y, _localRotation._x, _localRotation._z );

		return rotScaleMat * float4x4::createTranslation( relativePos32 );
	}

	bool SceneComponent::attachToComponent( SceneComponent* parent )
	{
		if ( parent == nullptr || parent == this || parent == _parent )
			return false;

		SceneComponent* ancestor = parent;
		while ( ancestor != nullptr )
		{
			if ( ancestor == this )
				return false;
			ancestor = ancestor->getParent();
		}

		detachFromComponent();

		_parent = parent;
		if ( _parent->_children.capacity() <= _parent->_children.size() )
		{
			_parent->_children.reserve( _parent->_children.size() + 4 );
		}
		_parent->_children.push_back( this );
		markTransformDirty();
		return true;
	}

	void SceneComponent::detachFromComponent()
	{
		if ( _parent != nullptr )
		{
			std::vector<SceneComponent*>& children = _parent->_children;
			const size_t				  count	   = children.size();
			for ( size_t idx = 0; idx < count; ++idx )
			{
				if ( children[idx] == this )
				{
					children[idx] = children.back();
					children.pop_back();
					break;
				}
			}
			_parent = nullptr;
			markTransformDirty();
		}
	}
}
