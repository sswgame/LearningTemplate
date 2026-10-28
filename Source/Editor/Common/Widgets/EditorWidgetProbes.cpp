/**
 * @file EditorWidgetProbes.cpp
 * @brief 공용 위젯의 자동화 탐침입니다 — `Editor.NoSearchResultHintShown` · `Editor.GraphNodeCount`.
 * @details 탐침 레지스트리(`Engine/Automation/Probe`)가 기능 층보다 아래라 기능이 자기 탐침을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorProbeSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Editor/Common/Widgets/EditorNodeGraph.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Engine/Automation/Probe/AutomationProbe.h"

namespace sw::editor
{
    namespace
    {
        [[nodiscard]] bool readNoSearchResultHintShown( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = EditorWidgets::wasFilteredNoResultHintDrawnRecently() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 가장 최근에 그린 노드 그래프 캔버스의 노드 수입니다. 대화 그래프, 애니메이션 그래프, 확장의 그래프 문서 패널이 해당합니다. */
        [[nodiscard]] bool readGraphNodeCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( EditorNodeGraph::getDrawnNodeCount() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_PROBE( editorNoSearchResultHintShown, "Editor.NoSearchResultHintShown", "1 when a panel drew its zero-results hint for a search this or last frame",
                         &readNoSearchResultHintShown );
    SW_AUTOMATION_PROBE( editorGraphNodeCount, "Editor.GraphNodeCount", "Nodes of the node graph canvas drawn most recently (dialogue, animation, extension graphs)",
                         &readGraphNodeCount );
} // namespace sw::editor
