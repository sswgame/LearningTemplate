#include "pch.h"

#include "Editor/Common/Commands/EditorInspectorCommands.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"

namespace sw::editor
{
    namespace
    {
        struct EditorInspectorCommandsInternal
        {
            static GameObject* findObjectById( uint64 objectId )
            {
                if ( objectId == 0 )
                    return nullptr;
                Scene* pScene = editor::getActiveScene();
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                    return nullptr;
                GameObject* pObj = pScene->getObjectManager()->findGameObjectById( objectId );
                if ( pObj == nullptr || pObj->isPendingKill() )
                    return nullptr;
                return pObj;
            }

            static void markSceneDirty()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    pContext->getWorkspace().markSceneDirty();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorInspectorCommands" );

    void EditorInspectorCommands::pushPodEdit( void* pData, size_t size, vector<uint8> beforeBytes, vector<uint8> afterBytes,
                                               string_view label, uint64 selectedObjectId )
    {
        if ( pData == nullptr || size == 0 || beforeBytes.size() != size || afterBytes.size() != size )
            return;

        CommandStack* pStack = editor::getService<CommandStack>();
        if ( pStack == nullptr )
            return;

        const string          cmdLabel = string( "Edit " ) + string{ label };
        CommandStack::Command cmd;
        cmd._label = cmdLabel;
        // **람다로 옮긴다.** 예전에는 값 매개변수를 람다가 다시 값으로 캡처해서 편집마다 바이트를 두 번 복사했다.
        // 부르는 쪽이 임시 객체를 넘기면 이제 복사가 아예 없다.
        cmd._undo = [pData, size, undoBytes = std::move( beforeBytes ), selectedObjectId]()
        {
            if ( selectedObjectId != 0 && EditorInspectorCommandsInternal::findObjectById( selectedObjectId ) == nullptr )
                return;
            Memory::copy( pData, undoBytes.data(), size );
        };
        cmd._redo = [pData, size, redoBytes = std::move( afterBytes ), selectedObjectId]()
        {
            if ( selectedObjectId != 0 && EditorInspectorCommandsInternal::findObjectById( selectedObjectId ) == nullptr )
                return;
            Memory::copy( pData, redoBytes.data(), size );
        };
        pStack->push( std::move( cmd ) );
        EditorInspectorCommandsInternal::markSceneDirty();
    }

    void EditorInspectorCommands::pushStringEdit( string* pPtr, string before, string after, string_view label,
                                                  uint64 selectedObjectId )
    {
        if ( pPtr == nullptr )
            return;

        CommandStack* pStack = editor::getService<CommandStack>();
        if ( pStack == nullptr )
            return;

        const string          cmdLabel = string( "Edit " ) + string{ label };
        CommandStack::Command cmd;
        cmd._label = cmdLabel;
        cmd._undo  = [pPtr, undoText = std::move( before ), selectedObjectId]()
        {
            if ( selectedObjectId != 0 && EditorInspectorCommandsInternal::findObjectById( selectedObjectId ) == nullptr )
                return;
            *pPtr = undoText;
        };
        cmd._redo = [pPtr, redoText = std::move( after ), selectedObjectId]()
        {
            if ( selectedObjectId != 0 && EditorInspectorCommandsInternal::findObjectById( selectedObjectId ) == nullptr )
                return;
            *pPtr = redoText;
        };
        pStack->push( std::move( cmd ) );
        EditorInspectorCommandsInternal::markSceneDirty();
    }

    bool EditorInspectorCommands::applyToPrefab( GameObject* pObj, string_view prefabPath )
    {
        if ( EditorUtil::areSceneEditsAllowed() == false )
            return false;
        if ( pObj == nullptr || prefabPath.empty() )
            return false;

        PrefabAsset asset;
        asset.setFromGameObject( pObj );
        if ( asset.saveToXmlFile( prefabPath ) == false )
            return false;

        SW_LOG_INFO( "Saved prefab changes to %#", string{ prefabPath }.c_str() );
        return true;
    }

    bool EditorInspectorCommands::revertToPrefab( GameObject* pObj, string_view prefabPath )
    {
        if ( EditorUtil::areSceneEditsAllowed() == false )
            return false;
        if ( pObj == nullptr || prefabPath.empty() )
            return false;

        ResourceManager* pResources = editor::getService<ResourceManager>();
        if ( pResources == nullptr )
            return false;

        PrefabAsset* pLoaded = pResources->getPrefabManager().loadPrefab( prefabPath );
        if ( pLoaded == nullptr || pLoaded->isValid() == false )
            return false;

        const EditorObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
        ObjectStateSerializer::loadFromXmlString( pObj, pLoaded->getStateData() );
        pObj->applyLoadedHierarchy();
        const EditorObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pObj );
        EditorTransaction::recordModify( pObj, beforeSnapshot, afterSnapshot, "Revert to Prefab" );
        return true;
    }

    void EditorInspectorCommands::unlinkPrefab( GameObject* pObj )
    {
        if ( EditorUtil::areSceneEditsAllowed() == false )
            return;
        if ( pObj == nullptr )
            return;

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        pContext->getWorkspace().setGameObjectPrefabPath( pObj->getObjectId(), "" );
        pContext->getWorkspace().markSceneDirty();
    }
} // namespace sw::editor
