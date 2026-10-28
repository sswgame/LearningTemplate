/**
 * @file InputMapProbes.cpp
 * @brief Input Map 편집기 패널의 자동화 탐침입니다 — `Editor.InputMapBindingCount` · `Editor.InputMapSelectedTrigger` · `Editor.InputMapConflicts`.
 * @details 탐침 레지스트리(`Engine/Automation/Probe`)가 기능 층보다 아래라 기능이 자기 탐침을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorProbeSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/InputMap/InputMapPanel.h"
#include "Editor/SelfTest/EditorProbeSupport.h"

#include "Engine/Automation/Probe/AutomationProbe.h"

namespace sw::editor
{
    namespace
    {
        /** @brief Input Map Editor 값 하나를 읽습니다. 0 은 선택한 액션의 바인딩 수, 1 은 그 0번 바인딩의 발화 규칙, 2 는 충돌 수입니다. 패널이 없거나 선택한 액션이 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readInputMapValue( uint32 which, float64& outValue )
        {
            EditorContext*       pContext = getProbeContext( __func__ );
            const InputMapPanel* pPanel   = pContext != nullptr ? pContext->getSubsystem<EditorPanelManager>().findPanel<InputMapPanel>() : nullptr;
            if ( pPanel == nullptr )
                return false;
            uint32 value{ 0 };
            if ( which == 2 )
                value = pPanel->getConflictCount();
            else if ( ( which == 0 ? pPanel->readSelectedBindingCount( value ) : pPanel->readSelectedTrigger( value ) ) == false )
                return false;
            outValue = static_cast<float64>( value );
            return true;
        }

        [[nodiscard]] bool readInputMapBindingCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readInputMapValue( 0, outValue );
        }

        [[nodiscard]] bool readInputMapSelectedTrigger( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readInputMapValue( 1, outValue );
        }

        [[nodiscard]] bool readInputMapConflicts( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readInputMapValue( 2, outValue );
        }
    } // namespace

    SW_AUTOMATION_PROBE( editorInputMapBindingCount, "Editor.InputMapBindingCount", "Bindings of the action selected in the Input Map Editor",
                         &readInputMapBindingCount );
    SW_AUTOMATION_PROBE( editorInputMapSelectedTrigger, "Editor.InputMapSelectedTrigger", "ActionTrigger number of binding 0 of the selected Input Map action",
                         &readInputMapSelectedTrigger );
    SW_AUTOMATION_PROBE( editorInputMapConflicts, "Editor.InputMapConflicts", "Binding conflicts the Input Map Editor found (same key, same layer, two actions)",
                         &readInputMapConflicts );
} // namespace sw::editor
