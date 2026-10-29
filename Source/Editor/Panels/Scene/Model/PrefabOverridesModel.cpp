#include "pch.h"

#include "Editor/Panels/Scene/Model/PrefabOverridesModel.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/Prefab/PrefabAsset.h"

namespace sw::editor
{
    namespace
    {
        struct PrefabOverridesModelInternal
        {
            /** @brief 오버라이드를 볼 프리팹 인스턴스 — 메인 씬에 놓인 인스턴스다(언리얼 레벨의 프리팹 · 블루프린트 인스턴스 패널처럼 레벨 선택). */
            static GameObject* getPrefabTargetInstance()
            {
                return editor::getMainSceneSelection().getPrimaryObject();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "PrefabTool" );

    PrefabOverridesModel::PrefabOverridesModel()
        : _selectedPrefabPath{}
        , _selectedInstanceName{}
        , _lastScanKey{}
        , _listOverride{}
        , _listNestedPrefab{}
        , _bShowOnlyModified{ true }
    {
        scan( nullptr );
    }

    void PrefabOverridesModel::scan( const utf8* pPrefabPath )
    {
        const string_view path = ( pPrefabPath != nullptr ) ? string_view{ pPrefabPath } : string_view{};
        EditorToolAssetCommands::collectPrefabOverrides( PrefabOverridesModelInternal::getPrefabTargetInstance(), path, _selectedPrefabPath,
                                                         _selectedInstanceName, _listOverride, _listNestedPrefab );
    }

    void PrefabOverridesModel::refreshIfFocusChanged()
    {
        const utf8*       pScanPath{ nullptr };
        GameObject*       pPrimary = editor::getMainSceneSelection().getPrimaryObject();
        const uint64      objectID = pPrimary != nullptr ? pPrimary->getObjectID() : 0;
        const string_view matching = EditorAssetTypeRegistry::matchingFocusedPath( EditorAssetType{ PrefabAsset::kAssetType } );
        if ( matching.empty() == false )
            pScanPath = matching.data();
        if ( EditorAssetTypeRegistry::consumeWorkspaceFocusKey( _lastScanKey, objectID ) )
            scan( pScanPath );
    }

    void PrefabOverridesModel::rescan()
    {
        scan( _selectedPrefabPath.c_str() );
    }

    void PrefabOverridesModel::applyAllToTemplate()
    {
        if ( EditorToolAssetCommands::applyPrefabOverridesToTemplate( PrefabOverridesModelInternal::getPrefabTargetInstance(), AssetID{ _selectedPrefabPath } ) )
            SW_LOG_TRACE( "Applied all instance overrides back to template %s", _selectedPrefabPath.c_str() );
        else
            SW_LOG_ERROR( "Could not apply overrides to template %s", _selectedPrefabPath.c_str() );
        rescan();
    }

    void PrefabOverridesModel::revertAll()
    {
        if ( EditorToolAssetCommands::revertAllPrefabOverrides( PrefabOverridesModelInternal::getPrefabTargetInstance(), _selectedPrefabPath ) )
            SW_LOG_TRACE( "Reverted all overrides on %s", _selectedInstanceName.c_str() );
        else
            SW_LOG_ERROR( "Could not revert overrides on %s", _selectedInstanceName.c_str() );
        rescan();
    }

    void PrefabOverridesModel::revertOverride( size_t overrideIndex )
    {
        PrefabOverrideItem& item = _listOverride[overrideIndex];
        EditorToolAssetCommands::revertPrefabOverride( PrefabOverridesModelInternal::getPrefabTargetInstance(), item, _selectedPrefabPath );
        SW_LOG_TRACE( "Reverted %s.%s to %s", item._componentName.c_str(), item._propertyName.c_str(), item._defaultValue.c_str() );
        rescan();
    }
} // namespace sw::editor
