#include "pch.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorWorkspace" );

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
        _pendingOpenPanel = EditorPanelOpenRequest{ string{ panelID }, EditorAssetType::Unknown };
    }

    void EditorWorkspace::requestOpenDocumentPanel( EditorAssetType kind )
    {
        _pendingOpenPanel = EditorPanelOpenRequest{ string{}, kind };
    }

    bool EditorWorkspace::consumeOpenPanel( EditorPanelOpenRequest& outRequest )
    {
        if ( _pendingOpenPanel._panelID.empty() && _pendingOpenPanel._documentKind == EditorAssetType::Unknown )
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
            SW_LOG_ERROR( "Prefab path of '%#' is not recorded: its scene is neither the active scene nor an open preview scene", object.getName().c_str() );
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
            SW_LOG_ERROR( "The scene was edited while scene edits are held (a light probe bake) - the edit path did not ask EditorPlaySession::areSceneEditsAllowed" );
            EditorPlaySession::noteSceneEditWhileHeld();
        }
        _bSceneDirty = SW_TRUE;
    }
} // namespace sw::editor
