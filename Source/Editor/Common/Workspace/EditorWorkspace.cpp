#include "pch.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Core/Container/StringUtil.h"

#include "Editor/Common/Commands/EditorTransformCommands.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw::editor
{
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
        , _pendingOpenPanelTitle{}
        , _pendingScenePath{}
        , _pendingSceneActionPath{}
        , _emptyString{}
        , _copiedComponentXml{}
        , _copiedComponentTypeName{}
        , _pendingSceneMutex{}
        , _arrCameraBookmark{}
        , _listPrefabIsolationFrame{}
        , _gizmoOperation{ 0 }
        , _pendingSceneAction{ EditorPendingSceneAction::None }
        , _bGizmoLocalSpace{ SW_TRUE }
        , _bSceneDirty{ SW_FALSE }
        , _bPrefabIsolation{ SW_FALSE }
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
        if ( pObj != nullptr && pComp != nullptr && pComp->getOwner() == pObj )
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

    void EditorWorkspace::requestOpenPanel( const utf8* pTitle )
    {
        _pendingOpenPanelTitle = ( pTitle != nullptr ) ? pTitle : "";
    }

    bool EditorWorkspace::consumeOpenPanel( string& outTitle )
    {
        if ( _pendingOpenPanelTitle.empty() )
            return false;
        outTitle = _pendingOpenPanelTitle;
        _pendingOpenPanelTitle.clear();
        return true;
    }

    void EditorWorkspace::requestLoadScene( string_view path )
    {
        std::scoped_lock<mutex> lock{ _pendingSceneMutex };
        _pendingScenePath = path;
    }

    bool EditorWorkspace::consumeLoadScene( string& outPath )
    {
        std::scoped_lock<mutex> lock{ _pendingSceneMutex };
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

    void EditorWorkspace::setGameObjectPrefabPath( uint64 objectID, string_view prefabPath )
    {
        Scene* pScene = editor::getActiveScene();
        if ( objectID == 0 || pScene == nullptr )
            return;
        pScene->setEntityPrefabPath( objectID, prefabPath );
    }

    const string& EditorWorkspace::getGameObjectPrefabPath( uint64 objectID ) const
    {
        const Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr )
            return _emptyString;
        return pScene->getEntityPrefabPath( objectID );
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

    void EditorWorkspace::clearCameraBookmark( uint32 slot )
    {
        if ( slot < _arrCameraBookmark.size() )
        {
            _arrCameraBookmark[slot]         = CameraBookmark{};
            _arrCameraBookmark[slot]._bValid = false;
        }
    }

    void EditorWorkspace::copyComponent( const Component* pComp )
    {
        if ( pComp == nullptr || pComp->getTypeInfo() == nullptr )
            return;

        _copiedComponentTypeName = pComp->getTypeName().c_str();
        _copiedComponentBytes.clear();
        BinarySerializer::serialize( pComp, *pComp->getTypeInfo(), _copiedComponentBytes );
        _copiedComponentXml = XmlSerializer::serialize( pComp, *pComp->getTypeInfo() );
    }

    bool EditorWorkspace::hasCopiedComponent() const
    {
        return _copiedComponentBytes.empty() == false || _copiedComponentXml.empty() == false;
    }

    bool EditorWorkspace::pasteComponentValues( Component* pTargetComp )
    {
        return EditorTransformCommands::pasteComponentValues( pTargetComp, _copiedComponentBytes, _copiedComponentXml );
    }

    Component* EditorWorkspace::pasteComponentAsNew( GameObject* pTargetObj )
    {
        return EditorTransformCommands::pasteComponentAsNew( pTargetObj, _copiedComponentTypeName, _copiedComponentBytes, _copiedComponentXml );
    }

    bool EditorWorkspace::saveComponentPreset( const Component* pComp, string_view presetName )
    {
        return EditorTransformCommands::saveComponentPreset( pComp, presetName );
    }

    bool EditorWorkspace::loadComponentPreset( Component* pComp, string_view presetFilePath )
    {
        return EditorTransformCommands::loadComponentPreset( pComp, presetFilePath );
    }

    const string& EditorWorkspace::getPrefabIsolationPrefabPath() const
    {
        if ( _listPrefabIsolationFrame.empty() )
            return _emptyString;
        return _listPrefabIsolationFrame.back()._prefabPath;
    }

    uint64 EditorWorkspace::getPrefabIsolationRootID() const
    {
        if ( _listPrefabIsolationFrame.empty() )
            return 0;
        return _listPrefabIsolationFrame.back()._rootObjectID;
    }

    const PrefabIsolationFrame* EditorWorkspace::getPrefabIsolationFrame() const
    {
        if ( _listPrefabIsolationFrame.empty() )
            return nullptr;
        return &_listPrefabIsolationFrame.back();
    }

    void EditorWorkspace::pushPrefabIsolation( PrefabIsolationFrame frame )
    {
        _listPrefabIsolationFrame.push_back( std::move( frame ) );
        _bPrefabIsolation = SW_TRUE;
    }

    bool EditorWorkspace::popPrefabIsolation()
    {
        if ( _listPrefabIsolationFrame.empty() )
            return true;
        _listPrefabIsolationFrame.pop_back();
        if ( _listPrefabIsolationFrame.empty() )
        {
            _bPrefabIsolation = SW_FALSE;
            return true;
        }
        return false;
    }

    void EditorWorkspace::clearPrefabIsolation()
    {
        _listPrefabIsolationFrame.clear();
        _bPrefabIsolation = SW_FALSE;
    }
} // namespace sw::editor
