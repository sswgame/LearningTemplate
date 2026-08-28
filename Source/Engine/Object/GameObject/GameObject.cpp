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
	SW_LOG_CALLER( "GameObject" );

	GameObject::GameObject()
		: _objectId{ _s_nextObjectId.fetch_add( 1, std::memory_order_relaxed ) }
		, _name{ "GameObject" }
		, _pOwnerManager{ nullptr }
		, _managerIndex{ static_cast<uint32>( -1 ) }
		, _bActive{ true }
		, _bIsActiveInHierarchy{ true }
		, _bIsPendingKill{ false }
		, _listComponent{}
	{
	}

	GameObject::GameObject( hashed_string name )
		: _objectId{ _s_nextObjectId.fetch_add( 1, std::memory_order_relaxed ) }
		, _name{ name }
		, _pOwnerManager{ nullptr }
		, _managerIndex{ static_cast<uint32>( -1 ) }
		, _bActive{ true }
		, _bIsActiveInHierarchy{ true }
		, _bIsPendingKill{ false }
		, _listComponent{}
	{
	}

	/**
	 * @brief 게임 오브젝트 소멸자: 부모 계층 연결을 해제하고 소유 컴포넌트를 파괴합니다.
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

		clearComponents();
	}

	const TypeInfo* GameObject::getTypeInfo() const
	{
		return StaticType();
	}

	/**
	 * @brief 게임플레이 시작(Play Mode) 시 소유한 모든 활성 컴포넌트의 onBeginPlay를 호출합니다.
	 */
	void GameObject::beginPlay()
	{
		for ( Component* pComp : _listComponent )
		{
			if ( pComp == nullptr || pComp->isPendingKill() || pComp->isActive() == false )
				continue;
			pComp->onBeginPlay();
		}
	}

	/**
	 * @brief 게임플레이 종료 시 소유한 모든 컴포넌트의 onEndPlay를 호출합니다.
	 */
	void GameObject::endPlay()
	{
		for ( Component* pComp : _listComponent )
		{
			if ( pComp == nullptr || pComp->isPendingKill() || pComp->isActive() == false )
				continue;
			pComp->onEndPlay();
		}
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

		for ( Component* pComp : _listComponent )
		{
			if ( pComp == nullptr || pComp->isPendingKill() )
				continue;
			pComp->setActive( bActive );
		}
		onPropertyChanged( hashed_string( "_bActive" ) );
	}

	bool GameObject::attachToParent( GameObject* pParent )
	{
		if ( pParent == nullptr )
			return false;
		if ( getParent() == pParent )
			return true;

		GameObjectManager* pMgr = getManager();
		if ( pMgr != nullptr && pMgr->isParallelTransformReadOnly() )
		{
			const uint64 childId  = _objectId;
			const uint64 parentId = pParent->getObjectId();
			pMgr->deferTransformUpdate( [pMgr, childId, parentId]()
			{
				GameObject* pChildObj  = pMgr->findGameObjectById( childId );
				GameObject* pParentObj = pMgr->findGameObjectById( parentId );
				if ( pChildObj != nullptr && pParentObj != nullptr )
					pChildObj->attachToParent( pParentObj );
			} );
			return true;
		}

		SceneComponent* pChildSc = getPrimarySceneComponent();
		if ( pChildSc == nullptr )
			return false;

		SceneComponent* pParentSc = pParent->getPrimarySceneComponent();
		if ( pParentSc == nullptr )
			return false;

		bool bAttached = pChildSc->attachToComponent( pParentSc );
		if ( bAttached )
			refreshActiveInHierarchy();

		return bAttached;
	}

	void GameObject::detachFromParent()
	{
		GameObjectManager* pMgr = getManager();
		if ( pMgr != nullptr && pMgr->isParallelTransformReadOnly() )
		{
			const uint64 childId = _objectId;
			pMgr->deferTransformUpdate( [pMgr, childId]()
			{
				GameObject* pChildObj = pMgr->findGameObjectById( childId );
				if ( pChildObj != nullptr )
					pChildObj->detachFromParent();
			} );
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
		SceneComponent* pSceneComp = getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return nullptr;
		SceneComponent* pParentComp = pSceneComp->getParent();
		if ( pParentComp == nullptr )
			return nullptr;
		return pParentComp->getOwner();
	}

	void GameObject::refreshActiveInHierarchy()
	{
		bool		bParentActive = true;
		GameObject* pParent		  = getParent();
		if ( pParent != nullptr )
			bParentActive = pParent->isActiveInHierarchy();

		_bIsActiveInHierarchy.store( bParentActive, std::memory_order_relaxed );

		for ( GameObject* pChild : getChildren() )
		{
			if ( pChild != nullptr )
				pChild->refreshActiveInHierarchy();
		}
	}

	vector<GameObject*> GameObject::getChildren() const
	{
		vector<GameObject*> listResult;
		SceneComponent*		pSceneComp = getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return listResult;

		const vector<SceneComponent*>& listChildComps = pSceneComp->getChildren();
		listResult.reserve( listChildComps.size() );
		for ( SceneComponent* pChildComp : listChildComps )
		{
			if ( pChildComp == nullptr )
				continue;
			GameObject* pChildObj = pChildComp->getOwner();
			if ( pChildObj == nullptr )
				continue;
			listResult.push_back( pChildObj );
		}
		return listResult;
	}

	SceneComponent* GameObject::getPrimarySceneComponent() const
	{
		return getComponent<SceneComponent>();
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

		TagComponent* pTagComp = getComponent<TagComponent>();
		if ( pTagComp == nullptr )
			pTagComp = addComponent<TagComponent>();
		if ( pTagComp != nullptr )
			pTagComp->addTag( tag );
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

		TagComponent* pTagComp = getComponent<TagComponent>();
		if ( pTagComp != nullptr )
			pTagComp->removeTag( tag );
	}

	void GameObject::clearTags()
	{
		TagComponent* pTagComp = getComponent<TagComponent>();
		if ( pTagComp != nullptr )
			pTagComp->clearTags();
	}

	bool GameObject::hasTag( TagID tag, bool bExactMatch ) const
	{
		const TagComponent* pTagComp = getComponent<TagComponent>();
		if ( pTagComp == nullptr )
			return false;
		return pTagComp->hasTag( tag, bExactMatch );
	}

	bool GameObject::matchesTagQuery( const TagQuery& query ) const
	{
		const TagComponent* pTagComp = getComponent<TagComponent>();
		if ( pTagComp == nullptr )
		{
			static const TagContainer s_emptyTags;
			return query.matches( s_emptyTags );
		}
		return pTagComp->matchesQuery( query );
	}

	const TagContainer& GameObject::getTags() const
	{
		const TagComponent* pTagComp = getComponent<TagComponent>();
		if ( pTagComp == nullptr )
		{
			static const TagContainer s_emptyTags;
			return s_emptyTags;
		}
		return pTagComp->getTags();
	}

	TagContainer& GameObject::getTags()
	{
		TagComponent* pTagComp = getComponent<TagComponent>();
		if ( pTagComp == nullptr )
			pTagComp = addComponent<TagComponent>();
		if ( pTagComp != nullptr )
			return pTagComp->getTags();

		static TagContainer s_emptyTags;
		return s_emptyTags;
	}

	size_t GameObject::getComponentCount() const
	{
		size_t count = 0;
		for ( Component* pComp : _listComponent )
		{
			if ( pComp != nullptr && pComp->isPendingKill() == false )
				++count;
		}
		return count;
	}

	vector<Component*> GameObject::getAllComponents() const
	{
		vector<Component*> listResult;
		for ( Component* pComp : _listComponent )
		{
			if ( pComp != nullptr && pComp->isPendingKill() == false )
				listResult.push_back( pComp );
		}
		return listResult;
	}

	void GameObject::clearComponents()
	{
		vector<Component*> listOwned = _listComponent;
		_listComponent.clear();
		for ( Component* pComp : listOwned )
		{
			if ( pComp == nullptr )
				continue;
			unregisterComponentIfSceneRoot( pComp );
			pComp->onDestroy();
			pComp->setOwner( nullptr );
			sw_delete( pComp );
		}
		markTickOrderDirty();
	}

	Component* GameObject::findComponentById( uint64 componentId, bool bIncludePendingKill ) const
	{
		for ( Component* pComp : _listComponent )
		{
			if ( pComp == nullptr || pComp->getComponentId() != componentId )
				continue;
			if ( bIncludePendingKill == false && pComp->isPendingKill() )
				return nullptr;
			return pComp;
		}
		return nullptr;
	}

	bool GameObject::removeComponent( Component* pComp )
	{
		if ( pComp == nullptr || pComp->getOwner() != this )
			return false;

		if ( _pOwnerManager != nullptr && _pOwnerManager->isStructuralMutationFrozen() )
		{
			_pOwnerManager->destroyComponent( pComp );
			return true;
		}

		const hashed_string componentName = pComp->getComponentName();

		unregisterComponentIfSceneRoot( pComp );
		pComp->onDestroy();
		pComp->setOwner( nullptr );

		bool bRemoved = false;
		for ( size_t compIndex = 0; compIndex < _listComponent.size(); ++compIndex )
		{
			if ( _listComponent[compIndex] == pComp )
			{
				_listComponent[compIndex] = _listComponent.back();
				_listComponent.pop_back();
				bRemoved = true;
				break;
			}
		}
		if ( bRemoved == false )
			SW_LOG_ERROR( "Failed to remove component '%#' from actor list.", componentName.c_str() );
		else
			sw_delete( pComp );
		markTickOrderDirty();
		return bRemoved;
	}

	void GameObject::registerComponentIfSceneRoot( Component* pComp )
	{
		if ( pComp == nullptr )
			return;
		SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
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
		SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
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

	void GameObject::markTickOrderDirty()
	{
		if ( _pOwnerManager != nullptr )
			_pOwnerManager->markTickWavesDirty();
	}

	void GameObject::prepareSerialize() const
	{
		for ( Component* pComp : _listComponent )
		{
			SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
			if ( pSceneComp != nullptr )
				pSceneComp->syncAttachSerializeFields();
		}
	}

	void GameObject::applyLoadedHierarchy()
	{
		for ( Component* pComp : _listComponent )
		{
			SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
			if ( pSceneComp != nullptr )
				pSceneComp->applyAttachSerializeFields();
		}
	}

	std::atomic<uint64> GameObject::_s_nextObjectId{ 1 };
} // namespace sw
