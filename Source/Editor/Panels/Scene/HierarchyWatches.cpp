/**
 * @file HierarchyWatches.cpp
 * @brief Hierarchy 패널의 자동화 감시 값입니다 — `Editor.HierarchyVisibleRoots` · `Editor.HierarchyRowIcon` · `Editor.HierarchyRowIndex` · `Editor.HierarchyRowRebuildCount`.
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorWatchSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Name/hashed_string.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorComponentIconRow.h"
#include "Editor/Panels/Scene/HierarchyPanel.h"
#include "Editor/Panels/Scene/Model/HierarchyViewModel.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw::editor
{
    /** @brief 프로브 `Editor.HierarchyRowIndex` 가 볼 오브젝트 이름입니다. 시나리오가 `<Variable>` 로 정합니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchHierarchyObject, "", "감시 값 Editor.HierarchyRowIndex 가 볼 오브젝트 이름 (시나리오용)" );

    namespace
    {
        /** @brief `gv_editorWatchHierarchyObject` 오브젝트가 오브젝트 행 사이에서 몇 번째인지입니다(0 부터). 행에 없거나 없는 이름이면 -1 입니다. */
        [[nodiscard]] bool readHierarchyRowIndex( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const optional<uint32> rowIndex = HierarchyViewModel::get().findObjectRowIndexByName( gv_editorWatchHierarchyObject.c_str() );
            outValue                        = rowIndex.has_value() ? static_cast<float64>( *rowIndex ) : -1.0;
            return true;
        }

        /** @brief 행 모델을 다시 지은 횟수입니다(감시 값 `Editor.HierarchyRowRebuildCount`). 구조가 그대로인 프레임에는 오르지 않아야 한다. */
        [[nodiscard]] bool readHierarchyRowRebuildCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( HierarchyViewModel::get().getRebuildCount() );
            return true;
        }

        [[nodiscard]] bool readHierarchyVisibleRoots( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const EditorPanelManager* pPanels    = getWatchPanelManager( __func__ );
            const HierarchyPanel*     pHierarchy = pPanels != nullptr ? pPanels->findPanel<HierarchyPanel>() : nullptr;
            if ( pHierarchy == nullptr )
                return false;
            outValue = static_cast<float64>( pHierarchy->getVisibleRootCount() );
            return true;
        }

        /** @brief `-gv_editorWatchObject` 이름의 오브젝트가 Hierarchy 행에 그리는 아이콘의 코드 포인트입니다. 그 이름의 오브젝트가 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readHierarchyRowIcon( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const GameObject* pObject = findWatchedObject();
            if ( pObject == nullptr )
                return false;
            outValue = static_cast<float64>( EditorComponentIconRow::decodeGlyph( EditorComponentIconRow::findObjectRow( *pObject )._pGlyph ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorHierarchyVisibleRoots, "Editor.HierarchyVisibleRoots", "Root rows the Hierarchy showed in the last frame (after its filter)",
                         &readHierarchyVisibleRoots );
    SW_AUTOMATION_WATCH( editorHierarchyRowIndex, "Editor.HierarchyRowIndex",
                         "Row index (from 0) among the Hierarchy object rows (expanded, after the filter) of the object named by gv_editorWatchHierarchyObject (-1 when not listed)",
                         &readHierarchyRowIndex );
    SW_AUTOMATION_WATCH( editorHierarchyRowRebuildCount, "Editor.HierarchyRowRebuildCount",
                         "Times the Hierarchy row model was rebuilt (roots and filter matches); rises only when the scene structure, tags or filter text change",
                         &readHierarchyRowRebuildCount );
    SW_AUTOMATION_WATCH( editorHierarchyRowIcon, "Editor.HierarchyRowIcon", "Icon code point of the Hierarchy row of the object named by gv_editorWatchObject",
                         &readHierarchyRowIcon );
} // namespace sw::editor
