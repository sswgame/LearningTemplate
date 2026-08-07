/**
 * @file SceneComponent.cpp
 * @brief SceneComponent 트랜스폼·계층 구현
 */
#include "pch.h"
#include "SceneComponent.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	namespace
	{
		// Mutated only on the main thread around parallel tick barriers.
		bool s_bParallelTransformReadOnly = false;

		float4x4 makeLocalTRS( const float3& position, const float3& rotation, const float3& scale )
		{
			// Row-vector DX style: Scale * Rotation * Translation
			return float4x4::createScale( scale ) *
				   float4x4::createFromYawPitchRoll( rotation._y, rotation._x, rotation._z ) *
				   float4x4::createTranslation( position );
		}

		void composeWorldFromParent( const float3& localPosition,
									 const float3& localRotation,
									 const float3& localScale,
									 const SceneComponent* parent,
									 const float4x4&	   parentWorldMatrix,
									 const double3&		   parentWorldLWC,
									 float4x4&			   outWorldMatrix,
									 double3&			   outWorldLWC,
									 float3&			   outWorldPos )
		{
			const float4x4 localTRS = makeLocalTRS( localPosition, localRotation, localScale );

			if ( parent != nullptr )
			{
				outWorldMatrix = localTRS * parentWorldMatrix;
				// LWC: parent double position + local offset transformed by parent rot/scale (no parent translation).
				const float3 offset = float3::transformNormal( localPosition, parentWorldMatrix );
				outWorldLWC			= parentWorldLWC + double3( static_cast<float64>( offset._x ),
																static_cast<float64>( offset._y ),
																static_cast<float64>( offset._z ) );
			}
			else
			{
				outWorldMatrix = localTRS;
				outWorldLWC	   = double3( static_cast<float64>( localPosition._x ),
										  static_cast<float64>( localPosition._y ),
										  static_cast<float64>( localPosition._z ) );
			}

			outWorldPos = float3( static_cast<float32>( outWorldLWC._x ),
								  static_cast<float32>( outWorldLWC._y ),
								  static_cast<float32>( outWorldLWC._z ) );
		}
	} // namespace

	void SceneComponent::beginParallelTransformReadOnly()
	{
		s_bParallelTransformReadOnly = true;
	}

	void SceneComponent::endParallelTransformReadOnly()
	{
		s_bParallelTransformReadOnly = false;
	}

	bool SceneComponent::isParallelTransformReadOnly()
	{
		return s_bParallelTransformReadOnly;
	}

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

	const TypeInfo* SceneComponent::getTypeInfo() const
	{
		if ( _cachedTypeInfo != nullptr )
			return _cachedTypeInfo;
		return sw::getTypeRegistry().findType( hashed_string( "sw::SceneComponent" ) );
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
		// During parallel tick, skip child walks (shared hierarchy mutation / races).
		// Next flushSceneTransforms recomputes the whole tree from roots.
		if ( s_bParallelTransformReadOnly )
			return;

		for ( SceneComponent* child : _children )
		{
			if ( child != nullptr && child->_bIsTransformDirty == 0 )
			{
				child->markTransformDirty();
			}
		}
	}

	void SceneComponent::updateWorldTransformFromParent()
	{
		if ( _parent != nullptr )
		{
			composeWorldFromParent( _localPosition, _localRotation, _localScale, _parent,
									_parent->_cachedWorldMatrix, _parent->_cachedWorldPositionLWC,
									_cachedWorldMatrix, _cachedWorldPositionLWC, _cachedWorldPosition );
		}
		else
		{
			composeWorldFromParent( _localPosition, _localRotation, _localScale, nullptr,
									float4x4::Identity, double3( 0.0, 0.0, 0.0 ),
									_cachedWorldMatrix, _cachedWorldPositionLWC, _cachedWorldPosition );
		}
		_bIsTransformDirty = 0;
	}

	float3 SceneComponent::getWorldPosition() const
	{
		if ( s_bParallelTransformReadOnly == false && _bIsTransformDirty != 0 )
		{
			getWorldMatrix();
		}
		return _cachedWorldPosition;
	}

	double3 SceneComponent::getWorldPositionLWC() const
	{
		if ( s_bParallelTransformReadOnly == false && _bIsTransformDirty != 0 )
		{
			getWorldMatrix();
		}
		return _cachedWorldPositionLWC;
	}

	float4x4 SceneComponent::getWorldMatrix() const
	{
		if ( s_bParallelTransformReadOnly )
			return _cachedWorldMatrix;

		if ( _bIsTransformDirty != 0 )
		{
			if ( _parent != nullptr )
			{
				const float4x4 parentWorld = _parent->getWorldMatrix();
				const double3  parentLWC   = _parent->getWorldPositionLWC();
				composeWorldFromParent( _localPosition, _localRotation, _localScale, _parent,
										parentWorld, parentLWC,
										_cachedWorldMatrix, _cachedWorldPositionLWC, _cachedWorldPosition );
			}
			else
			{
				composeWorldFromParent( _localPosition, _localRotation, _localScale, nullptr,
										float4x4::Identity, double3( 0.0, 0.0, 0.0 ),
										_cachedWorldMatrix, _cachedWorldPositionLWC, _cachedWorldPosition );
			}
			_bIsTransformDirty = 0;
		}
		return _cachedWorldMatrix;
	}

	float4x4 SceneComponent::getCameraRelativeWorldMatrix( const double3& cameraWorldPos ) const
	{
		const float4x4 worldMat		  = getWorldMatrix();
		const double3  relativePos64  = getWorldPositionLWC() - cameraWorldPos;
		const float3   relativePos32( static_cast<float32>( relativePos64._x ),
									  static_cast<float32>( relativePos64._y ),
									  static_cast<float32>( relativePos64._z ) );

		float4x4 cameraRel = worldMat;
		cameraRel.setTranslation( relativePos32 );
		return cameraRel;
	}

	bool SceneComponent::attachToComponent( SceneComponent* parent )
	{
		if ( s_bParallelTransformReadOnly )
		{
			SW_LOG_ERROR( "[SceneComponent] attachToComponent is not allowed during parallel transform read-only." );
			return false;
		}

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
		if ( s_bParallelTransformReadOnly )
		{
			SW_LOG_ERROR( "[SceneComponent] detachFromComponent is not allowed during parallel transform read-only." );
			return;
		}

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

	namespace
	{
		// Built-in factory so ObjectStateSerializer / Add Component can recreate SceneComponent by name.
		void registerSceneComponentFactory( ComponentManager& manager )
		{
			manager.registerComponentType<SceneComponent>( hashed_string( "SceneComponent" ) );
		}

		static ComponentFactoryRegistrar s_sceneComponentFactoryRegistrar( &registerSceneComponentFactory );
	} // namespace
} // namespace sw
