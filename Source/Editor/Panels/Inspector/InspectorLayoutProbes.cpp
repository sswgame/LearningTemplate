/**
 * @file InspectorLayoutProbes.cpp
 * @brief 인스펙터 그리드가 배치 묶음을 새로 지은 횟수를 시나리오가 읽는 탐침입니다(`Editor.InspectorLayoutRebuildCount`).
 * @details 묶음은 (타입, 확장이 그린 이름, 검색 글, 타입 레지스트리 세대)가 바뀔 때만 짓는다. 같은 선택을 몇 프레임 그려도 이 수가 늘지 않으면 매 프레임 재구성이 없다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Inspector/InspectorPanel.h"

#include "Engine/Automation/Probe/AutomationProbe.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 인스펙터 그리드의 묶음 재구성 횟수입니다. 에디터 컨텍스트나 인스펙터 창이 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readInspectorLayoutRebuildCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = editor::findSubsystem<EditorContext>();
            if ( pContext == nullptr )
                return false;
            const InspectorPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<InspectorPanel>();
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getLayoutRebuildCount() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_PROBE( editorInspectorLayoutRebuildCount, "Editor.InspectorLayoutRebuildCount",
                         "Times the Inspector property grid rebuilt a category layout (grows only when the type, search text or type registry generation change)",
                         &readInspectorLayoutRebuildCount );
} // namespace sw::editor
