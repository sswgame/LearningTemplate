#include "pch.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Editor/Common/Commands/EditorTransformCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw::editor
{
    namespace
    {
        struct EditorWorkspaceInternal
        {
            static string componentTypeBaseName( const Component* pComp )
            {
                if ( pComp == nullptr )
                    return "Component";

                const TypeInfo* pTypeInfo = pComp->getTypeInfo();
                if ( pTypeInfo != nullptr )
                {
                    if ( pTypeInfo->_fullyQualifiedName.empty() == false )
                        return pTypeInfo->_fullyQualifiedName.c_str();
                }

                if ( pComp->getComponentName().empty() == false )
                    return pComp->getComponentName().c_str();

                return "Component";
            }

            static string componentBaseKey( const Component* pComp )
            {
                if ( pComp != nullptr && pComp->getComponentName().empty() == false )
                    return pComp->getComponentName().c_str();

                return componentTypeBaseName( pComp );
            }

            static string makeStableComponentKey( const Component* pComp, int32 occurrence )
            {
                if ( occurrence <= 0 )
                    return componentBaseKey( pComp );

                return componentBaseKey( pComp ) + "#" + to_string( occurrence );
            }

            static string computeStableComponentKey( const GameObject* pGameObject, const Component* pTarget )
            {
                if ( pGameObject == nullptr || pTarget == nullptr )
                    return {};

                unordered_map<string, int32> mapOccurrence;
                for ( Component* pComp : pGameObject->getAllComponents() )
                {
                    if ( pComp == nullptr )
                        continue;

                    const string base = componentBaseKey( pComp );
                    const int32  occ  = mapOccurrence[base]++;
                    if ( pComp == pTarget )
                        return makeStableComponentKey( pComp, occ );
                }
                return {};
            }

            static Component* findComponentByStableKey( GameObject* pGameObject, string_view key )
            {
                if ( pGameObject == nullptr || key.empty() )
                    return nullptr;

                unordered_map<string, int32> mapOccurrence;
                for ( Component* pComp : pGameObject->getAllComponents() )
                {
                    if ( pComp == nullptr )
                        continue;

                    const string base      = componentBaseKey( pComp );
                    const int32  occ       = mapOccurrence[base]++;
                    const string stableKey = makeStableComponentKey( pComp, occ );
                    if ( stableKey == key )
                        return pComp;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // Constructor
    // ------------------------------------------------------------------------------
    EditorWorkspace::EditorWorkspace()
        : _selectedComponentId{ 0 }
        , _observedSceneGeneration{ 0 }
        , _scrollToComponentId{ 0 }
        , _scrollToObjectId{ 0 }
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
        , _mapGameObjectToPrefab{}
        , _mapObjectIdToGuid{}
        , _mapGuidToObjectId{}
        , _inspectMode{ InspectMode::GameObject }
        , _pendingSceneAction{ EditorPendingSceneAction::None }
        , _gizmoOperation{ 0 }
        , _bGizmoLocalSpace{ SW_TRUE }
        , _bBoneHierarchyPopupOpen{ SW_FALSE }
        , _bSceneDirty{ SW_FALSE }
        , _bPrefabIsolation{ SW_FALSE }
        , _reservedWorkspace{ 0 }
    {
    }

    // ------------------------------------------------------------------------------
    // Member Methods
    // ------------------------------------------------------------------------------
    uint64 EditorWorkspace::getSelectedObjectId() const
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            return pContext->getSelectionManager().getPrimaryObjectId();
        return 0;
    }

    GameObjectPtr EditorWorkspace::getSelectedObject() const
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            return pContext->getSelectionManager().getPrimaryObject();
        return GameObjectPtr{};
    }

    string EditorWorkspace::getSelectedObjectName() const
    {
        GameObjectPtr pObj = getSelectedObject();
        if ( pObj.isValid() )
            return string{ pObj.get()->getName().c_str() };
        return {};
    }

    void EditorWorkspace::clearSelection()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getSelectionManager().clearAll();
        _selectedComponentId = 0;
        _selectedComponentKey.clear();
    }

    void EditorWorkspace::selectGameObject( GameObjectPtr pObj, SelectionMode mode )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getSelectionManager().selectObject( pObj, mode );
        _selectedComponentId = 0;
        _selectedComponentKey.clear();
        _inspectMode = InspectMode::GameObject;
    }

    void EditorWorkspace::selectComponent( GameObjectPtr pObj, ComponentPtr pComp )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getSelectionManager().selectObject( pObj, SelectionMode::Replace );

        Component* pRawComp = pComp.get();
        if ( pRawComp != nullptr )
            _selectedComponentId = pRawComp->getComponentId();
        else
            _selectedComponentId = 0;

        GameObject* pRawObj = pObj.get();
        if ( pRawObj != nullptr && pRawComp != nullptr )
            _selectedComponentKey = EditorWorkspaceInternal::computeStableComponentKey( pRawObj, pRawComp );
        else
            _selectedComponentKey.clear();

        _scrollToComponentId = _selectedComponentId;
        _inspectMode         = InspectMode::GameObject;
    }

    void EditorWorkspace::remapSelectionByObjectName( GameObjectManager* pGameObjectManager )
    {
        if ( pGameObjectManager == nullptr )
            return;

        const string name = getSelectedObjectName();
        if ( name.empty() )
        {
            _selectedComponentId = 0;
            _selectedComponentKey.clear();
            return;
        }

        GameObject* pObj = pGameObjectManager->findGameObjectByName( hashed_string( name.c_str() ) );
        if ( pObj == nullptr )
        {
            clearSelection();
            return;
        }

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getSelectionManager().selectObject( GameObjectPtr{ pObj }, SelectionMode::Replace );

        if ( _selectedComponentKey.empty() )
        {
            _selectedComponentId = 0;
            return;
        }

        Component* pRematerialized = EditorWorkspaceInternal::findComponentByStableKey( pObj, _selectedComponentKey );
        if ( pRematerialized != nullptr )
            _selectedComponentId = pRematerialized->getComponentId();
        else
            _selectedComponentId = 0;
    }

    void EditorWorkspace::setFocusedAssetPath( const utf8* pPath )
    {
        _focusedAssetPath = ( pPath != nullptr ) ? pPath : "";
        if ( pPath != nullptr && pPath[0] != '\0' )
        {
            EditorContext* pContext = EditorContext::get();
            if ( pContext != nullptr )
                pContext->getSelectionManager().selectAsset( pPath, SelectionMode::Replace );
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

    void EditorWorkspace::rebuildGameObjectPrefabMap( GameObjectManager* pManager )
    {
        _mapGameObjectToPrefab.clear();
        if ( pManager == nullptr )
            return;

        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return;
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr )
            return;

        for ( GameObject* pGo : pManager->getAllGameObjects() )
        {
            if ( pGo == nullptr )
                continue;
            const string& path = pScene->getEntityPrefabPath( pGo->getObjectId() );
            if ( path.empty() == false )
                _mapGameObjectToPrefab[pGo->getObjectId()] = path;
        }
    }

    void EditorWorkspace::clearGameObjectPrefabMap()
    {
        _mapGameObjectToPrefab.clear();
    }

    void EditorWorkspace::setGameObjectPrefabPath( uint64 objectId, string_view prefabPath )
    {
        if ( objectId == 0 )
            return;
        if ( prefabPath.empty() )
            _mapGameObjectToPrefab.erase( objectId );
        else
            _mapGameObjectToPrefab[objectId] = string{ prefabPath };

        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return;
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr )
            return;
        pScene->setEntityPrefabPath( objectId, prefabPath );
    }

    const string& EditorWorkspace::getGameObjectPrefabPath( uint64 objectId ) const
    {
        const auto it = _mapGameObjectToPrefab.find( objectId );
        if ( it != _mapGameObjectToPrefab.end() )
            return it->second;

        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return _emptyString;
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr )
            return _emptyString;
        return pScene->getEntityPrefabPath( objectId );
    }

    bool EditorWorkspace::isGameObjectPrefabInstance( uint64 objectId ) const
    {
        return getGameObjectPrefabPath( objectId ).empty() == false;
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

        _copiedComponentTypeName = pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str()
                                                                              : pComp->getTypeInfo()->_name.c_str();
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

    void EditorWorkspace::snapSelectedToGround()
    {
        EditorTransformCommands::snapSelectedToGround();
    }

    void EditorWorkspace::alignSelectedObjects( AlignAxis axis, AlignType type )
    {
        EditorTransformCommands::alignSelectedObjects( axis, type );
    }

    void EditorWorkspace::distributeSelectedObjects( AlignAxis axis )
    {
        EditorTransformCommands::distributeSelectedObjects( axis );
    }

    const string& EditorWorkspace::getPrefabIsolationPrefabPath() const
    {
        if ( _listPrefabIsolationFrame.empty() )
            return _emptyString;
        return _listPrefabIsolationFrame.back()._prefabPath;
    }

    uint64 EditorWorkspace::getPrefabIsolationRootId() const
    {
        if ( _listPrefabIsolationFrame.empty() )
            return 0;
        return _listPrefabIsolationFrame.back()._rootObjectId;
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

    Uuid EditorWorkspace::getOrAssignGuid( uint64 objectId )
    {
        if ( objectId == 0 )
            return Uuid{};

        const auto it = _mapObjectIdToGuid.find( objectId );
        if ( it != _mapObjectIdToGuid.end() )
            return it->second;

        const Uuid newGuid           = Uuid::generate();
        _mapObjectIdToGuid[objectId] = newGuid;
        _mapGuidToObjectId[newGuid]  = objectId;
        return newGuid;
    }

    Uuid EditorWorkspace::getGuid( uint64 objectId ) const
    {
        if ( objectId == 0 )
            return Uuid{};

        const auto it = _mapObjectIdToGuid.find( objectId );
        if ( it != _mapObjectIdToGuid.end() )
            return it->second;
        return Uuid{};
    }

    void EditorWorkspace::setGuid( uint64 objectId, const Uuid& guid )
    {
        if ( objectId == 0 || guid.isNull() )
            return;

        const auto itOld = _mapObjectIdToGuid.find( objectId );
        if ( itOld != _mapObjectIdToGuid.end() )
            _mapGuidToObjectId.erase( itOld->second );

        _mapObjectIdToGuid[objectId] = guid;
        _mapGuidToObjectId[guid]     = objectId;
    }

    uint64 EditorWorkspace::findObjectIdByGuid( const Uuid& guid ) const
    {
        if ( guid.isNull() )
            return 0;

        const auto it = _mapGuidToObjectId.find( guid );
        if ( it != _mapGuidToObjectId.end() )
            return it->second;
        return 0;
    }

    GameObject* EditorWorkspace::findGameObjectByGuid( const Uuid& guid ) const
    {
        const uint64 objectId = findObjectIdByGuid( guid );
        if ( objectId == 0 )
            return nullptr;

        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager == nullptr )
            return nullptr;
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return nullptr;

        return pScene->getObjectManager()->findGameObjectById( objectId );
    }

    void EditorWorkspace::removeGuid( uint64 objectId )
    {
        const auto it = _mapObjectIdToGuid.find( objectId );
        if ( it != _mapObjectIdToGuid.end() )
        {
            _mapGuidToObjectId.erase( it->second );
            _mapObjectIdToGuid.erase( it );
        }
    }

    void EditorWorkspace::clearGuidMap()
    {
        _mapObjectIdToGuid.clear();
        _mapGuidToObjectId.clear();
    }
} // namespace sw::editor
