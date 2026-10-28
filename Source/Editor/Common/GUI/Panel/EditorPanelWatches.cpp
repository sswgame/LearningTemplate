/**
 * @file EditorPanelWatches.cpp
 * @brief 패널 매니저(`EditorPanelManager`)의 자동화 감시 값입니다 — `Editor.PanelOpen` · `Editor.PanelDirty` · `Editor.HasPanel`.
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다. 패널 id 를 정하는 전역 변수 `gv_editorWatchPanel` 은 `EditorWatchSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw::editor
{
    namespace
    {
        /** @brief `-gv_editorWatchPanel` 의 패널이 열려 있으면 1 입니다. 그 id 의 패널이 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readWatchedPanelOpen( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getWatchContext( __func__ );
            if ( pContext == nullptr )
                return false;
            const IEditorPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanelByID( gv_editorWatchPanel );
            if ( pPanel == nullptr )
                return false;
            outValue = pPanel->isOpen() ? 1.0 : 0.0;
            return true;
        }

        /** @brief `-gv_editorWatchPanel` 의 패널 문서에 저장하지 않은 편집이 있으면 1 입니다. 그 id 의 패널이 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readWatchedPanelDirty( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getWatchContext( __func__ );
            if ( pContext == nullptr )
                return false;
            const IEditorPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanelByID( gv_editorWatchPanel );
            if ( pPanel == nullptr )
                return false;
            outValue = pPanel->isDocumentDirty() ? 1.0 : 0.0;
            return true;
        }

        /**
         * @brief 패널 id(`key`)의 패널을 패널 매니저가 가지면 1, 아니면 0 입니다. 등록 목록이 DLL 마다 갈라지면 그 이미지의 패널이 0 이 됩니다.
         * @details 패널 수를 숫자 하나로 박으면 패널을 더할 때마다 시나리오를 손으로 고쳐야 한다. 이미지마다 대표 패널의 id 로 묻는다.
         */
        [[nodiscard]] bool readHasPanel( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            EditorContext* pContext = editor::findSubsystem<EditorContext>();
            if ( pContext == nullptr )
            {
                outValue.setText( "no editor context" );
                return false;
            }
            bool bFound = false;
            for ( const EditorPanelEntry& entry : pContext->getSubsystem<EditorPanelManager>().getPanels() )
            {
                if ( string_view{ entry._id } == query._key )
                {
                    bFound = true;
                    break;
                }
            }
            outValue.setNumber( bFound ? 1.0 : 0.0 );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorWatchedPanelOpen, "Editor.PanelOpen", "1 when the panel named by gv_editorWatchPanel is open", &readWatchedPanelOpen );
    SW_AUTOMATION_WATCH( editorWatchedPanelDirty, "Editor.PanelDirty", "1 when the document of the panel named by gv_editorWatchPanel has unsaved edits", &readWatchedPanelDirty );
    SW_AUTOMATION_QUERY_WATCH( editorHasPanel, "Editor.HasPanel", "1 when the panel manager holds a panel whose id is the key, else 0", &readHasPanel, false, true );
} // namespace sw::editor
