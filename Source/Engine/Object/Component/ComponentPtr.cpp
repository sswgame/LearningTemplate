#include "pch.h"

#include "Engine/Object/Component/ComponentPtr.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
	ComponentPtr::ComponentPtr()
		: _targetObjectName{}
		, _targetComponentType{}
		, _pCachedPtr{ nullptr }
		, _cachedObjectId{ 0 }
		, _pManager{ nullptr }
	{
	}

	ComponentPtr::ComponentPtr( Component* pTarget )
		: _targetObjectName{}
		, _targetComponentType{}
		, _pCachedPtr{ pTarget }
		, _cachedObjectId{ 0 }
	{
		if ( pTarget != nullptr && pTarget->getOwner() != nullptr )
		{
			_targetObjectName	 = pTarget->getOwner()->getName();
			_targetComponentType = pTarget->getComponentName();
			_cachedObjectId		 = pTarget->getOwner()->getObjectId();
			_pManager			 = pTarget->getOwner()->getManager();
		}
	}

	ComponentPtr::ComponentPtr( const ComponentPtr& other )
		: _targetObjectName{ other._targetObjectName }
		, _targetComponentType{ other._targetComponentType }
		, _pCachedPtr{ other._pCachedPtr }
		, _cachedObjectId{ other._cachedObjectId }
		, _pManager{ other._pManager }
	{
	}

	ComponentPtr::ComponentPtr( ComponentPtr&& other ) noexcept
		: _targetObjectName{ std::move( other._targetObjectName ) }
		, _targetComponentType{ std::move( other._targetComponentType ) }
		, _pCachedPtr{ other._pCachedPtr }
		, _cachedObjectId{ other._cachedObjectId }
		, _pManager{ other._pManager }
	{
		other._pCachedPtr		   = nullptr;
		other._targetObjectName	   = hashed_string{};
		other._targetComponentType = hashed_string{};
		other._cachedObjectId	   = 0;
		other._pManager			   = nullptr;
	}

	ComponentPtr::~ComponentPtr() = default;

	ComponentPtr& ComponentPtr::operator=( Component* pTarget )
	{
		_pCachedPtr = pTarget;
		if ( pTarget != nullptr && pTarget->getOwner() != nullptr )
		{
			_targetObjectName	 = pTarget->getOwner()->getName();
			_targetComponentType = pTarget->getComponentName();
			_cachedObjectId		 = pTarget->getOwner()->getObjectId();
			_pManager			 = pTarget->getOwner()->getManager();
		}
		else
		{
			_targetObjectName	 = hashed_string{};
			_targetComponentType = hashed_string{};
			_cachedObjectId		 = 0;
			_pManager			 = nullptr;
		}
		return *this;
	}

	ComponentPtr& ComponentPtr::operator=( const ComponentPtr& other )
	{
		if ( this != &other )
		{
			_targetObjectName	 = other._targetObjectName;
			_targetComponentType = other._targetComponentType;
			_pCachedPtr			 = other._pCachedPtr;
			_cachedObjectId		 = other._cachedObjectId;
			_pManager			 = other._pManager;
		}
		return *this;
	}

	ComponentPtr& ComponentPtr::operator=( ComponentPtr&& other ) noexcept
	{
		if ( this != &other )
		{
			_targetObjectName	 = std::move( other._targetObjectName );
			_targetComponentType = std::move( other._targetComponentType );
			_pCachedPtr			 = other._pCachedPtr;
			_cachedObjectId		 = other._cachedObjectId;
			_pManager			 = other._pManager;

			other._targetObjectName	   = hashed_string{};
			other._targetComponentType = hashed_string{};
			other._pCachedPtr		   = nullptr;
			other._cachedObjectId	   = 0;
			other._pManager			   = nullptr;
		}
		return *this;
	}

	void ComponentPtr::resolveLazy() const
	{
		if ( _targetObjectName.getHash() == 0 || _targetComponentType.getHash() == 0 )
		{
			_pCachedPtr		= nullptr;
			_cachedObjectId = 0;
			return;
		}

		GameObjectManager* pObjMgr = _pManager;
		if ( pObjMgr == nullptr && engine::areEngineServicesBound() )
		{
			SceneManager& sceneMgr = engine::getSceneManager();
			Scene*		  pScene   = sceneMgr.getActiveScene();
			if ( pScene != nullptr )
			{
				pObjMgr = pScene->getObjectManager();
			}
		}

		if ( pObjMgr == nullptr )
			return;

		// 1) 빠른 경로
		if ( _cachedObjectId != 0 )
		{
			GameObject* pFound = pObjMgr->findGameObjectById( _cachedObjectId );
			if ( pFound != nullptr && pFound->isPendingKill() == false )
			{
				bool bComponentFound = false;
				for ( Component* pComp : pFound->getAllComponents() )
				{
					if ( pComp == _pCachedPtr && pComp->isPendingKill() == false )
					{
						bComponentFound = true;
						break;
					}
				}

				if ( bComponentFound && pFound->getName() == _targetObjectName && _pCachedPtr->getComponentName() == _targetComponentType )
				{
					return; // 캐시 유효
				}
			}
		}

		// 2) 느린 경로
		_pCachedPtr		= nullptr;
		_cachedObjectId = 0;

		GameObject* pObj = pObjMgr->findGameObjectByName( _targetObjectName );
		if ( pObj != nullptr && pObj->isPendingKill() == false )
		{
			for ( Component* pComp : pObj->getAllComponents() )
			{
				if ( pComp->getComponentName() == _targetComponentType )
				{
					_pCachedPtr		= pComp;
					_cachedObjectId = pObj->getObjectId();
					break;
				}
			}
		}
	}

} // namespace sw
