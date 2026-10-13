#include "pch.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorAssetTypeActions.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorAssetCommands" );

    bool EditorAssetCommands::openPath( string_view relativePath )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return false;

        // 전용 도구 패널을 먼저 찾는다(종류 테이블의 패널 제목). 패널이 없는 종류는 그 종류의 동작이 연다(씬).
        const string_view panelTitle = EditorAssetTypeRegistry::findPanelTitleForPath( relativePath );
        if ( panelTitle.empty() == false )
        {
            pContext->getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ relativePath }.c_str() );
            pContext->getSubsystem<EditorWorkspace>().requestOpenPanel( string{ panelTitle }.c_str() );
            return true;
        }

        const IEditorAssetTypeActions* pActions = EditorAssetTypeActionsRegistry::findActionsForPath( relativePath );
        return pActions != nullptr && pActions->open( relativePath );
    }

    void EditorAssetCommands::focusPath( string_view relativePath )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        pContext->getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ relativePath }.c_str() );
    }

    GameObject* EditorAssetCommands::spawnPrefab( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent,
                                                  const utf8* pUndoLabel )
    {
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
            return nullptr;
        GameObject* pSpawned = spawnPrefabWithoutUndo( pManager, pPath, pParent );
        if ( pSpawned == nullptr )
            return nullptr;

        const utf8* pLabel = ( pUndoLabel != nullptr ) ? pUndoLabel : "Spawn Prefab";
        EditorTransaction::recordCreation( pSpawned, pLabel );
        EditorSceneCommands::select( pSpawned, SelectionMode::Replace );
        return pSpawned;
    }

    GameObject* EditorAssetCommands::spawnPrefabWithoutUndo( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent )
    {
        if ( pManager == nullptr || StringUtil::isNullOrEmpty( pPath ) )
            return nullptr;

        if ( EditorAssetTypeRegistry::matches( EditorAssetType::Prefab, pPath ) == false )
        {
            SW_LOG_TRACE( "Not a prefab path: %#", pPath );
            return nullptr;
        }

        // `getService<T>()` 는 nullptr 을 반환할 수 있다. 역참조하기 전에 확인한다.
        AssetManager* pResources = editor::getService<AssetManager>();
        if ( pResources == nullptr )
        {
            SW_LOG_WARNING( "AssetManager 서비스가 없어 프리팹을 스폰할 수 없습니다: %#", pPath );
            return nullptr;
        }

        GameObject* pSpawned = pResources->getAssetCache<PrefabCache>().spawn( pManager, pPath );
        if ( pSpawned == nullptr )
        {
            SW_LOG_WARNING( "Failed to spawn prefab: %#", pPath );
            return nullptr;
        }

        if ( pParent != nullptr && pSpawned->attachToParent( pParent ) == false )
            SW_LOG_WARNING( "Spawned prefab could not be put under '%#' - it stays at the root", pParent->getName().c_str() );

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getSubsystem<EditorWorkspace>().setGameObjectPrefabPath( pSpawned->getObjectID(), pPath );

        SW_LOG_TRACE( "Spawned prefab from %#", pPath );
        return pSpawned;
    }

    GameObject* EditorAssetCommands::spawnSprite( GameObjectManager* pManager, const utf8* pPath, const float3& worldPos )
    {
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
            return nullptr;
        if ( pManager == nullptr || StringUtil::isNullOrEmpty( pPath ) )
            return nullptr;

        string filename = FileUtil::getFileNamePart( pPath );
        filename        = FileUtil::removeExtension( filename );

        GameObject* pSpawned = pManager->createGameObject( hashed_string( filename.c_str() ) );
        if ( pSpawned == nullptr )
            return nullptr;

        SceneComponent* pSc = pSpawned->addComponent<SceneComponent>();
        if ( pSc != nullptr )
            pSc->setLocalPosition( worldPos );

        SpriteComponent* pSprite = pSpawned->addComponent<SpriteComponent>();
        if ( pSprite != nullptr )
        {
            // 드롭한 것은 콘텐츠 브라우저의 리소스 id 다. 이것을 프로젝트 루트 기준으로 다시 `makeRelativePath` 하면 경로가 깨져
            // 스프라이트가 텍스처를 찾지 못하고 흰 사각형으로 그려지므로 주의한다.
            const string textureID = ResourcePaths::toResourceID( pPath );
            if ( textureID.empty() )
                SW_LOG_WARNING( "Sprite drop: '%#' is not inside the resource tree - the sprite has no texture", pPath );
            pSprite->setTextureName( textureID );
            // 편집 모드에서는 시작(onBeginPlay)이 없다. 드롭한 위치에서 바로 그려지도록 렌더 에셋을 해석한다.
            pSprite->resolveRenderAssets();
        }

        EditorTransaction::recordCreation( pSpawned, "Spawn Sprite in Viewport" );
        EditorSceneCommands::select( pSpawned, SelectionMode::Replace );
        return pSpawned;
    }

    void EditorAssetCommands::dropAt( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos )
    {
        if ( pManager == nullptr || pPath == nullptr )
            return;

        // 스폰과 로드는 종류의 동작(프리팹, 텍스처, 씬)이 한다. 처리하지 않은 것은 연다.
        const IEditorAssetTypeActions* pActions = EditorAssetTypeActionsRegistry::findActionsForPath( pPath );
        if ( pActions != nullptr && pActions->dropInViewport( pManager, pPath, spawnPos ) )
            return;
        (void)openPath( pPath ); // 실패는 openPath 가 알린다
    }
} // namespace sw::editor
