#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Animation/Component/SpriteAnimatorComponent.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Resource/AssetManager.h"
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
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                return editorContext.getSubsystem<EditorWorkspace>().getSelectedObject();
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

            /** @brief 대사 미리보기 메서드의 시그니처( string speaker, string text )인지입니다. */
            static bool takesDialogueLineArgs( const FunctionInfo& method )
            {
                // 인자 타입은 넘길 C++ 타입의 내장 자리(`ReflectBuiltinOf<string>`)로 묻는다 — 이름 리터럴을 두지 않는다.
                const auto isTextParameter = []( const FunctionParameterInfo& parameter )
                { return ReflectValueUtil::findBuiltinIndex( hashed_string( parameter._typeName.c_str() ) ) == ReflectBuiltinOf<string>::kIndex; };
                const bool bTwoTextParameters = method._listParameter.size() == 2 && isTextParameter( method._listParameter[0] ) && isTextParameter( method._listParameter[1] );
                return bTwoTextParameters && method._invoker.isBound();
            }

            /**
             * @brief 프리뷰가 `MaterialCache` 에서 잡아 둔 머티리얼 경로입니다. 비어 있으면 잡은 것이 없습니다.
             * @details `acquire` 는 참조를 하나 늘립니다. 프리뷰가 가진 참조는 **하나뿐**입니다 — 부를 때마다 늘리기만 하면 편집할 때마다
             *          참조가 쌓여 그 머티리얼은 캐시에서 영영 지워지지 않습니다. 같은 경로면 다시 잡지 않고, 다른 경로로 갈
             *          때는 새 것을 메시에 건 **뒤에** 옛 것을 놓습니다(놓는 순간 사라질 수 있으므로 순서가 중요합니다).
             *          프로세스 정적 문자열이 아니라 에디터 범위의 상태 저장소에 둔다 — 범위마다 따로이고, 에디터 UI 스레드만 만진다(`EditorStateStore`).
             */
            struct PreviewMaterialState
            {
                string _acquiredPath;
            };
        };
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

        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return; // 선택(②): 미리보기는 씬이 없는 에디터에서도 열 수 있다. 재생할 대상이 없다
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
        SequenceTimelineUtil::applyFrame( editor::getActiveObjectManager(), asset, frame );
    }

    void EditorViewportPreview::applyDialogueLine( string_view speaker, string_view text )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager != nullptr )
        {
            if ( speaker.empty() == false )
            {
                GameObject* pSpeaker = pManager->findGameObjectByName( hashed_string{ speaker } );
                if ( pSpeaker != nullptr )
                    editor::getSubsystem<EditorWorkspace>().selectGameObject( pSpeaker ); // 필수
            }

            const vector<GameObject*> listObject = pManager->getAllGameObjects();
            for ( GameObject* pObject : listObject )
            {
                if ( pObject == nullptr )
                    continue;
                const vector<Component*> listComp( pObject->getComponents().begin(), pObject->getComponents().end() );
                for ( Component* pComp : listComp )
                {
                    const TypeInfo* pType = ( pComp != nullptr ) ? pComp->getTypeInfo() : nullptr;
                    if ( pType == nullptr )
                        continue;
                    const FunctionInfo* pMethod = findPreviewMethod( *pType, kDialogueLinePreview );
                    if ( pMethod == nullptr )
                        continue;
                    if ( EditorViewportPreviewInternal::takesDialogueLineArgs( *pMethod ) == false )
                    {
                        SW_LOG_WARNING( "%#::%# is EditorPreview=%# but does not take ( string speaker, string text )",
                                        pType->_name.c_str(), pMethod->_name.c_str(), kDialogueLinePreview );
                        continue;
                    }
                    pMethod->_invoker( pComp, TaskArgs{ string{ speaker }, string{ text } } );
                }
            }
        }
        if ( speaker.empty() == false || text.empty() == false )
            SW_LOG_INFO( "Dialogue preview [%#]: %#", string{ speaker }.c_str(), string{ text }.c_str() );
    }

    const FunctionInfo* EditorViewportPreview::findPreviewMethod( const TypeInfo& type, string_view previewKind )
    {
#if !defined( SW_SHIPPING )
        if ( previewKind.empty() )
            return nullptr;
        for ( const TypeInfo* pType = &type; pType != nullptr; pType = pType->getParentType() )
        {
            for ( const FunctionInfo& method : pType->_listMethod )
            {
                if ( method._metadata->_editorPreview == previewKind )
                    return &method;
            }
        }
#else
        (void)type;
        (void)previewKind;
#endif
        return nullptr;
    }

    void EditorViewportPreview::applyMaterial( Material* pMaterial, string_view assetPath )
    {
        string previousAcquiredPath;
        if ( assetPath.empty() == false )
        {
            AssetManager* pResources = &editor::getSubsystem<AssetManager>(); // 필수 - 머티리얼 캐시가 없으면 미리보기가 참조를 못 쥔다
            {
                // 같은 경로를 다시 걸 때는 이미 보관하고 있는 참조를 그대로 쓴다. 부를 때마다 올리면 편집 한 번에 참조가 하나씩 쌓이고,
                // 그 머티리얼은 캐시에서 영영 지워지지 않는다.
                EditorViewportPreviewInternal::PreviewMaterialState& heldMaterial = getEditorState<EditorViewportPreviewInternal::PreviewMaterialState>();
                const bool                                           bAlreadyHeld = ( heldMaterial._acquiredPath == assetPath );
                Material*                                            pCached      = pResources->getAssetCache<MaterialCache>().acquire( assetPath, nullptr );
                if ( pCached != nullptr )
                {
                    if ( bAlreadyHeld )
                    {
                        // 방금 늘린 참조는 곧바로 되돌린다. 프리뷰가 가진 참조는 언제나 하나다.
                        pResources->getAssetCache<MaterialCache>().release( assetPath );
                    }
                    else
                    {
                        previousAcquiredPath       = heldMaterial._acquiredPath;
                        heldMaterial._acquiredPath = string{ assetPath };
                    }

                    if ( pMaterial != nullptr )
                    {
                        if ( pCached->loadFromXML( pMaterial->saveToString() ) == false )
                            SW_LOG_WARNING( "Material preview could not copy the edited material - the preview shows the cached one" );
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
        // 메시가 아직 그 포인터를 보관하고 있을 수 있다.
        if ( previousAcquiredPath.empty() == false )
            editor::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().release( previousAcquiredPath ); // 필수 - 놓지 못하면 참조가 샌다

        if ( pPrimary == nullptr )
            return;

        SpriteComponent* pSprite = pPrimary->getComponent<SpriteComponent>();
        if ( pSprite != nullptr && assetPath.empty() == false )
            pSprite->setMaterialPath( assetPath );
    }
} // namespace sw::editor
