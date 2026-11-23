#include "pch.h"

#include "Editor/Panels/Scene/Model/PrefabOverridesViewModel.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/Prefab/PrefabAsset.h"

namespace sw::editor
{
    namespace
    {
        struct PrefabOverridesViewModelInternal
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
    PrefabOverridesViewModel::PrefabOverridesViewModel()
        : _selectedPrefabPath{}
        , _selectedInstanceName{}
        , _lastScanKey{}
        , _scannedEditSerial{ 0 }
        , _listOverride{}
        , _listNestedPrefab{}
        , _bShowOnlyModified{ true }
    {
        rescan();
    }

    void PrefabOverridesViewModel::rescan()
    {
        // 프리팹에 포커스가 있으면 그 중첩 목록을 보인다 — 인스턴스를 골랐으면 인스턴스의 프리팹이 이긴다(`collectPrefabOverrides`).
        const string_view focused = EditorAssetTypeRegistry::matchingFocusedPath( EditorAssetType{ PrefabAsset::kAssetType } );
        EditorToolAssetCommands::collectPrefabOverrides( PrefabOverridesViewModelInternal::getPrefabTargetInstance(), focused, _selectedPrefabPath,
                                                         _selectedInstanceName, _listOverride, _listNestedPrefab );
    }

    void PrefabOverridesViewModel::refreshIfChanged()
    {
        const GameObject*      pPrimary   = PrefabOverridesViewModelInternal::getPrefabTargetInstance();
        const uint64           objectID   = pPrimary != nullptr ? pPrimary->getObjectID() : 0;
        const EditorWorkspace* pWorkspace = editor::findSubsystem<EditorWorkspace>(); // 선택: 셸 없는 테스트 호스트에는 없다
        const uint32           editSerial = pWorkspace != nullptr ? pWorkspace->getSceneEditSerial() : 0;
        const bool             bFocus     = EditorAssetTypeRegistry::consumeWorkspaceFocusKey( _lastScanKey, objectID );
        if ( bFocus == false && editSerial == _scannedEditSerial )
            return;
        _scannedEditSerial = editSerial;
        rescan();
    }

    void PrefabOverridesViewModel::applyAllToTemplate()
    {
        if ( EditorToolAssetCommands::applyPrefabOverridesToTemplate( PrefabOverridesViewModelInternal::getPrefabTargetInstance() ) )
            SW_LOG_TRACE_ID( "PrefabTool.AppliedAllInstanceOverrides", "Applied all instance overrides back to template %#", _selectedPrefabPath.c_str() );
        else
            SW_LOG_ERROR_ID( "PrefabTool.CouldNotApplyOverrides", "Could not apply overrides to template %#", _selectedPrefabPath.c_str() );
        rescan();
    }

    void PrefabOverridesViewModel::revertAll()
    {
        if ( EditorToolAssetCommands::revertAllPrefabOverrides( PrefabOverridesViewModelInternal::getPrefabTargetInstance() ) )
            SW_LOG_TRACE_ID( "PrefabTool.RevertedAllOverrides", "Reverted all overrides on %#", _selectedInstanceName.c_str() );
        else
            SW_LOG_ERROR_ID( "PrefabTool.CouldNotRevertOverrides", "Could not revert overrides on %#", _selectedInstanceName.c_str() );
        rescan();
    }

    void PrefabOverridesViewModel::revertOverride( size_t overrideIndex )
    {
        PrefabOverrideItem& item = _listOverride[overrideIndex];
        EditorToolAssetCommands::revertPrefabOverride( PrefabOverridesViewModelInternal::getPrefabTargetInstance(), item );
        SW_LOG_TRACE_ID( "PrefabTool.Reverted", "Reverted %#.%# to %#", item._componentName.c_str(), item._propertyName.c_str(), item._defaultValue.c_str() );
        rescan();
    }
} // namespace sw::editor
