#include "pch.h"

#include "TestFramework/TestFixture.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace test
{
    TestFixture::TestFixture( TestContext& context )
        : _pContext{ &context }
        , _pObjectManager{ getObjectManager() }
    {
        if ( _pObjectManager == nullptr )
            return;

        const sw::vector<sw::GameObject*> listGameObject = _pObjectManager->getAllGameObjects();
        _listBaselineObjectId.reserve( listGameObject.size() );
        for ( sw::GameObject* pObject : listGameObject )
        {
            if ( pObject != nullptr )
                _listBaselineObjectId.push_back( pObject->getObjectId() );
        }

        sw::GameObjectManager*   pObjectManager       = _pObjectManager;
        const sw::vector<uint64> listBaselineObjectId = _listBaselineObjectId;
        _pContext->deferCleanup( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [pObjectManager, listBaselineObjectId]()
        {
            if ( pObjectManager == nullptr )
                return;

            const sw::vector<sw::GameObject*> listCurrentGameObject = pObjectManager->getAllGameObjects();
            for ( sw::GameObject* pObject : listCurrentGameObject )
            {
                bool isBaselineObject = false;
                if ( pObject != nullptr )
                {
                    for ( const uint64 baselineObjectId : listBaselineObjectId )
                    {
                        if ( baselineObjectId == pObject->getObjectId() )
                        {
                            isBaselineObject = true;
                            break;
                        }
                    }
                }
                if ( pObject != nullptr && isBaselineObject == false )
                    pObjectManager->destroyObject( pObject, false );
            }
            pObjectManager->processDeferredDestruction();
        } ) );
    }

    sw::Scene* TestFixture::getActiveScene() const
    {
        if ( sw::engine::areEngineServicesBound() == false )
            return nullptr;
        return sw::engine::getSceneManager().getActiveScene();
    }

    sw::GameObjectManager* TestFixture::getObjectManager() const
    {
        sw::Scene* pScene = getActiveScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }

    sw::EventDispatcher& TestFixture::getEventDispatcher() const
    {
        return sw::engine::getEventDispatcher();
    }

    void TestFixture::deferCleanup( sw::Delegate<void()> cleanup ) const
    {
        _pContext->deferCleanup( cleanup );
    }
} // namespace test
