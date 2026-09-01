#include "pch.h"

#include "Engine/Object/GameObject/GameObjectPtr.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
	GameObjectPtr::GameObjectPtr()
		: _targetName{}
		, _pCachedPtr{ nullptr }
		, _cachedObjectId{ 0 }
		, _pManager{ nullptr }
	{
	}

	GameObjectPtr::GameObjectPtr( GameObject* pTarget )
		: _targetName{ pTarget != nullptr ? pTarget->getName() : hashed_string{} }
		, _pCachedPtr{ pTarget }
		, _cachedObjectId{ pTarget != nullptr ? pTarget->getObjectId() : 0 }
		, _pManager{ pTarget != nullptr ? pTarget->getManager() : nullptr }
	{
	}

	GameObjectPtr::GameObjectPtr( const GameObjectPtr& other )
		: _targetName{ other._targetName }
		, _pCachedPtr{ other._pCachedPtr }
		, _cachedObjectId{ other._cachedObjectId }
		, _pManager{ other._pManager }
	{
	}

	GameObjectPtr::GameObjectPtr( GameObjectPtr&& other ) noexcept
		: _targetName{ std::move( other._targetName ) }
		, _pCachedPtr{ other._pCachedPtr }
		, _cachedObjectId{ other._cachedObjectId }
		, _pManager{ other._pManager }
	{
		other._targetName	  = hashed_string{};
		other._pCachedPtr	  = nullptr;
		other._cachedObjectId = 0;
		other._pManager		  = nullptr;
	}

	GameObjectPtr::~GameObjectPtr() = default;

	GameObjectPtr& GameObjectPtr::operator=( GameObject* pTarget )
	{
		_targetName		= pTarget != nullptr ? pTarget->getName() : hashed_string{};
		_pCachedPtr		= pTarget;
		_cachedObjectId = pTarget != nullptr ? pTarget->getObjectId() : 0;
		_pManager		= pTarget != nullptr ? pTarget->getManager() : nullptr;
		return *this;
	}

	GameObjectPtr& GameObjectPtr::operator=( const GameObjectPtr& other )
	{
		if ( this != &other )
		{
			_targetName		= other._targetName;
			_pCachedPtr		= other._pCachedPtr;
			_cachedObjectId = other._cachedObjectId;
			_pManager		= other._pManager;
		}
		return *this;
	}

	GameObjectPtr& GameObjectPtr::operator=( GameObjectPtr&& other ) noexcept
	{
		if ( this != &other )
		{
			_targetName			  = std::move( other._targetName );
			_pCachedPtr			  = other._pCachedPtr;
			_cachedObjectId		  = other._cachedObjectId;
			_pManager			  = other._pManager;
			other._targetName	  = hashed_string{};
			other._pCachedPtr	  = nullptr;
			other._cachedObjectId = 0;
			other._pManager		  = nullptr;
		}
		return *this;
	}

	void GameObjectPtr::resolveLazy() const
	{
		if ( _targetName.getHash() == 0 )
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

		// 1) 빠른 경로: 캐시된 오브젝트 ID가 여전히 유효하고 이름이 일치하는지 확인
		if ( _cachedObjectId != 0 )
		{
			GameObject* pFound = pObjMgr->findGameObjectById( _cachedObjectId );
			if ( pFound != nullptr && pFound == _pCachedPtr && pFound->getName() == _targetName && pFound->isPendingKill() == false )
			{
				return; // 캐시 유효
			}
		}

		// 2) 느린 경로: 이름으로 룩업하여 갱신 (핫리로드 후 등)
		_pCachedPtr = pObjMgr->findGameObjectByName( _targetName );
		if ( _pCachedPtr != nullptr && _pCachedPtr->isPendingKill() == false )
		{
			_cachedObjectId = _pCachedPtr->getObjectId();
		}
		else
		{
			_pCachedPtr		= nullptr;
			_cachedObjectId = 0;
		}
	}

} // namespace sw
