#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorAssetTypeActions.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
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

        // 전용 도구 패널을 먼저 찾는다(종류 테이블에 패널 제목이 있는 종류). 패널이 없는 종류는 그 종류의 동작이 연다(씬).
        const EditorAssetType panelKind = EditorAssetTypeRegistry::findPanelKindForPath( relativePath );
        if ( panelKind != EditorAssetType::Unknown )
        {
            pContext->getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ relativePath }.c_str() );
            pContext->getSubsystem<EditorWorkspace>().requestOpenDocumentPanel( panelKind );
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

        // `findSubsystem<T>()` 는 nullptr 을 반환할 수 있다. 역참조하기 전에 확인한다.
        AssetManager* pResources = editor::findSubsystem<AssetManager>(); // 필수 서비스: 없으면 버그
        if ( pResources == nullptr )
        {
            SW_LOG_ERROR( "Cannot spawn the prefab because there is no AssetManager service: %#", pPath );
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
            pContext->getSubsystem<EditorWorkspace>().setPlacedPrefabPath( *pSpawned, pPath );

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

    GameObject* EditorAssetCommands::spawnMesh( GameObjectManager* pManager, const utf8* pPath, const float3& worldPos )
    {
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
            return nullptr;
        if ( pManager == nullptr || StringUtil::isNullOrEmpty( pPath ) )
            return nullptr;
        // 메시 id 는 리소스 id 로 저장한다(씬 파일이 다른 PC 에서도 읽힌다). 리소스 트리 밖이면 만들지 않는다.
        const string meshID = ResourcePaths::toResourceID( pPath );
        if ( meshID.empty() )
        {
            SW_LOG_WARNING( "Mesh drop: '%#' is not inside the resource tree - nothing was placed", pPath );
            return nullptr;
        }
        const string name     = FileUtil::removeExtension( FileUtil::getFileNamePart( pPath ) );
        GameObject*  pSpawned = pManager->createGameObject( hashed_string( name.c_str() ) );
        if ( pSpawned == nullptr )
            return nullptr;
        MeshComponent* pMesh = pSpawned->addComponent<MeshComponent>();
        if ( pMesh != nullptr )
        {
            pMesh->setLocalPosition( worldPos );
            pMesh->setMeshID( meshID );
        }
        EditorTransaction::recordCreation( pSpawned, "Spawn Mesh in Viewport" );
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
