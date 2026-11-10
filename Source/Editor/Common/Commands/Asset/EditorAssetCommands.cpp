#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"
#include "Editor/Common/Workspace/Asset/EditorPlacementPose.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/2D/SpriteComponent.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw::editor
{
    bool EditorAssetCommands::openPath( string_view relativePath )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        // 전용 도구 패널을 먼저 찾는다(종류 등록에 패널 제목이 있는 종류). 패널이 없는 종류는 그 종류의 동작이 연다(씬).
        const EditorAssetType panelKind = EditorAssetTypeRegistry::findPanelKindForPath( relativePath );
        if ( panelKind.isUnknown() == false )
        {
            editorContext.getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ relativePath }.c_str() );
            editorContext.getSubsystem<EditorWorkspace>().requestOpenDocumentPanel( panelKind );
            return true;
        }

        // 동작이 열지 않으면(썸네일만 둔 오디오 — `IEditorAssetTypeActions::open` 기본) 아래 포커스 길로 간다. 예전에는 그 false 를 그대로 돌려줘 오디오 그래프(`*.audiograph.xml`)가
        // 콘텐츠 브라우저 · `asset.openScratch` 로 열리지 않았다 — 문서 패널은 종류의 동작이 아니라 포커스로 문서를 받는다.
        const IEditorAssetTypeActions* pActions = EditorAssetTypeRegistry::findActionsForPath( relativePath );
        if ( pActions != nullptr && pActions->open( relativePath ) )
            return true;
        // 전용 패널도 여는 동작도 없는 종류(Data 의 `*.vfx.xml` 등)는 그 종류의 문서 패널이 포커스 경로로 연다(`EditorDocumentPanel::getMatchingFocusedPath` —
        // 콘텐츠 브라우저 더블클릭이 고르는 것과 같은 길). 예전에는 포커스를 세우지 않아 시나리오 명령(asset.openScratch)이 "열 곳 없음" 으로 졌다.
        // 이 층(명령)은 패널 관리자를 볼 수 없다 — 아는 종류면 포커스를 세우고, 받는 문서 패널이 그 경로를 연다(없으면 선택만 남는다 — 브라우저와 같다).
        if ( EditorAssetTypeRegistry::findKind( relativePath ).isUnknown() )
            return false;
        editorContext.getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ relativePath }.c_str() );
        return true;
    }

    void EditorAssetCommands::focusPath( string_view relativePath )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        editorContext.getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ relativePath }.c_str() );
    }

    GameObject* EditorAssetCommands::spawnPrefab( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent,
                                                  const utf8* pUndoLabel )
    {
        if ( EditorPlaySession::refuseSceneEdit( "Spawn prefab" ) )
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

        if ( EditorAssetTypeRegistry::matches( EditorAssetType{ PrefabAsset::kAssetType }, pPath ) == false )
        {
            SW_LOG_TRACE_ID( "EditorAssetCommands.NotPrefabPath", "Not a prefab path: %#", pPath );
            return nullptr;
        }

        GameObject* pSpawned = editor::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().spawn( pManager, pPath );
        if ( pSpawned == nullptr )
        {
            SW_LOG_WARNING_ID( "EditorAssetCommands.FailedSpawnPrefab", "Failed to spawn prefab: %#", pPath );
            return nullptr;
        }

        if ( pParent != nullptr && pSpawned->attachToParent( pParent ) == false )
            SW_LOG_WARNING_ID( "EditorAssetCommands.SpawnedPrefabCouldNot", "Spawned prefab could not be put under '%#' - it stays at the root", pParent->getName().c_str() );

        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        editorContext.getSubsystem<EditorWorkspace>().setPlacedPrefabPath( *pSpawned, pPath );

        SW_LOG_TRACE_ID( "EditorAssetCommands.SpawnedPrefab", "Spawned prefab from %#", pPath );
        return pSpawned;
    }

    GameObject* EditorAssetCommands::spawnSprite( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose )
    {
        if ( EditorPlaySession::refuseSceneEdit( "Spawn sprite" ) )
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
        {
            pSc->setLocalPosition( pose._position );
            pSc->setLocalRotation( pSc->getLocalRotation() + pose.makeRotationOffset() ); // 놓기 전에 고른 회전 — 기록보다 먼저 입혀 다시 실행도 같은 자세다
        }

        SpriteComponent* pSprite = pSpawned->addComponent<SpriteComponent>();
        if ( pSprite != nullptr )
        {
            // 드롭한 것은 콘텐츠 브라우저의 리소스 id 다. 이것을 프로젝트 루트 기준으로 다시 `makeRelativePath` 하면 경로가 깨져
            // 스프라이트가 텍스처를 찾지 못하고 흰 사각형으로 그려지므로 주의한다.
            const string textureID = AssetPaths::normalizeAssetID( pPath );
            if ( textureID.empty() )
                SW_LOG_WARNING_ID( "EditorAssetCommands.SpriteDropNotInside", "Sprite drop: '%#' is not inside the resource tree - the sprite has no texture", pPath );
            pSprite->setTextureName( textureID );
            // 편집 모드에서는 시작(onBeginPlay)이 없다. 드롭한 위치에서 바로 그려지도록 렌더 에셋을 해석한다.
            pSprite->resolveRenderAssets();
        }

        EditorTransaction::recordCreation( pSpawned, "Spawn Sprite in Viewport" );
        EditorSceneCommands::select( pSpawned, SelectionMode::Replace );
        return pSpawned;
    }

    GameObject* EditorAssetCommands::spawnMesh( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose )
    {
        if ( EditorPlaySession::refuseSceneEdit( "Spawn mesh" ) )
            return nullptr;
        if ( pManager == nullptr || StringUtil::isNullOrEmpty( pPath ) )
            return nullptr;
        // 메시 id 는 리소스 id 로 저장한다(씬 파일이 다른 PC 에서도 읽힌다). 리소스 트리 밖이면 만들지 않는다.
        const string meshID = AssetPaths::normalizeAssetID( pPath ); // 드롭한 것은 콘텐츠 브라우저의 리소스 id 다
        if ( meshID.empty() )
        {
            SW_LOG_WARNING_ID( "EditorAssetCommands.MeshDropNotInside", "Mesh drop: '%#' is not inside the resource tree - nothing was placed", pPath );
            return nullptr;
        }
        const string name     = FileUtil::removeExtension( FileUtil::getFileNamePart( pPath ) );
        GameObject*  pSpawned = pManager->createGameObject( hashed_string( name.c_str() ) );
        if ( pSpawned == nullptr )
            return nullptr;
        MeshComponent* pMesh = pSpawned->addComponent<MeshComponent>();
        if ( pMesh != nullptr )
        {
            pMesh->setLocalPosition( pose._position );
            pMesh->setLocalRotation( pMesh->getLocalRotation() + pose.makeRotationOffset() );
            pMesh->setMeshID( meshID );
        }
        EditorTransaction::recordCreation( pSpawned, "Spawn Mesh in Viewport" );
        EditorSceneCommands::select( pSpawned, SelectionMode::Replace );
        return pSpawned;
    }

    GameObject* EditorAssetCommands::spawnPrefabAt( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose )
    {
        if ( EditorPlaySession::refuseSceneEdit( "Spawn prefab" ) )
            return nullptr;
        GameObject* pSpawned = spawnPrefabWithoutUndo( pManager, pPath, nullptr );
        if ( pSpawned == nullptr )
            return nullptr;
        SceneComponent* pSceneComponent = pSpawned->getPrimarySceneComponent();
        if ( pSceneComponent != nullptr )
        {
            pSceneComponent->setLocalPosition( pose._position );
            pSceneComponent->setLocalRotation( pSceneComponent->getLocalRotation() + pose.makeRotationOffset() ); // 프리팹의 회전에 더한다
        }
        EditorTransaction::recordCreation( pSpawned, "Spawn Prefab in Viewport" );
        EditorSceneCommands::select( pSpawned, SelectionMode::Replace );
        return pSpawned;
    }

    bool EditorAssetCommands::canPlaceInViewport( string_view relativePath )
    {
        const IEditorAssetTypeActions* pActions = EditorAssetTypeRegistry::findActionsForPath( relativePath );
        return pActions != nullptr && pActions->canPlaceInViewport();
    }

    void EditorAssetCommands::placeInViewport( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose )
    {
        if ( pManager == nullptr || StringUtil::isNullOrEmpty( pPath ) )
            return;
        // 만드는 일은 종류의 동작(프리팹 · 메시 · 텍스처)이 한다. 놓을 수 없는 종류를 여기로 보내는 것은 부른 쪽의 잘못이다(그 종류는 연다).
        const IEditorAssetTypeActions* pActions = EditorAssetTypeRegistry::findActionsForPath( pPath );
        if ( pActions == nullptr || pActions->canPlaceInViewport() == false )
        {
            SW_LOG_ERROR_ID( "EditorAssetCommands.NotPlaceable", "'%#' cannot be placed in the viewport (its type opens instead) - nothing was placed", pPath );
            return;
        }
        pActions->placeInViewport( pManager, pPath, pose );
    }
} // namespace sw::editor
