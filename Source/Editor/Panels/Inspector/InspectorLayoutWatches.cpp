/**
 * @file InspectorLayoutWatches.cpp
 * @brief 인스펙터 그리드의 배치 · 메서드 목록 · 값 행을 시나리오가 읽는 감시 값입니다(`Editor.InspectorLayoutRebuildCount` · `Editor.InspectorMethodRows` · `Editor.InspectorHiddenMethods` ·
 *        `Editor.InspectorFilledRows` · `Editor.InspectorSkippedRows`).
 * @details 묶음은 (타입, 확장이 그린 이름, 검색 글, 타입 레지스트리 세대)가 바뀔 때만 짓는다. 같은 선택을 몇 프레임 그려도 이 수가 늘지 않으면 매 프레임 재구성이 없다.
 *          메서드 수는 마지막으로 그린 프레임의 값이다 — 기본 목록은 `CallInEditor` 만이고 나머지는 숨긴 수로 센다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Panels/Inspector/InspectorPanel.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 인스펙터 창입니다. 에디터 컨텍스트나 창이 없으면 nullptr 입니다. */
        const InspectorPanel* findInspectorPanel()
        {
            const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
            return pPanels != nullptr ? pPanels->findPanel<InspectorPanel>() : nullptr;
        }

        /** @brief 인스펙터 그리드의 묶음 재구성 횟수입니다. 에디터 컨텍스트나 인스펙터 창이 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readInspectorLayoutRebuildCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const InspectorPanel* pPanel = findInspectorPanel();
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getLayoutRebuildCount() );
            return true;
        }

        /** @brief 인스펙터가 마지막 프레임에 그린 메서드 행 수입니다. */
        [[nodiscard]] bool readInspectorMethodRows( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const InspectorPanel* pPanel = findInspectorPanel();
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getMethodRowCount() );
            return true;
        }

        /** @brief 인스펙터가 마지막 프레임에 숨긴(`CallInEditor` 없는) 메서드 수입니다. */
        [[nodiscard]] bool readInspectorHiddenMethods( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const InspectorPanel* pPanel = findInspectorPanel();
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getHiddenMethodCount() );
            return true;
        }

        /** @brief 인스펙터가 마지막 프레임에 값을 채운 프로퍼티 행 수입니다(화면 밖 행은 세지 않는다). */
        [[nodiscard]] bool readInspectorFilledRows( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const InspectorPanel* pPanel = findInspectorPanel();
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getFilledRowCount() );
            return true;
        }

        /** @brief 인스펙터가 마지막 프레임에 화면 밖이라 높이만 둔 행 수입니다. */
        [[nodiscard]] bool readInspectorSkippedRows( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const InspectorPanel* pPanel = findInspectorPanel();
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getSkippedRowCount() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorInspectorLayoutRebuildCount, "Editor.InspectorLayoutRebuildCount",
                         "Times the Inspector property grid rebuilt a category layout (grows only when the type, search text or type registry generation change)",
                         &readInspectorLayoutRebuildCount );
    SW_AUTOMATION_WATCH( editorInspectorMethodRows, "Editor.InspectorMethodRows",
                         "FUNCTION rows the Inspector drew in its last frame (CallInEditor only unless the diagnostic view is on)", &readInspectorMethodRows );
    SW_AUTOMATION_WATCH( editorInspectorHiddenMethods, "Editor.InspectorHiddenMethods",
                         "FUNCTIONs without CallInEditor the Inspector hid in its last frame (0 while the diagnostic view shows all)", &readInspectorHiddenMethods );
    SW_AUTOMATION_WATCH( editorInspectorFilledRows, "Editor.InspectorFilledRows",
                         "Property rows the Inspector filled with values in its last frame (rows outside the visible area are not filled)", &readInspectorFilledRows );
    SW_AUTOMATION_WATCH( editorInspectorSkippedRows, "Editor.InspectorSkippedRows",
                         "Top-level property rows the Inspector kept as height only in its last frame because they were outside the visible area", &readInspectorSkippedRows );
} // namespace sw::editor
