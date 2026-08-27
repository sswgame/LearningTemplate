#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
	GameObject::GameObject()
		: _objectId{ _s_nextObjectId.fetch_add( 1, std::memory_order_relaxed ) }
		, _name{ "GameObject" }
		, _pOwnerManager{ nullptr }
		, _entityId{ sw::kNullEntity }
		, _managerIndex{ static_cast<uint32>( -1 ) }
		, _bActive{ true }
		, _bIsActiveInHierarchy{ true }
		, _bIsPendingKill{ false }
	{
	}

	GameObject::GameObject( hashed_string name )
		: _objectId{ _s_nextObjectId.fetch_add( 1, std::memory_order_relaxed ) }
		, _name{ name }
		, _pOwnerManager{ nullptr }
		, _entityId{ sw::kNullEntity }
		, _managerIndex{ static_cast<uint32>( -1 ) }
		, _bActive{ true }
		, _bIsActiveInHierarchy{ true }
		, _bIsPendingKill{ false }
	{
	}

	/**
	 * @brief 게임 오브젝트 소멸자: 부모 계층 연결을 해제하고 모든 소유 컴포넌트의 onDestroy 호출 및 ECS 엔티티를 파괴합니다.
	 */
	GameObject::~GameObject()
	{

		// 컴포넌트 파괴 전 부모-자식 계층 링크 분리 (자식 오브젝트들은 루트로 승격되어 생존)
		detachFromParent();

		vector<GameObject*> listChildrenObjs = getChildren();
		for ( GameObject* pChildPtr : listChildrenObjs )
		{
			if ( pChildPtr != nullptr )
				pChildPtr->detachFromParent();
		}

		if ( _pOwnerManager == nullptr || _entityId == sw::kNullEntity )
			return;

		_pOwnerManager->getRegistry().forEachComponent( _entityId, [&]( Component* pComp )
		{
			if ( pComp == nullptr )
				return;

			unregisterComponentIfSceneRoot( pComp );
			pComp->onDestroy();
		} );

		_pOwnerManager->getRegistry().destroy( _entityId );
		_entityId = sw::kNullEntity;
	}

	const TypeInfo* GameObject::getTypeInfo() const
	{
		return engine::getTypeRegistry().findType( hashed_string( "sw::GameObject" ) );
	}

	/**
	 * @brief 게임플레이 시작(Play Mode) 시 소유한 모든 활성 컴포넌트의 onBeginPlay를 호출합니다.
	 */
	void GameObject::beginPlay()
	{
		forEachComponent( [&]( Component* pComp )
		{
			if ( pComp == nullptr || pComp->isActive() == false )
				return;
			pComp->onBeginPlay();
		} );
	}

	/**
	 * @brief 게임플레이 종료 시 소유한 모든 컴포넌트의 onEndPlay를 호출합니다.
	 */
	void GameObject::endPlay()
	{
		forEachComponent( [&]( Component* pComp )
		{
			if ( pComp == nullptr || pComp->isActive() == false )
				return;
			pComp->onEndPlay();
		} );
	}

	void GameObject::onPropertyChanged( hashed_string propertyName )
	{
		(void)propertyName;
	}

	/**
	 * @brief 프레임 종료 시점에 게임 오브젝트를 안전하게 파괴하도록 지연 등록합니다.
	 */
	void GameObject::destroy()
	{
		if ( _pOwnerManager == nullptr )
			return;
		_pOwnerManager->destroyObject( this );
	}

	void GameObject::markPendingKill()
	{
		_bIsPendingKill.store( true, std::memory_order_release );
		if ( _pOwnerManager != nullptr && _entityId != sw::kNullEntity )
		{
			EntityStateData* pState = _pOwnerManager->getRegistry().getPtr<EntityStateData>( _entityId );
			if ( pState != nullptr )
				pState->bIsPendingKill.store( true, std::memory_order_relaxed );
		}
	}

	/**
	 * @brief 게임 오브젝트 이름을 변경하고 GameObjectManager의 이름 검색 인덱스를 갱신합니다.
	 */
	void GameObject::setName( hashed_string name )
	{
		if ( name == _name )
			return;

		const hashed_string oldName = _name;
		_name						= name;
		if ( _pOwnerManager != nullptr )
			_pOwnerManager->notifyNameChanged( this, oldName, name );
	}

	/**
	 * @brief 로컬 활성 상태를 변경하고 부모 계층 상태를 반영하여 자식들에게 활성화 이벤트를 전파합니다.
	 */
	void GameObject::setActive( bool bActive )
	{
		_bActive.store( bActive, std::memory_order_relaxed );
		refreshActiveInHierarchy();

		forEachComponent( [&]( Component* pComp )
		{
			if ( pComp == nullptr )
				return;
			pComp->setActive( bActive );
		} );
		onPropertyChanged( hashed_string( "_bActive" ) );
	}

	bool GameObject::attachToParent( GameObject* pParent )
	{
		if ( getManager() != nullptr && getManager()->isParallelTransformReadOnly() )
		{
			SW_LOG_ERROR( "[GameObject] attachToParent is not allowed during parallel transform read-only." );
			return false;
		}

		SceneComponent* pChildSc = getPrimarySceneComponent();
		if ( pChildSc == nullptr )
			return false;

		SceneComponent* pParentSc = pParent != nullptr ? pParent->getPrimarySceneComponent() : nullptr;
		if ( pParentSc == nullptr )
			return false;

		bool bAttached = pChildSc->attachToComponent( pParentSc );
		if ( bAttached )
			refreshActiveInHierarchy();

		return bAttached;
	}

	void GameObject::detachFromParent()
	{
		if ( getManager() != nullptr && getManager()->isParallelTransformReadOnly() )
		{
			SW_LOG_ERROR( "[GameObject] detachFromParent is not allowed during parallel transform read-only." );
			return;
		}

		SceneComponent* pChildSc = getPrimarySceneComponent();
		if ( pChildSc != nullptr )
		{
			pChildSc->detachFromComponent();
			refreshActiveInHierarchy();
		}
	}

	GameObject* GameObject::getParent() const
	{
		if ( _pOwnerManager != nullptr && _entityId != sw::kNullEntity )
		{
			HierarchyData* pData = _pOwnerManager->getRegistry().getPtr<HierarchyData>( _entityId );
			if ( pData != nullptr && pData->parentEntity != sw::kNullEntity )
			{
				return _pOwnerManager->findGameObjectByEntity( pData->parentEntity );
			}
		}
		return nullptr;
	}

	void GameObject::refreshActiveInHierarchy()
	{
		bool		bParentActive = true;
		GameObject* pParent		  = getParent();
		if ( pParent != nullptr )
			bParentActive = pParent->isActiveInHierarchy();

		_bIsActiveInHierarchy.store( bParentActive, std::memory_order_relaxed );

		if ( _pOwnerManager != nullptr && _entityId != sw::kNullEntity )
		{
			EntityStateData* pState = _pOwnerManager->getRegistry().getPtr<EntityStateData>( _entityId );
			if ( pState != nullptr )
				pState->bIsActiveInHierarchy.store( bParentActive, std::memory_order_relaxed );
		}

		for ( GameObject* pChild : getChildren() )
		{
			if ( pChild != nullptr )
				pChild->refreshActiveInHierarchy();
		}
	}

	vector<GameObject*> GameObject::getChildren() const
	{
		vector<GameObject*> listResult;
		if ( _pOwnerManager == nullptr || _entityId == sw::kNullEntity )
			return listResult;

		HierarchyData* pData = _pOwnerManager->getRegistry().getPtr<HierarchyData>( _entityId );
		if ( pData == nullptr )
			return listResult;

		const auto& children = pData->listChildEntities;
		listResult.reserve( children.size() );
		for ( sw::Entity childEnt : children )
		{
			GameObject* pChildObj = _pOwnerManager->findGameObjectByEntity( childEnt );
			if ( pChildObj == nullptr )
				continue;
			listResult.push_back( pChildObj );
		}
		return listResult;
	}

	SceneComponent* GameObject::getPrimarySceneComponent() const
	{
		SceneComponent* pExact = getComponent<SceneComponent>().get();
		if ( pExact != nullptr )
			return pExact;

		const vector<Component*>& listComponents = getAllComponents();
		for ( Component* pComp : listComponents )
		{
			if ( pComp == nullptr )
				continue;
			SceneComponent* pSceneComp = pComp->asSceneComponent();
			if ( pSceneComp != nullptr )
				return pSceneComp;
		}
		return nullptr;
	}

	void GameObject::addTag( TagID tag )
	{
		if ( _pOwnerManager != nullptr && _pOwnerManager->isStructuralMutationFrozen() )
		{
			const uint64	   objectId = _objectId;
			GameObjectManager* pMgr		= _pOwnerManager;
			const TagID		   tagCopy	= tag;
			pMgr->deferPostTick( [pMgr, objectId, tagCopy]()
			{
				GameObject* pObj = pMgr->findGameObjectById( objectId );
				if ( pObj != nullptr )
					pObj->addTag( tagCopy );
			} );
			return;
		}

		TagData* pTagData = getComponent<TagData>().get();
		if ( pTagData == nullptr )
			pTagData = addComponent<TagData>();
		if ( pTagData != nullptr )
			pTagData->tags.addTag( tag );
	}

	void GameObject::removeTag( TagID tag )
	{
		if ( _pOwnerManager != nullptr && _pOwnerManager->isStructuralMutationFrozen() )
		{
			const uint64	   objectId = _objectId;
			GameObjectManager* pMgr		= _pOwnerManager;
			const TagID		   tagCopy	= tag;
			pMgr->deferPostTick( [pMgr, objectId, tagCopy]()
			{
				GameObject* pObj = pMgr->findGameObjectById( objectId );
				if ( pObj != nullptr )
					pObj->removeTag( tagCopy );
			} );
			return;
		}

		TComponentHandle<TagData> tagData = getComponent<TagData>();
		if ( tagData.isValid() )
			tagData->tags.removeTag( tag );
	}

	void GameObject::clearTags()
	{
		TComponentHandle<TagData> tagData = getComponent<TagData>();
		if ( tagData.isValid() )
			tagData->tags.clear();
	}

	bool GameObject::hasTag( TagID tag, bool bExactMatch ) const
	{
		TComponentHandle<TagData> tagData = getComponent<TagData>();
		if ( tagData.isValid() )
			return tagData->tags.hasTag( tag, bExactMatch );
		return false;
	}

	bool GameObject::matchTags( const TagContainer& required, const TagContainer& forbidden ) const
	{
		TComponentHandle<TagData> tagData = getComponent<TagData>();
		if ( tagData.isValid() )
			return tagData->tags.matchTags( required, forbidden );
		return required.getTagCount() == 0;
	}

	bool GameObject::matchesTagQuery( const TagQuery& query ) const
	{
		TComponentHandle<TagData> tagData = getComponent<TagData>();
		if ( tagData.isValid() )
			return query.matches( tagData->tags );
		static const TagContainer s_emptyTags;
		return query.matches( s_emptyTags );
	}

	const TagContainer& GameObject::getTags() const
	{
		static TagContainer		  s_emptyContainer;
		TComponentHandle<TagData> tagData = getComponent<TagData>();
		if ( tagData.isValid() )
			return tagData->tags;
		return s_emptyContainer;
	}

	size_t GameObject::getComponentCount() const
	{
		return getAllComponents().size();
	}

	vector<Component*> GameObject::getAllComponents() const
	{
		vector<Component*> listResult;
		forEachComponent( [&]( Component* pComp )
		{
			listResult.push_back( pComp );
		} );
		return listResult;
	}

	bool GameObject::removeComponent( Component* pComp )
	{
		if ( pComp == nullptr || _pOwnerManager == nullptr || _entityId == sw::kNullEntity )
			return false;
		if ( pComp->getOwner() != this )
			return false;

		if ( _pOwnerManager->getRegistry().isTicking() )
		{
			_pOwnerManager->destroyComponent( pComp );
			return true;
		}

		const hashed_string componentName = pComp->getComponentName();
		(void)componentName;

		unregisterComponentIfSceneRoot( pComp );
		pComp->onDestroy();
		const bool removed = _pOwnerManager->getRegistry().removeComponent( _entityId, pComp );
		if ( removed == false )
			SW_LOG_ERROR( "[GameObject] Failed to remove component '%#' from ECS pool.", componentName.c_str() );
		markTickOrderDirty();
		return removed;
	}

	void GameObject::registerComponentIfSceneRoot( Component* pComp )
	{
		if ( pComp == nullptr )
			return;
		SceneComponent* pSceneComp = pComp->asSceneComponent();
		if ( pSceneComp != nullptr )
		{
			if ( pSceneComp->getParent() == nullptr )
			{
				if ( _pOwnerManager != nullptr )
					_pOwnerManager->registerRootSceneComponent( pSceneComp );
				else
				{
					Scene* pScene = engine::getSceneManager().getActiveScene();
					if ( pScene != nullptr )
					{
						GameObjectManager* pManager = pScene->getObjectManager();
						if ( pManager != nullptr )
							pManager->registerRootSceneComponent( pSceneComp );
					}
				}
			}
		}
	}

	void GameObject::unregisterComponentIfSceneRoot( Component* pComp )
	{
		if ( pComp == nullptr )
			return;
		SceneComponent* pSceneComp = pComp->asSceneComponent();
		if ( pSceneComp != nullptr )
		{
			if ( pSceneComp->getParent() == nullptr )
			{
				if ( _pOwnerManager != nullptr )
					_pOwnerManager->unregisterRootSceneComponent( pSceneComp );
				else
				{
					Scene* pScene = engine::getSceneManager().getActiveScene();
					if ( pScene != nullptr )
					{
						GameObjectManager* pManager = pScene->getObjectManager();
						if ( pManager != nullptr )
							pManager->unregisterRootSceneComponent( pSceneComp );
					}
				}
			}
		}
	}

	void GameObject::moveStateFrom( GameObject& source )
	{
		// 1. Move properties
		setName( source._name );
		setActive( source._bActive.load( std::memory_order_relaxed ) );

		// Move ECS Entity ownership
		if ( _pOwnerManager != nullptr )
		{
			if ( _entityId != sw::kNullEntity )
				_pOwnerManager->getRegistry().destroy( _entityId );
			_entityId		 = source._entityId;
			source._entityId = sw::kNullEntity;
		}
	}

	void GameObject::markTickOrderDirty()
	{
		if ( _pOwnerManager != nullptr )
			_pOwnerManager->getRegistry().markTickWavesDirty();
	}

	std::atomic<uint64> GameObject::_s_nextObjectId{ 1 };
} // namespace sw
