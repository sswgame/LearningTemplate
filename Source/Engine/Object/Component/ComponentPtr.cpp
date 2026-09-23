#include "pch.h"

#include "Engine/Object/Component/ComponentPtr.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    ComponentPtr::ComponentPtr()
        : _targetObjectName{}
        , _targetComponentType{}
        , _pCachedPtr{ nullptr }
        , _cachedObjectId{ 0 }
        , _cachedComponentGeneration{ 0 }
        , _pManager{ nullptr }
    {
    }

    ComponentPtr::ComponentPtr( Component* pTarget )
        : _targetObjectName{}
        , _targetComponentType{}
        , _pCachedPtr{ pTarget }
        , _cachedObjectId{ 0 }
        , _cachedComponentGeneration{ 0 }
        , _pManager{ nullptr }
    {
        if ( pTarget != nullptr && pTarget->getOwner() != nullptr )
        {
            _targetObjectName          = pTarget->getOwner()->getName();
            _targetComponentType       = pTarget->getComponentName();
            _cachedObjectId            = pTarget->getOwner()->getObjectId();
            _cachedComponentGeneration = pTarget->getOwner()->getComponentGeneration();
            _pManager                  = pTarget->getOwner()->getManager();
        }
    }

    ComponentPtr::ComponentPtr( ComponentPtr&& other ) noexcept
        : _targetObjectName{ other._targetObjectName }
        , _targetComponentType{ other._targetComponentType }
        , _pCachedPtr{ other._pCachedPtr }
        , _cachedObjectId{ other._cachedObjectId }
        , _cachedComponentGeneration{ other._cachedComponentGeneration }
        , _pManager{ other._pManager }
    {
        other._pCachedPtr                = nullptr;
        other._targetObjectName          = hashed_string{};
        other._targetComponentType       = hashed_string{};
        other._cachedObjectId            = 0;
        other._cachedComponentGeneration = 0;
        other._pManager                  = nullptr;
    }

    ComponentPtr& ComponentPtr::operator=( Component* pTarget )
    {
        _pCachedPtr = pTarget;
        if ( pTarget != nullptr && pTarget->getOwner() != nullptr )
        {
            _targetObjectName          = pTarget->getOwner()->getName();
            _targetComponentType       = pTarget->getComponentName();
            _cachedObjectId            = pTarget->getOwner()->getObjectId();
            _cachedComponentGeneration = pTarget->getOwner()->getComponentGeneration();
            _pManager                  = pTarget->getOwner()->getManager();
        }
        else
        {
            _targetObjectName          = hashed_string{};
            _targetComponentType       = hashed_string{};
            _cachedObjectId            = 0;
            _cachedComponentGeneration = 0;
            _pManager                  = nullptr;
        }
        return *this;
    }

    ComponentPtr& ComponentPtr::operator=( ComponentPtr&& other ) noexcept
    {
        if ( this != &other )
        {
            _targetObjectName          = other._targetObjectName;
            _targetComponentType       = other._targetComponentType;
            _pCachedPtr                = other._pCachedPtr;
            _cachedObjectId            = other._cachedObjectId;
            _cachedComponentGeneration = other._cachedComponentGeneration;
            _pManager                  = other._pManager;

            other._targetObjectName          = hashed_string{};
            other._targetComponentType       = hashed_string{};
            other._pCachedPtr                = nullptr;
            other._cachedObjectId            = 0;
            other._cachedComponentGeneration = 0;
            other._pManager                  = nullptr;
        }
        return *this;
    }

    void ComponentPtr::resolveLazy() const
    {
        if ( _targetObjectName.getHash() == 0 || _targetComponentType.getHash() == 0 )
        {
            _pCachedPtr     = nullptr;
            _cachedObjectId = 0;
            return;
        }

        // 씬이 통째로 바뀌어도 이름으로 다시 찾는다 — 어느 매니저에게 물을지는 한 곳이 정한다.
        GameObjectManager* pObjMgr = GameObjectManager::resolveOwningManager( _pManager );
        if ( pObjMgr == nullptr )
            return;

        // 1) 빠른 경로 — 소유자가 살아 있고 목록 세대가 캐시를 잡을 때와 같으면 포인터는 아직 그 목록에 있다(떼는 길은 전부
        //    세대를 올린다). 예전에는 접근마다 목록을 훑어 포인터를 찾았다 — 루프 안에서 부르면 그 훑기가 비용이었다.
        if ( _cachedObjectId != 0 && _pCachedPtr != nullptr )
        {
            GameObject* pFound = pObjMgr->findGameObjectById( _cachedObjectId );
            if ( pFound != nullptr && pFound->isPendingKill() == false &&
                 pFound->getComponentGeneration() == _cachedComponentGeneration && _pCachedPtr->isPendingKill() == false &&
                 pFound->getName() == _targetObjectName && _pCachedPtr->getComponentName() == _targetComponentType )
                return; // 캐시 유효
        }

        // 2) 느린 경로
        _pCachedPtr     = nullptr;
        _cachedObjectId = 0;

        GameObject* pObj = pObjMgr->findGameObjectByName( _targetObjectName );
        if ( pObj != nullptr && pObj->isPendingKill() == false )
        {
            for ( Component* pComp : pObj->getComponents() )
            {
                if ( pComp != nullptr && pComp->isPendingKill() == false && pComp->getComponentName() == _targetComponentType )
                {
                    _pCachedPtr                = pComp;
                    _cachedObjectId            = pObj->getObjectId();
                    _cachedComponentGeneration = pObj->getComponentGeneration();
                    break;
                }
            }
        }
    }

} // namespace sw
