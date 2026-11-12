#include "pch.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    span<const utf8* const> EditorWorkspace::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EditorSelection::kSubsystemName };
        return kArrDependency;
    }

    // ------------------------------------------------------------------------------
    // 생성자
    // ------------------------------------------------------------------------------
    EditorWorkspace::EditorWorkspace( EditorSelection* pEditorSelection )
        : _pEditorSelection{ pEditorSelection }
        , _selectedComponentID{ 0 }
        , _observedSceneGeneration{ 0 }
        , _scrollToComponentID{ 0 }
        , _selectedComponentKey{}
        , _focusedAssetPath{}
        , _assetsMoved{}
        , _pendingOpenPanel{}
        , _pendingScenePath{}
        , _pendingSceneActionPath{}
        , _copiedComponentXML{}
        , _copiedComponentTypeName{}
        , _pendingSceneMutex{}
        , _arrCameraBookmark{}
        , _listLockedObjectID{}
        , _gizmoOperation{ 0 }
        , _pendingSceneAction{ EditorPendingSceneAction::None }
        , _sceneEditSerial{ 0 }
        , _bGizmoLocalSpace{ SW_TRUE }
        , _bSceneDirty{ SW_FALSE }
        , _reservedWorkspace{ 0 }
    {
    }

    // ------------------------------------------------------------------------------
    // 멤버 함수
    // ------------------------------------------------------------------------------
    uint64 EditorWorkspace::getSelectedObjectID() const
    {
        if ( _pEditorSelection != nullptr )
            return _pEditorSelection->getPrimaryObjectID();
        return 0;
    }

    GameObject* EditorWorkspace::getSelectedObject() const
    {
        if ( _pEditorSelection != nullptr )
            return _pEditorSelection->getPrimaryObject();
        return nullptr;
    }

    string EditorWorkspace::getSelectedObjectName() const
    {
        const GameObject* pObj = getSelectedObject();
        if ( pObj != nullptr )
            return string{ pObj->getName().c_str() };
        return {};
    }

    void EditorWorkspace::clearSelection()
    {
        if ( _pEditorSelection != nullptr )
            _pEditorSelection->clearAll();
        _selectedComponentID = 0;
        _selectedComponentKey.clear();
    }

    void EditorWorkspace::selectGameObject( GameObject* pObj, SelectionMode mode )
    {
        if ( _pEditorSelection != nullptr )
            _pEditorSelection->selectObject( pObj, mode );
        _selectedComponentID = 0;
        _selectedComponentKey.clear();
    }

    void EditorWorkspace::selectGameObjects( const vector<GameObject*>& listObj, SelectionMode mode )
    {
        if ( _pEditorSelection != nullptr )
            _pEditorSelection->selectObjects( listObj, mode );
        _selectedComponentID = 0;
        _selectedComponentKey.clear();
    }

    void EditorWorkspace::selectComponent( GameObject* pObj, Component* pComp )
    {
        if ( _pEditorSelection != nullptr )
            _pEditorSelection->selectObject( pObj, SelectionMode::Replace );

        if ( pComp != nullptr )
            _selectedComponentID = pComp->getComponentID();
        else
            _selectedComponentID = 0;

        // 씬 파일의 부착 대상과 같은 키(`ComponentStableKey`)다. 소유자가 다른 컴포넌트는 되찾을 수 없으니 비운다.
        const bool bComponentBelongsToObject = pObj != nullptr && pComp != nullptr && pComp->getOwner() == pObj;
        if ( bComponentBelongsToObject )
            _selectedComponentKey = ComponentStableKey::makeKey( pComp );
        else
            _selectedComponentKey.clear();

        _scrollToComponentID = _selectedComponentID;
    }

    void EditorWorkspace::remapSelectionByObjectName( GameObjectManager* pGameObjectManager )
    {
        if ( pGameObjectManager == nullptr )
            return;

        const string name = getSelectedObjectName();
        if ( name.empty() )
        {
            _selectedComponentID = 0;
            _selectedComponentKey.clear();
            return;
        }

        GameObject* pObj = pGameObjectManager->findGameObjectByName( hashed_string( name.c_str() ) );
        if ( pObj == nullptr )
        {
            clearSelection();
            return;
        }

        if ( _pEditorSelection != nullptr )
            _pEditorSelection->selectObject( pObj, SelectionMode::Replace );

        if ( _selectedComponentKey.empty() )
        {
            _selectedComponentID = 0;
            return;
        }

        Component* pResolved = ComponentStableKey::findComponent( pObj, _selectedComponentKey );
        if ( pResolved != nullptr )
            _selectedComponentID = pResolved->getComponentID();
        else
            _selectedComponentID = 0;
    }

    void EditorWorkspace::notifyAssetsMoved( const EditorAssetMoveNotice& notice )
    {
        const vector<string>& listOldID = notice._listOldID;
        const vector<string>& listNewID = notice._listNewID;
        SW_ASSERT( listOldID.size() == listNewID.size() );
        // 옛 id → 새 id 를 한 번 색인하고 받는 쪽마다 찾기만 한다(옮긴 수 × 받는 쪽 칸 수로 훑지 않는다 — 언리얼 리다이렉터 고치기도 옛 경로 → 새 경로 맵이다).
        unordered_map<string, string> mapOldToNew;
        mapOldToNew.reserve( listOldID.size() );
        for ( size_t index = 0; index < listOldID.size(); ++index )
        {
            mapOldToNew.insert_or_assign( listOldID[index], listNewID[index] );
        }

        const auto itFocused = mapOldToNew.find( _focusedAssetPath );
        if ( itFocused != mapOldToNew.end() )
            _focusedAssetPath = itFocused->second;
        Scene* const pScene = editor::getActiveScene();
        if ( pScene != nullptr )
        {
            const auto itScene = mapOldToNew.find( pScene->getSourcePath() );
            if ( itScene != mapOldToNew.end() )
                pScene->setSourcePath( itScene->second ); // 다음 저장(Ctrl+S)이 새 자리에 쓴다 — 옛 자리에 파일을 다시 만들지 않는다
        }
        if ( _pEditorSelection != nullptr )
            _pEditorSelection->remapAssets( mapOldToNew ); // 고른 에셋 경로도 글이다 — 옛 id 로 남으면 인스펙터 · 명령이 없는 파일을 가리킨다
        for ( EditorSelection* const pPreviewSelection : editor::getPreviewSelections() )
            pPreviewSelection->remapAssets( mapOldToNew ); // 미리보기 창의 선택도 같은 글이다(창마다 선택이 따로다)
        _assetsMoved.broadcast( notice );
    }

    void EditorWorkspace::setFocusedAssetPath( const utf8* pPath )
    {
        _focusedAssetPath = ( pPath != nullptr ) ? pPath : "";
        if ( StringUtil::isNullOrEmpty( pPath ) == false )
        {
            if ( _pEditorSelection != nullptr )
                _pEditorSelection->selectAsset( pPath, SelectionMode::Replace );
        }
    }

    void EditorWorkspace::requestOpenPanelByID( string_view panelID )
    {
        _pendingOpenPanel = EditorPanelOpenRequest{ string{ panelID }, EditorAssetType{} };
    }

    void EditorWorkspace::requestFloatPanelByID( string_view panelID )
    {
        _pendingOpenPanel = EditorPanelOpenRequest{ string{ panelID }, EditorAssetType{}, true };
    }

    void EditorWorkspace::requestOpenDocumentPanel( const EditorAssetType& kind )
    {
        _pendingOpenPanel = EditorPanelOpenRequest{ string{}, kind };
    }

    bool EditorWorkspace::consumeOpenPanel( EditorPanelOpenRequest& outRequest )
    {
        if ( _pendingOpenPanel._panelID.empty() && _pendingOpenPanel._documentKind.isUnknown() )
            return false;
        outRequest        = std::move( _pendingOpenPanel );
        _pendingOpenPanel = EditorPanelOpenRequest{};
        return true;
    }

    void EditorWorkspace::requestLoadScene( string_view path )
    {
        ScopedLock<mutex> lock{ _pendingSceneMutex };
        _pendingScenePath = path;
    }

    bool EditorWorkspace::consumeLoadScene( string& outPath )
    {
        ScopedLock<mutex> lock{ _pendingSceneMutex };
        if ( _pendingScenePath.empty() )
            return false;
        outPath = _pendingScenePath;
        _pendingScenePath.clear();
        return true;
    }

    void EditorWorkspace::setPendingSceneAction( EditorPendingSceneAction action, string_view loadPath )
    {
        _pendingSceneAction     = action;
        _pendingSceneActionPath = string{ loadPath };
    }

    void EditorWorkspace::clearPendingSceneAction()
    {
        _pendingSceneAction = EditorPendingSceneAction::None;
        _pendingSceneActionPath.clear();
    }

    void EditorWorkspace::setPlacedPrefabPath( const GameObject& object, string_view prefabPath )
    {
        Scene* pScene = editor::findSceneOf( object.getManager() );
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR_ID( "EditorWorkspace.PrefabPathNotRecorded", "Prefab path of '%#' is not recorded: its scene is neither the active scene nor an open preview scene", object.getName().c_str() );
            return;
        }
        pScene->setEntityPrefabPath( object.getObjectID(), prefabPath );
    }

    string_view EditorWorkspace::getPlacedPrefabPath( const GameObject& object ) const
    {
        const Scene* pScene = editor::findSceneOf( object.getManager() );
        if ( pScene == nullptr )
            return {};
        return pScene->getEntityPrefabPath( object.getObjectID() );
    }

    string_view EditorWorkspace::findPrefabPath( const GameObject* pObject ) const
    {
        if ( pObject == nullptr )
            return {};
        if ( isNestedPrefabPart( pObject ) )
            return pObject->getManager()->getSubsystem<PrefabInstanceRegistry>().findNestedPart( pObject->getObjectID() )->_prefabPath; // 붙은 부품이면 기록이 있다
        return getPlacedPrefabPath( *pObject );
    }

    bool EditorWorkspace::isNestedPrefabPart( const GameObject* pObject ) const
    {
        GameObjectManager* pManager = pObject != nullptr ? pObject->getManager() : nullptr;
        return pManager != nullptr && pManager->getSubsystem<PrefabInstanceRegistry>().isAttachedNestedPart( pObject );
    }

    void EditorWorkspace::setCameraBookmark( uint32 slot, const CameraBookmark& bookmark )
    {
        if ( slot < _arrCameraBookmark.size() )
        {
            _arrCameraBookmark[slot]         = bookmark;
            _arrCameraBookmark[slot]._bValid = true;
        }
    }

    const CameraBookmark* EditorWorkspace::getCameraBookmark( uint32 slot ) const
    {
        if ( slot < _arrCameraBookmark.size() && _arrCameraBookmark[slot]._bValid == true )
            return &_arrCameraBookmark[slot];
        return nullptr;
    }

    bool EditorWorkspace::hasCameraBookmark( uint32 slot ) const
    {
        return slot < _arrCameraBookmark.size() && _arrCameraBookmark[slot]._bValid == true;
    }

    void EditorWorkspace::copyComponent( const Component* pComp )
    {
        if ( pComp == nullptr || pComp->getTypeInfo() == nullptr )
            return;

        _copiedComponentTypeName = pComp->getTypeName().c_str();
        _copiedComponentXML      = XMLSerializer::serialize( pComp, *pComp->getTypeInfo() );
    }

    bool EditorWorkspace::hasCopiedComponent() const
    {
        return _copiedComponentXML.empty() == false;
    }

    bool EditorWorkspace::isObjectLocked( uint64 objectID ) const
    {
        return VectorUtil::contains( _listLockedObjectID, objectID );
    }

    void EditorWorkspace::setObjectLocked( uint64 objectID, bool bLocked )
    {
        for ( size_t index = 0; index < _listLockedObjectID.size(); ++index )
        {
            if ( _listLockedObjectID[index] != objectID )
                continue;
            if ( bLocked == false )
                _listLockedObjectID.erase( _listLockedObjectID.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
        if ( bLocked )
            _listLockedObjectID.push_back( objectID );
    }

    void EditorWorkspace::markSceneDirty()
    {
        if ( EditorPlaySession::areSceneEditsHeld() )
        {
            SW_LOG_ERROR_ID( "EditorWorkspace.SceneEditedWhileScene", "The scene was edited while scene edits are held (a light probe bake) - the edit path did not ask EditorPlaySession::areSceneEditsAllowed" );
            EditorPlaySession::recordSceneEditWhileHeld();
        }
        _bSceneDirty = SW_TRUE;
        ++_sceneEditSerial;
    }
} // namespace sw::editor
