/**
 * @file GlobalVariablesPanelWatchs.cpp
 * @brief 전역 변수 창이 보일 목록을 다시 지은 횟수를 시나리오가 읽는 감시 값입니다(`Editor.GlobalVariablesRebuildCount`).
 * @details 창은 등록 세대 · 검색 글이 바뀐 프레임에만 목록을 짓는다. 창을 연 채 몇 프레임 지나도 이 수가 늘지 않으면 매 프레임 재구성이 없다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Settings/GlobalVariablesPanel.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 전역 변수 창의 목록 재구성 횟수입니다. 창이 없으면(에디터 컨텍스트 없음 · 창 인스턴스 없음) 값을 내지 않습니다. */
        [[nodiscard]] bool readGlobalVariablesRebuildCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = editor::findSubsystem<EditorContext>();
            if ( pContext == nullptr )
                return false;
            const GlobalVariablesPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<GlobalVariablesPanel>();
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getListRebuildCount() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorGlobalVariablesRebuildCount, "Editor.GlobalVariablesRebuildCount",
                         "Times the Global Variables panel rebuilt its visible variable list (grows only when registrations or the search text change)",
                         &readGlobalVariablesRebuildCount );
} // namespace sw::editor
