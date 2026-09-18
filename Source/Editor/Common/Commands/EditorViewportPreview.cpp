#include "pch.h"

#include "Editor/Common/Commands/EditorViewportPreview.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw::editor
{
    namespace
    {
        struct EditorViewportPreviewInternal
        {
            static GameObject* getPrimaryObject()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return nullptr;
                return pContext->getWorkspace().getSelectedObject().get();
            }

            static GameObjectManager* getActiveObjectManager()
            {
                Scene* pScene = editor::getActiveScene();
                if ( pScene == nullptr )
                    return nullptr;
                return pScene->getObjectManager();
            }

            static bool matchesAnimationGraphPath( const SpriteAnimatorComponent* pAnimator, string_view graphPath )
            {
                if ( pAnimator == nullptr )
                    return false;
                if ( graphPath.empty() )
                    return true;
                const string& animatorPath = pAnimator->getAnimationGraphPath();
                if ( animatorPath.empty() )
                    return true;
                return FileUtil::pathsEqualNormalized( animatorPath, graphPath );
            }

            static bool isDialogueRunnerType( const TypeInfo* pType )
            {
                if ( pType == nullptr )
                    return false;
                if ( pType->_name == hashed_string( "DialogueRunnerComponent" ) )
                    return true;
                return pType->_fullyQualifiedName == hashed_string( "sw::DialogueRunnerComponent" );
            }

            static void invokeDialoguePreviewLine( Component* pComp, string_view speaker, string_view text )
            {
                if ( pComp == nullptr )
                    return;
                const TypeInfo* pType = pComp->getTypeInfo();
                if ( pType == nullptr )
                    return;
                TypeRegistry* pRegistry = editor::getService<TypeRegistry>();
                if ( pRegistry == nullptr )
                    return;
                TaskArgs args;
                args.add( string{ speaker } );
                args.add( string{ text } );
                pRegistry->invokeMethod( pComp, pType->_fullyQualifiedName, hashed_string( "previewLine" ), args );
            }
        };

        /**
         * @brief 프리뷰가 `MaterialCache` 에서 잡아 둔 머티리얼 경로. 비어 있으면 잡은 것이 없습니다.
         * @details `acquire` 는 참조를 하나 올린다. 예전에는 `applyMaterial` 이 부를 때마다 올리기만
         *          하고 내리지 않아서, 머티리얼을 한 번 편집할 때마다 참조가 하나씩 쌓였다 — 그 뒤로
         *          그 머티리얼은 참조가 0 에 닿지 못해 캐시에서 영영 지워지지 않는다.
         *          프리뷰가 드는 참조는 **하나뿐**이다: 같은 경로면 다시 잡지 않고, 다른 경로로 갈
         *          때는 새 것을 메시에 건 **뒤에** 옛 것을 놓는다(놓는 순간 사라질 수 있으므로 순서가 중요하다).
         */
        string s_acquiredPreviewMaterialPath;
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorViewportPreview" );

    void EditorViewportPreview::applyAnimationNode( string_view nodeName, string_view graphPath )
    {
        if ( nodeName.empty() )
            return;

        const string nodeNameStr{ nodeName };
        GameObject*  pPrimary = EditorViewportPreviewInternal::getPrimaryObject();
        if ( pPrimary != nullptr )
        {
            SpriteAnimatorComponent* pPrimaryAnimator = pPrimary->getComponent<SpriteAnimatorComponent>();
            if ( pPrimaryAnimator != nullptr )
                pPrimaryAnimator->play( nodeNameStr, false );
        }

        GameObjectManager* pManager = EditorViewportPreviewInternal::getActiveObjectManager();
        if ( pManager == nullptr )
            return;
        const vector<GameObject*> listObject = pManager->getAllGameObjects();
        for ( GameObject* pObject : listObject )
        {
            if ( pObject == nullptr || pObject == pPrimary )
                continue;
            SpriteAnimatorComponent* pAnimator = pObject->getComponent<SpriteAnimatorComponent>();
            if ( EditorViewportPreviewInternal::matchesAnimationGraphPath( pAnimator, graphPath ) == false )
                continue;
            pAnimator->play( nodeNameStr, false );
        }
    }

    void EditorViewportPreview::applySequenceFrame( const SequenceAsset& asset, int32 frame )
    {
        SequenceTimelineUtil::applyFrame( EditorViewportPreviewInternal::getActiveObjectManager(), asset, frame );
    }

    void EditorViewportPreview::applyDialogueLine( string_view speaker, string_view text )
    {
        GameObjectManager* pManager = EditorViewportPreviewInternal::getActiveObjectManager();
        if ( pManager != nullptr )
        {
            if ( speaker.empty() == false )
            {
                GameObject* pSpeaker = pManager->findGameObjectByName( hashed_string{ speaker } );
                if ( pSpeaker != nullptr )
                {
                    EditorContext* pContext = EditorContext::get();
                    if ( pContext != nullptr )
                        pContext->getWorkspace().selectGameObject( GameObjectPtr{ pSpeaker } );
                }
            }

            const vector<GameObject*> listObject = pManager->getAllGameObjects();
            for ( GameObject* pObject : listObject )
            {
                if ( pObject == nullptr )
                    continue;
                const vector<Component*> listComp = pObject->getAllComponents();
                for ( Component* pComp : listComp )
                {
                    if ( pComp == nullptr )
                        continue;
                    if ( EditorViewportPreviewInternal::isDialogueRunnerType( pComp->getTypeInfo() ) == false )
                        continue;
                    EditorViewportPreviewInternal::invokeDialoguePreviewLine( pComp, speaker, text );
                }
            }
        }
        if ( speaker.empty() == false || text.empty() == false )
            SW_LOG_INFO( "Dialogue preview [%#]: %#", string{ speaker }.c_str(), string{ text }.c_str() );
    }

    void EditorViewportPreview::applyMaterial( Material* pMaterial, string_view assetPath )
    {
        string previousAcquiredPath;
        if ( assetPath.empty() == false )
        {
            ResourceManager* pResources = editor::getService<ResourceManager>();
            if ( pResources != nullptr )
            {
                // 같은 경로를 다시 걸 때는 이미 들고 있는 참조를 그대로 쓴다 — 부를 때마다 올리면
                // 편집 한 번에 참조가 하나씩 쌓이고, 그 머티리얼은 캐시에서 영영 지워지지 않는다.
                const bool bAlreadyHeld = ( s_acquiredPreviewMaterialPath == assetPath );
                Material*  pCached      = pResources->getMaterialManager().acquire( assetPath, nullptr );
                if ( pCached != nullptr )
                {
                    if ( bAlreadyHeld )
                    {
                        // 방금 올린 몫은 곧바로 되돌린다 — 프리뷰가 드는 참조는 언제나 하나다.
                        pResources->getMaterialManager().release( assetPath );
                    }
                    else
                    {
                        previousAcquiredPath          = s_acquiredPreviewMaterialPath;
                        s_acquiredPreviewMaterialPath = string{ assetPath };
                    }

                    if ( pMaterial != nullptr )
                    {
                        pCached->loadFromXml( pMaterial->saveToString() );
                        pCached->rebuildPackedBuffer();
                        pMaterial = pCached;
                    }
                }
            }
        }

        GameObject* pPrimary = EditorViewportPreviewInternal::getPrimaryObject();

        MeshComponent* pMesh = ( pPrimary != nullptr ) ? pPrimary->getComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr && pMaterial != nullptr )
            pMesh->setMaterial( pMaterial );

        // 옛 참조는 **새 것을 건 뒤에** 놓는다. 먼저 놓으면 참조가 0 이 되어 캐시가 지우는데,
        // 메시가 아직 그 포인터를 들고 있을 수 있다.
        if ( previousAcquiredPath.empty() == false )
        {
            ResourceManager* pResources = editor::getService<ResourceManager>();
            if ( pResources != nullptr )
                pResources->getMaterialManager().release( previousAcquiredPath );
        }

        if ( pPrimary == nullptr )
            return;

        SpriteComponent* pSprite = pPrimary->getComponent<SpriteComponent>();
        if ( pSprite != nullptr && assetPath.empty() == false )
            pSprite->setMaterialName( string{ assetPath } );
    }
} // namespace sw::editor
