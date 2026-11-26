/**
 * @file GlobalVariablesWatches.cpp
 * @brief 전역 변수 창의 자동화 감시 값입니다 — `Editor.GlobalVariablesRebuildCount`.
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Panels/GlobalVariables/GlobalVariablesPanel.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 전역 변수 창이 보일 목록을 다시 지은 횟수입니다. 창이 없으면(에디터 컨텍스트 없음 · 창 인스턴스 없음) 값을 내지 않습니다.
         * @details 창은 등록 세대 · 검색 글이 바뀐 프레임에만 목록을 짓는다. 창을 연 채 몇 프레임 지나도 이 수가 늘지 않으면 매 프레임 재구성이 없다.
         */
        [[nodiscard]] bool readGlobalVariablesRebuildCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const GlobalVariablesPanel* pPanel = EditorPanelManager::findPanelInEditorContext<GlobalVariablesPanel>();
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
