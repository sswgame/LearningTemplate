#include "pch.h"

#include "Engine/Object/Component/SceneComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
	namespace
	{
		struct SceneComponentInternal
		{
			/**
			 * @brief 로컬 위치, 오일러 회전(Yaw/Pitch/Roll), 스케일 벡터로부터 TRS 로컬 변환 행렬을 생성합니다.
			 */
			static float4x4 makeLocalTRS( const float3& position, const float3& rotation, const float3& scale )
			{
				// DirectX 행-벡터 규격: Scale * Rotation * Translation
				return float4x4::createScale( scale ) *
					   float4x4::createFromYawPitchRoll( rotation._y, rotation._x, rotation._z ) *
					   float4x4::createTranslation( position );
			}

			/**
			 * @brief 부모의 월드 행렬과 결합하여 현재 컴포넌트의 월드 행렬 및 64비트 LWC 월드 좌표를 합성합니다.
			 */
			static void composeWorldFromParent( const float3&	localPosition,
												const float3&	localRotation,
												const float3&	localScale,
												const float4x4* pParentWorldMatrix,
												const double3*	pParentWorldLWC,
												float4x4&		outWorldMatrix,
												double3&		outWorldLWC,
												float3&			outWorldPos )
			{
				const float4x4 localTRS = makeLocalTRS( localPosition, localRotation, localScale );

				if ( pParentWorldMatrix != nullptr && pParentWorldLWC != nullptr )
				{
					outWorldMatrix		= localTRS * ( *pParentWorldMatrix );
					const float3 offset = float3::transformNormal( localPosition, *pParentWorldMatrix );
					outWorldLWC			= *pParentWorldLWC + double3( static_cast<float64>( offset._x ),
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

			static string_view sceneComponentTypeBaseName( const Component* pComp )
			{
				if ( pComp == nullptr )
					return "Component";
				if ( pComp->getComponentName().empty() == false )
					return pComp->getComponentName().c_str();
				const TypeInfo* pTypeInfo = pComp->getTypeInfo();
				if ( pTypeInfo != nullptr )
				{
					if ( pTypeInfo->_name.empty() == false )
						return pTypeInfo->_name.c_str();
					if ( pTypeInfo->_fullyQualifiedName.empty() == false )
						return pTypeInfo->_fullyQualifiedName.c_str();
				}
				return "Component";
			}

			static string makeStableSceneComponentKey( const Component* pComp, int32 occurrenceIndex )
			{
				const string_view base = sceneComponentTypeBaseName( pComp );
				string			  key;
				key.reserve( base.size() + 12 );
				key.append( base.data(), base.size() );
				key += '#';
				key += to_string( occurrenceIndex );
				return key;
			}

			static string findStableSceneComponentKey( const Component* pComp )
			{
				if ( pComp == nullptr || pComp->getOwner() == nullptr )
					return {};

				const string_view targetBase = sceneComponentTypeBaseName( pComp );
				int32			  occ		 = 0;
				string			  resultKey;

				pComp->getOwner()->forEachComponent( [&]( const Component* pOther )
				{
					if ( resultKey.empty() == false || pOther == nullptr )
						return;

					const string_view otherBase = sceneComponentTypeBaseName( pOther );
					if ( otherBase == targetBase )
					{
						if ( pOther == pComp )
							resultKey = makeStableSceneComponentKey( pComp, occ );
						else
							++occ;
					}
				} );

				return resultKey;
			}

			static SceneComponent* findSceneComponentByAttachKey( GameObject* pOwner, string_view attachKey )
			{
				if ( pOwner == nullptr || attachKey.empty() )
					return nullptr;

				const size_t hashPos = attachKey.rfind( '#' );
				if ( hashPos == string_view::npos )
					return nullptr;

				const string_view reqBase = attachKey.substr( 0, hashPos );
				int32			  reqOcc  = 0;
				for ( size_t idx = hashPos + 1; idx < attachKey.size(); ++idx )
				{
					if ( attachKey[idx] < '0' || attachKey[idx] > '9' )
						return nullptr;
					reqOcc = reqOcc * 10 + ( attachKey[idx] - '0' );
				}

				int32			occ	   = 0;
				SceneComponent* pFound = nullptr;

				pOwner->forEachComponent( [&]( Component* pComp )
				{
					if ( pFound != nullptr || pComp == nullptr )
						return;

					const string_view base = sceneComponentTypeBaseName( pComp );
					if ( base == reqBase )
					{
						if ( occ == reqOcc )
							pFound = castTo<SceneComponent>( pComp );
						else
							++occ;
					}
				} );

				return pFound;
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SceneComponent::SceneComponent()
		: _localPosition{ 0.0f, 0.0f, 0.0f }
		, _localRotation{ 0.0f, 0.0f, 0.0f }
		, _localScale{ 1.0f, 1.0f, 1.0f }
		, _attachOwner{}
		, _attachComponent{}
		, _cachedWorldPosition{ 0.0f, 0.0f, 0.0f }
		, _cachedWorldMatrix{ float4x4::Identity }
		, _cachedWorldPositionLWC{ 0.0, 0.0, 0.0 }
		, _pParent{ nullptr }
		, _listChild{}
		, _bIsTransformDirty{ SW_TRUE }
		, _bHasDirtyDescendant{ SW_FALSE }
		, _reservedTransform{ 0 }
	{
		_bCanEverTick = SW_FALSE;
	}

	SceneComponent::SceneComponent( SceneComponent&& other ) noexcept
		: Component{ std::move( other ) }
		, _localPosition{ other._localPosition }
		, _localRotation{ other._localRotation }
		, _localScale{ other._localScale }
		, _attachOwner{ other._attachOwner }
		, _attachComponent{ other._attachComponent }
		, _cachedWorldPosition{ other._cachedWorldPosition }
		, _cachedWorldMatrix{ other._cachedWorldMatrix }
		, _cachedWorldPositionLWC{ other._cachedWorldPositionLWC }
		, _pParent{ other._pParent }
		, _listChild{ std::move( other._listChild ) }
		, _bIsTransformDirty{ other._bIsTransformDirty }
		, _bHasDirtyDescendant{ other._bHasDirtyDescendant }
		, _reservedTransform{ other._reservedTransform }
	{
		other._pParent = nullptr;
		other._listChild.clear();
	}

	SceneComponent& SceneComponent::operator=( SceneComponent&& other ) noexcept
	{
		if ( this != &other )
		{
			Component::operator=( std::move( other ) );
			_localPosition			= other._localPosition;
			_localRotation			= other._localRotation;
			_localScale				= other._localScale;
			_attachOwner			= other._attachOwner;
			_attachComponent		= other._attachComponent;
			_cachedWorldPosition	= other._cachedWorldPosition;
			_cachedWorldMatrix		= other._cachedWorldMatrix;
			_cachedWorldPositionLWC = other._cachedWorldPositionLWC;
			_pParent				= other._pParent;
			_listChild				= std::move( other._listChild );
			_bIsTransformDirty		= other._bIsTransformDirty;
			_bHasDirtyDescendant	= other._bHasDirtyDescendant;
			_reservedTransform		= other._reservedTransform;

			other._pParent = nullptr;
			_listChild.clear();
			other._listChild.clear();
		}
		return *this;
	}

	SceneComponent::~SceneComponent()
	{
		// 소멸 시 자식 컴포넌트들을 부모로부터 분리 (힙 복사 없이 역순 분리)
		while ( _listChild.empty() == false )
		{
			SceneComponent* pChild = _listChild.back();
			if ( pChild != nullptr )
				pChild->detachFromComponent();
			else
				_listChild.pop_back();
		}
		detachFromComponent();
	}

	void SceneComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		markTransformDirty();
	}

	void SceneComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );
	}

	void SceneComponent::onPropertyChanged( hashed_string propertyName )
	{
		Component::onPropertyChanged( propertyName );
		if ( propertyName == hashed_string( "_localPosition" ) ||
			 propertyName == hashed_string( "_localRotation" ) ||
			 propertyName == hashed_string( "_localScale" ) )
			markTransformDirty();
	}

	void SceneComponent::setLocalPosition( const float3& pos )
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getManager()->isParallelTransformReadOnly() )
		{
			GameObjectManager*		  pMgr	 = pOwner->getManager();
			const sw::ComponentHandle handle = getHandle();
			pMgr->deferTransformUpdate( [pMgr, handle, pos]()
			{
				SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
				if ( pSelf != nullptr )
					pSelf->setLocalPosition( pos );
			} );
			return;
		}
		if ( float3::getDistanceSquared( _localPosition, pos ) <= MathUtil::Epsilon )
			return;
		_localPosition = pos;
		markTransformDirty();
	}

	float3 SceneComponent::getLocalPosition() const
	{
		return _localPosition;
	}

	void SceneComponent::setLocalRotation( const float3& rot )
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getManager()->isParallelTransformReadOnly() )
		{
			GameObjectManager*		  pMgr	 = pOwner->getManager();
			const sw::ComponentHandle handle = getHandle();
			pMgr->deferTransformUpdate( [pMgr, handle, rot]()
			{
				SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
				if ( pSelf != nullptr )
					pSelf->setLocalRotation( rot );
			} );
			return;
		}
		if ( float3::getDistanceSquared( _localRotation, rot ) <= MathUtil::Epsilon )
			return;
		_localRotation = rot;
		markTransformDirty();
	}

	float3 SceneComponent::getLocalRotation() const
	{
		return _localRotation;
	}

	void SceneComponent::setLocalScale( const float3& scale )
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getManager()->isParallelTransformReadOnly() )
		{
			GameObjectManager*		  pMgr	 = pOwner->getManager();
			const sw::ComponentHandle handle = getHandle();
			pMgr->deferTransformUpdate( [pMgr, handle, scale]()
			{
				SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
				if ( pSelf != nullptr )
					pSelf->setLocalScale( scale );
			} );
			return;
		}
		if ( float3::getDistanceSquared( _localScale, scale ) <= MathUtil::Epsilon )
			return;
		_localScale = scale;
		markTransformDirty();
	}

	float3 SceneComponent::getLocalScale() const
	{
		return _localScale;
	}

	float3 SceneComponent::getWorldPosition() const
	{
		if ( getOwner() == nullptr || getOwner()->getManager() == nullptr || getOwner()->getManager()->isParallelTransformReadOnly() == false )
			getWorldMatrix();
		return _cachedWorldPosition;
	}

	double3 SceneComponent::getWorldPositionLWC() const
	{
		if ( getOwner() == nullptr || getOwner()->getManager() == nullptr || getOwner()->getManager()->isParallelTransformReadOnly() == false )
			getWorldMatrix();
		return _cachedWorldPositionLWC;
	}

	float4x4 SceneComponent::getWorldMatrix() const
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getManager()->isParallelTransformReadOnly() )
			return _cachedWorldMatrix;

		if ( _bIsTransformDirty == SW_TRUE )
		{
			SceneComponent* pParent = _pParent;
			if ( pParent != nullptr )
				pParent->getWorldMatrix();
			const float4x4* pParentWorld = pParent != nullptr ? &pParent->_cachedWorldMatrix : nullptr;
			const double3*	pParentLwc	 = pParent != nullptr ? &pParent->_cachedWorldPositionLWC : nullptr;
			SceneComponent* pMutable	 = const_cast<SceneComponent*>( this );
			SceneComponentInternal::composeWorldFromParent( _localPosition, _localRotation, _localScale, pParentWorld, pParentLwc,
															pMutable->_cachedWorldMatrix, pMutable->_cachedWorldPositionLWC, pMutable->_cachedWorldPosition );
			pMutable->_bIsTransformDirty = SW_FALSE;
		}
		return _cachedWorldMatrix;
	}

	float4x4 SceneComponent::getCameraRelativeWorldMatrix( const double3& cameraWorldPos ) const
	{
		const float4x4 worldMat		 = getWorldMatrix();
		const double3  relativePos64 = getWorldPositionLWC() - cameraWorldPos;
		const float3   relativePos32( static_cast<float32>( relativePos64._x ),
									  static_cast<float32>( relativePos64._y ),
									  static_cast<float32>( relativePos64._z ) );

		float4x4 cameraRel = worldMat;
		cameraRel.setTranslation( relativePos32 );
		return cameraRel;
	}

	void SceneComponent::updateWorldTransformFromParent()
	{
		const float4x4* pParentWorld = _pParent != nullptr ? &_pParent->_cachedWorldMatrix : nullptr;
		const double3*	pParentLwc	 = _pParent != nullptr ? &_pParent->_cachedWorldPositionLWC : nullptr;
		SceneComponentInternal::composeWorldFromParent( _localPosition, _localRotation, _localScale, pParentWorld, pParentLwc,
														_cachedWorldMatrix, _cachedWorldPositionLWC, _cachedWorldPosition );
		_bIsTransformDirty = SW_FALSE;
	}

	bool SceneComponent::attachToComponent( SceneComponent* pParent )
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getManager()->isParallelTransformReadOnly() )
		{
			GameObjectManager*		  pMgr		   = pOwner->getManager();
			const sw::ComponentHandle selfHandle   = getHandle();
			const sw::ComponentHandle parentHandle = ( pParent != nullptr ) ? pParent->getHandle() : sw::ComponentHandle{};
			pMgr->deferTransformUpdate( [pMgr, selfHandle, parentHandle]()
			{
				SceneComponent* pSelf			= static_cast<SceneComponent*>( pMgr->resolveComponent( selfHandle ) );
				SceneComponent* pResolvedParent = parentHandle.isValid() ? static_cast<SceneComponent*>( pMgr->resolveComponent( parentHandle ) ) : nullptr;
				if ( pSelf != nullptr )
					pSelf->attachToComponent( pResolvedParent );
			} );
			return true;
		}

		if ( pParent == this )
			return false;

		if ( _pParent == pParent )
			return true;

		if ( pParent == nullptr )
			return false;

		SceneComponent* pAncestor = pParent;
		while ( pAncestor != nullptr )
		{
			if ( pAncestor == this )
				return false;
			pAncestor = pAncestor->_pParent;
		}

		detachFromComponent();

		_pParent = pParent;
		pParent->_listChild.push_back( this );

		if ( pOwner != nullptr && pOwner->getManager() != nullptr )
			pOwner->getManager()->unregisterRootSceneComponent( this );

		markTransformDirty();
		return true;
	}

	void SceneComponent::detachFromComponent()
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getManager()->isParallelTransformReadOnly() )
		{
			GameObjectManager*		  pMgr	 = pOwner->getManager();
			const sw::ComponentHandle handle = getHandle();
			pMgr->deferTransformUpdate( [pMgr, handle]()
			{
				SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
				if ( pSelf != nullptr )
					pSelf->detachFromComponent();
			} );
			return;
		}

		if ( _pParent == nullptr )
			return;

		vector<SceneComponent*>& listSibling = _pParent->_listChild;
		for ( size_t childIndex = 0; childIndex < listSibling.size(); ++childIndex )
		{
			if ( listSibling[childIndex] == this )
			{
				listSibling[childIndex] = listSibling.back();
				listSibling.pop_back();
				break;
			}
		}
		_pParent = nullptr;

		if ( pOwner != nullptr && pOwner->getManager() != nullptr )
			pOwner->getManager()->registerRootSceneComponent( this );

		markTransformDirty();
	}

	void SceneComponent::markTransformDirty()
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getManager()->isParallelTransformReadOnly() )
		{
			GameObjectManager*		  pMgr	 = pOwner->getManager();
			const sw::ComponentHandle handle = getHandle();
			pMgr->deferTransformUpdate( [pMgr, handle]()
			{
				SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
				if ( pSelf != nullptr )
					pSelf->markTransformDirty();
			} );
			return;
		}

		_bIsTransformDirty = SW_TRUE;

		if ( pOwner != nullptr && pOwner->getManager() != nullptr )
			pOwner->getManager()->notifyTransformDirtied();

		SceneComponent* pParentComp = _pParent;
		while ( pParentComp != nullptr )
		{
			if ( pParentComp->_bHasDirtyDescendant == SW_TRUE )
				break;
			pParentComp->_bHasDirtyDescendant = SW_TRUE;
			pParentComp						  = pParentComp->_pParent;
		}

		for ( SceneComponent* pChild : _listChild )
		{
			if ( pChild != nullptr && pChild->_bIsTransformDirty == SW_FALSE )
				pChild->markTransformDirty();
		}
	}

	void SceneComponent::syncAttachSerializeFields() const
	{
		_attachOwner	 = {};
		_attachComponent = {};
		if ( _pParent == nullptr )
			return;

		GameObject* pParentOwner = _pParent->getOwner();
		if ( pParentOwner == nullptr )
			return;

		const string parentKey = SceneComponentInternal::findStableSceneComponentKey( _pParent );
		if ( parentKey.empty() )
			return;
		_attachOwner	 = pParentOwner->getName();
		_attachComponent = hashed_string( parentKey.c_str() );
	}

	void SceneComponent::applyAttachSerializeFields()
	{
		if ( _attachComponent.empty() )
			return;

		GameObject* pSelfOwner = getOwner();
		if ( pSelfOwner == nullptr )
			return;

		GameObject* pParentOwner = pSelfOwner;
		if ( _attachOwner.empty() == false && _attachOwner != pSelfOwner->getName() )
		{
			GameObjectManager* pManager = pSelfOwner->getManager();
			if ( pManager == nullptr )
				return;
			pParentOwner = pManager->findGameObjectByName( _attachOwner );
			if ( pParentOwner == nullptr )
				return;
		}

		SceneComponent* pParent = SceneComponentInternal::findSceneComponentByAttachKey( pParentOwner, _attachComponent.c_str() );
		if ( pParent == nullptr || pParent == this )
			return;
		attachToComponent( pParent );
	}

} // namespace sw
