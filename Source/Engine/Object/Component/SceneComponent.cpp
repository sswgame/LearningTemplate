#include "pch.h"

#include "Engine/Object/Component/SceneComponent.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{

	namespace
	{

		/**
		 * @brief 로컬 위치, 오일러 회전(Yaw/Pitch/Roll), 스케일 벡터로부터 TRS 로컬 변환 행렬을 생성합니다.
		 */
		float4x4 makeLocalTRS( const float3& position, const float3& rotation, const float3& scale )
		{
			// DirectX 행-벡터 규격: Scale * Rotation * Translation
			return float4x4::createScale( scale ) *
				   float4x4::createFromYawPitchRoll( rotation._y, rotation._x, rotation._z ) *
				   float4x4::createTranslation( position );
		}

		/**
		 * @brief 부모의 월드 행렬과 결합하여 현재 컴포넌트의 월드 행렬 및 64비트 LWC 월드 좌표를 합성합니다.
		 */
		void composeWorldFromParent( const float3&		  localPosition,
									 const float3&		  localRotation,
									 const float3&		  localScale,
									 const TransformData* parentData,
									 float4x4&			  outWorldMatrix,
									 double3&			  outWorldLWC,
									 float3&			  outWorldPos )
		{
			const float4x4 localTRS = makeLocalTRS( localPosition, localRotation, localScale );

			if ( parentData != nullptr )
			{
				outWorldMatrix = localTRS * parentData->cachedWorldMatrix;
				// LWC: 부모의 64비트 월드 좌표에 부모 회전/스케일이 적용된 로컬 오프셋을 가산
				const float3 offset = float3::transformNormal( localPosition, parentData->cachedWorldMatrix );
				outWorldLWC			= parentData->cachedWorldPositionLWC + double3( static_cast<float64>( offset._x ),
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

	SceneComponent::SceneComponent()
		: Component{ true }
	{
	}

	/**
	 * @brief SceneComponent 소멸자: 부모 및 자식 계층 연결을 정리하고 루트 씬 컴포넌트 등록을 해제합니다.
	 */
	SceneComponent::~SceneComponent()
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			auto&			 reg   = getOwner()->getManager()->getRegistry();
			const sw::Entity myEnt = getOwner()->getEntityId();
			if ( myEnt != sw::kNullEntity )
			{
				auto* hdata = reg.getPtr<HierarchyData>( myEnt );
				if ( hdata != nullptr && hdata->parentEntity != sw::kNullEntity )
				{
					const sw::Entity pEnt = hdata->parentEntity;
					hdata->parentEntity	  = sw::kNullEntity;
					auto* phdata		  = reg.getPtr<HierarchyData>( pEnt );
					if ( phdata != nullptr )
					{
						auto it = std::find( phdata->listChildEntities.begin(), phdata->listChildEntities.end(), myEnt );
						if ( it != phdata->listChildEntities.end() )
						{
							*it = phdata->listChildEntities.back();
							phdata->listChildEntities.pop_back();
						}
					}
				}
				if ( hdata != nullptr )
				{
					for ( sw::Entity childEnt : hdata->listChildEntities )
					{
						if ( childEnt != sw::kNullEntity )
						{
							auto* chdata = reg.getPtr<HierarchyData>( childEnt );
							if ( chdata != nullptr && chdata->parentEntity == myEnt )
								chdata->parentEntity = sw::kNullEntity;
						}
					}
					hdata->listChildEntities.clear();
				}
			}
			getOwner()->getManager()->unregisterRootSceneComponent( this );
		}
	}

	/**
	 * @brief 게임플레이 시작 시 ECS 레지스트리에 TransformData 및 HierarchyData 컴포넌트를 보장합니다.
	 */
	void SceneComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			sw::Registry& reg = getOwner()->getManager()->getRegistry();
			sw::Entity	  ent = getOwner()->getEntityId();
			if ( reg.has<TransformData>( ent ) == false )
				reg.emplace<TransformData>( ent );
			if ( reg.has<HierarchyData>( ent ) == false )
				reg.emplace<HierarchyData>( ent );
		}
	}

	void SceneComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );
	}

	void SceneComponent::setLocalPosition( const float3& pos )
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			if ( getOwner()->getManager()->isParallelTransformReadOnly() )
			{
				GameObjectManager*		  pMgr	 = getOwner()->getManager();
				const sw::ComponentHandle handle = getHandle();
				pMgr->deferTransformUpdate( [pMgr, handle, pos]()
				{
					SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
					if ( pSelf != nullptr )
						pSelf->setLocalPosition( pos );
				} );
				return;
			}
			sw::Registry& reg = getOwner()->getManager()->getRegistry();
			sw::Entity	  ent = getOwner()->getEntityId();
			if ( reg.has<TransformData>( ent ) == false )
				reg.emplace<TransformData>( ent );
			auto& tdata = reg.get<TransformData>( ent );
			if ( MathUtil::abs( tdata.localPosition._x - pos._x ) <= 1e-6f &&
				 MathUtil::abs( tdata.localPosition._y - pos._y ) <= 1e-6f &&
				 MathUtil::abs( tdata.localPosition._z - pos._z ) <= 1e-6f )
				return;
			tdata.localPosition = pos;
			markTransformDirty();
		}
	}

	float3 SceneComponent::getLocalPosition() const
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			float3 result{ 0.0f, 0.0f, 0.0f };
			if ( getOwner()->getManager()->getRegistry().withComponentConst<TransformData>( getOwner()->getEntityId(), [&]( const TransformData& tdata )
			{
				result = tdata.localPosition;
			} ) )
			{
				return result;
			}
		}
		return float3( 0.0f, 0.0f, 0.0f );
	}

	void SceneComponent::setLocalRotation( const float3& rot )
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			if ( getOwner()->getManager()->isParallelTransformReadOnly() )
			{
				GameObjectManager*		  pMgr	 = getOwner()->getManager();
				const sw::ComponentHandle handle = getHandle();
				pMgr->deferTransformUpdate( [pMgr, handle, rot]()
				{
					SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
					if ( pSelf != nullptr )
						pSelf->setLocalRotation( rot );
				} );
				return;
			}
			sw::Registry& reg = getOwner()->getManager()->getRegistry();
			sw::Entity	  ent = getOwner()->getEntityId();
			if ( reg.has<TransformData>( ent ) == false )
				reg.emplace<TransformData>( ent );
			auto& tdata = reg.get<TransformData>( ent );
			if ( MathUtil::abs( tdata.localRotation._x - rot._x ) <= 1e-6f &&
				 MathUtil::abs( tdata.localRotation._y - rot._y ) <= 1e-6f &&
				 MathUtil::abs( tdata.localRotation._z - rot._z ) <= 1e-6f )
				return;
			tdata.localRotation = rot;
			markTransformDirty();
		}
	}

	float3 SceneComponent::getLocalRotation() const
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			float3 result{ 0.0f, 0.0f, 0.0f };
			if ( getOwner()->getManager()->getRegistry().withComponentConst<TransformData>( getOwner()->getEntityId(), [&]( const TransformData& tdata )
			{
				result = tdata.localRotation;
			} ) )
			{
				return result;
			}
		}
		return float3( 0.0f, 0.0f, 0.0f );
	}

	void SceneComponent::setLocalScale( const float3& scale )
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			if ( getOwner()->getManager()->isParallelTransformReadOnly() )
			{
				GameObjectManager*		  pMgr	 = getOwner()->getManager();
				const sw::ComponentHandle handle = getHandle();
				pMgr->deferTransformUpdate( [pMgr, handle, scale]()
				{
					SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
					if ( pSelf != nullptr )
						pSelf->setLocalScale( scale );
				} );
				return;
			}
			sw::Registry& reg = getOwner()->getManager()->getRegistry();
			sw::Entity	  ent = getOwner()->getEntityId();
			if ( reg.has<TransformData>( ent ) == false )
				reg.emplace<TransformData>( ent );
			auto& tdata = reg.get<TransformData>( ent );
			if ( MathUtil::abs( tdata.localScale._x - scale._x ) <= 1e-6f &&
				 MathUtil::abs( tdata.localScale._y - scale._y ) <= 1e-6f &&
				 MathUtil::abs( tdata.localScale._z - scale._z ) <= 1e-6f )
				return;
			tdata.localScale = scale;
			markTransformDirty();
		}
	}

	float3 SceneComponent::getLocalScale() const
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			float3 result{ 1.0f, 1.0f, 1.0f };
			if ( getOwner()->getManager()->getRegistry().withComponentConst<TransformData>( getOwner()->getEntityId(), [&]( const TransformData& tdata )
			{
				result = tdata.localScale;
			} ) )
			{
				return result;
			}
		}
		return float3( 1.0f, 1.0f, 1.0f );
	}

	float3 SceneComponent::getWorldPosition() const
	{
		if ( getOwner() == nullptr || getOwner()->getManager() == nullptr || getOwner()->getManager()->isParallelTransformReadOnly() == false )
			getWorldMatrix();
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			float3 result{ 0.0f, 0.0f, 0.0f };
			if ( getOwner()->getManager()->getRegistry().withComponentConst<TransformData>( getOwner()->getEntityId(), [&]( const TransformData& tdata )
			{
				result = tdata.cachedWorldPosition;
			} ) )
			{
				return result;
			}
		}
		return float3( 0.0f, 0.0f, 0.0f );
	}

	double3 SceneComponent::getWorldPositionLWC() const
	{
		if ( getOwner() == nullptr || getOwner()->getManager() == nullptr || getOwner()->getManager()->isParallelTransformReadOnly() == false )
			getWorldMatrix();
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			double3 result{ 0.0, 0.0, 0.0 };
			if ( getOwner()->getManager()->getRegistry().withComponentConst<TransformData>( getOwner()->getEntityId(), [&]( const TransformData& tdata )
			{
				result = tdata.cachedWorldPositionLWC;
			} ) )
			{
				return result;
			}
		}
		return double3( 0.0, 0.0, 0.0 );
	}

	float4x4 SceneComponent::getWorldMatrix() const
	{
		if ( getOwner() == nullptr || getOwner()->getManager() == nullptr )
			return float4x4::Identity;

		auto& reg	= getOwner()->getManager()->getRegistry();
		auto* tdata = reg.getPtr<TransformData>( getOwner()->getEntityId() );
		if ( tdata == nullptr )
			return float4x4::Identity;

		if ( getOwner()->getManager()->isParallelTransformReadOnly() )
			return tdata->cachedWorldMatrix;

		if ( tdata->bIsTransformDirty != 0 )
		{
			SceneComponent* parentObj = getParent();
			if ( parentObj != nullptr )
			{
				// Ensure parent is updated
				parentObj->getWorldMatrix();
				auto* pdata = reg.getPtr<TransformData>( parentObj->getOwner()->getEntityId() );
				tdata		= reg.getPtr<TransformData>( getOwner()->getEntityId() );
				if ( tdata == nullptr )
					return float4x4::Identity;
				composeWorldFromParent( tdata->localPosition, tdata->localRotation, tdata->localScale, pdata,
										tdata->cachedWorldMatrix, tdata->cachedWorldPositionLWC, tdata->cachedWorldPosition );
			}
			else
			{
				composeWorldFromParent( tdata->localPosition, tdata->localRotation, tdata->localScale, nullptr,
										tdata->cachedWorldMatrix, tdata->cachedWorldPositionLWC, tdata->cachedWorldPosition );
			}
			tdata->bIsTransformDirty = 0;
		}
		return tdata->cachedWorldMatrix;
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
		if ( getOwner() == nullptr || getOwner()->getManager() == nullptr )
			return;
		auto& reg	= getOwner()->getManager()->getRegistry();
		auto* tdata = reg.getPtr<TransformData>( getOwner()->getEntityId() );
		if ( tdata == nullptr )
			return;

		SceneComponent* parentObj = getParent();
		if ( parentObj != nullptr )
		{
			auto* pdata = reg.getPtr<TransformData>( parentObj->getOwner()->getEntityId() );
			composeWorldFromParent( tdata->localPosition, tdata->localRotation, tdata->localScale, pdata,
									tdata->cachedWorldMatrix, tdata->cachedWorldPositionLWC, tdata->cachedWorldPosition );
		}
		else
		{
			composeWorldFromParent( tdata->localPosition, tdata->localRotation, tdata->localScale, nullptr,
									tdata->cachedWorldMatrix, tdata->cachedWorldPositionLWC, tdata->cachedWorldPosition );
		}
		tdata->bIsTransformDirty = 0;
	}

	bool SceneComponent::attachToComponent( SceneComponent* pParent )
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr && getOwner()->getManager()->isParallelTransformReadOnly() )
		{
			GameObjectManager*		  pMgr		   = getOwner()->getManager();
			const sw::ComponentHandle selfHandle   = getHandle();
			const sw::ComponentHandle parentHandle = pParent != nullptr ? pParent->getHandle() : sw::ComponentHandle{};
			pMgr->deferTransformUpdate( [pMgr, selfHandle, parentHandle]()
			{
				SceneComponent* pSelfComp = static_cast<SceneComponent*>( pMgr->resolveComponent( selfHandle ) );
				if ( pSelfComp == nullptr )
					return;
				SceneComponent* pParentComp{ nullptr };
				if ( parentHandle.isValid() )
					pParentComp = static_cast<SceneComponent*>( pMgr->resolveComponent( parentHandle ) );
				pSelfComp->attachToComponent( pParentComp );
			} );
			return true;
		}

		if ( pParent == nullptr || pParent == this || pParent == getParent() )
			return false;

		SceneComponent* pAncestor = pParent;
		while ( pAncestor != nullptr )
		{
			if ( pAncestor == this )
				return false;
			pAncestor = pAncestor->getParent();
		}

		detachFromComponent();

		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			auto&	   reg	 = getOwner()->getManager()->getRegistry();
			sw::Entity myEnt = getOwner()->getEntityId();
			sw::Entity pEnt	 = pParent->getOwner()->getEntityId();

			if ( reg.has<HierarchyData>( myEnt ) == false )
				reg.emplace<HierarchyData>( myEnt );
			if ( reg.has<HierarchyData>( pEnt ) == false )
				reg.emplace<HierarchyData>( pEnt );

			reg.get<HierarchyData>( myEnt ).parentEntity = pEnt;
			reg.get<HierarchyData>( pEnt ).listChildEntities.push_back( myEnt );

			getOwner()->getManager()->unregisterRootSceneComponent( this );
			markTransformDirty();
			return true;
		}
		return false;
	}

	void SceneComponent::detachFromComponent()
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr && getOwner()->getManager()->isParallelTransformReadOnly() )
		{
			GameObjectManager*		  pMgr	 = getOwner()->getManager();
			const sw::ComponentHandle handle = getHandle();
			pMgr->deferTransformUpdate( [pMgr, handle]()
			{
				SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
				if ( pSelf != nullptr )
					pSelf->detachFromComponent();
			} );
			return;
		}

		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			auto&	   reg	 = getOwner()->getManager()->getRegistry();
			sw::Entity myEnt = getOwner()->getEntityId();
			if ( myEnt != sw::kNullEntity )
			{
				auto* hdata = reg.getPtr<HierarchyData>( myEnt );
				if ( hdata != nullptr && hdata->parentEntity != sw::kNullEntity )
				{
					const sw::Entity pEnt = hdata->parentEntity;
					hdata->parentEntity	  = sw::kNullEntity;

					HierarchyData* pPhdata = reg.getPtr<HierarchyData>( pEnt );
					if ( pPhdata != nullptr )
					{
						vector<sw::Entity>& children = pPhdata->listChildEntities;
						for ( size_t childIndex = 0; childIndex < children.size(); ++childIndex )
						{
							if ( children[childIndex] == myEnt )
							{
								children[childIndex] = children.back();
								children.pop_back();
								break;
							}
						}
					}

					getOwner()->getManager()->registerRootSceneComponent( this );
					markTransformDirty();
				}
			}
		}
	}

	SceneComponent* SceneComponent::getParent() const
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			auto* hdata = getOwner()->getManager()->getRegistry().getPtr<HierarchyData>( getOwner()->getEntityId() );
			if ( hdata != nullptr && hdata->parentEntity != sw::kNullEntity )
				return getOwner()->getManager()->getRegistry().getPtr<SceneComponent>( hdata->parentEntity );
		}
		return nullptr;
	}

	vector<SceneComponent*> SceneComponent::getChildren() const
	{
		vector<SceneComponent*> listResult;
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			sw::Registry& reg	= getOwner()->getManager()->getRegistry();
			auto*		  hdata = reg.getPtr<HierarchyData>( getOwner()->getEntityId() );
			if ( hdata != nullptr )
			{
				const auto& childList = hdata->listChildEntities;
				listResult.reserve( childList.size() );
				for ( sw::Entity childEnt : childList )
				{
					SceneComponent* pComp = reg.getPtr<SceneComponent>( childEnt );
					if ( pComp != nullptr )
						listResult.push_back( pComp );
				}
			}
		}
		return listResult;
	}

	void SceneComponent::markTransformDirty()
	{
		if ( getOwner() == nullptr || getOwner()->getManager() == nullptr )
			return;

		if ( getOwner()->getManager()->isParallelTransformReadOnly() )
		{
			GameObjectManager*		  pMgr	 = getOwner()->getManager();
			const sw::ComponentHandle handle = getHandle();
			pMgr->deferTransformUpdate( [pMgr, handle]()
			{
				SceneComponent* pSelf = static_cast<SceneComponent*>( pMgr->resolveComponent( handle ) );
				if ( pSelf != nullptr )
					pSelf->markTransformDirty();
			} );
			return;
		}

		auto& reg	 = getOwner()->getManager()->getRegistry();
		auto* pTdata = reg.getPtr<TransformData>( getOwner()->getEntityId() );
		if ( pTdata != nullptr )
			pTdata->bIsTransformDirty = 1;

		getOwner()->getManager()->notifyTransformDirtied();

		SceneComponent* pParentComp = getParent();
		while ( pParentComp != nullptr )
		{
			auto* pParentTdata = reg.getPtr<TransformData>( pParentComp->getOwner()->getEntityId() );
			if ( pParentTdata != nullptr )
			{
				if ( pParentTdata->bHasDirtyDescendant == 1 )
					break;
				pParentTdata->bHasDirtyDescendant = 1;
			}
			pParentComp = pParentComp->getParent();
		}

		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr && getOwner()->getManager()->isParallelTransformReadOnly() )
			return;

		auto* pHdata = reg.getPtr<HierarchyData>( getOwner()->getEntityId() );
		if ( pHdata != nullptr )
		{
			for ( sw::Entity childEnt : pHdata->listChildEntities )
			{
				TransformData* pCtdata = reg.getPtr<TransformData>( childEnt );
				if ( pCtdata != nullptr && pCtdata->bIsTransformDirty == 0 )
				{
					SceneComponent* pChild = reg.getPtr<SceneComponent>( childEnt );
					if ( pChild != nullptr )
						pChild->markTransformDirty();
				}
			}
		}
	}

	bool SceneComponent::isTransformDirty() const
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			auto* tdata = getOwner()->getManager()->getRegistry().getPtr<TransformData>( getOwner()->getEntityId() );
			if ( tdata != nullptr )
				return tdata->bIsTransformDirty != 0;
		}
		return false;
	}

	bool SceneComponent::hasDirtyDescendant() const
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			auto* tdata = getOwner()->getManager()->getRegistry().getPtr<TransformData>( getOwner()->getEntityId() );
			if ( tdata != nullptr )
				return tdata->bHasDirtyDescendant != 0;
		}
		return false;
	}

	void SceneComponent::clearDirtyDescendant()
	{
		if ( getOwner() != nullptr && getOwner()->getManager() != nullptr )
		{
			auto* tdata = getOwner()->getManager()->getRegistry().getPtr<TransformData>( getOwner()->getEntityId() );
			if ( tdata != nullptr )
				tdata->bHasDirtyDescendant = 0;
		}
	}

} // namespace sw
