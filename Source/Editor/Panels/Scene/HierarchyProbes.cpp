/**
 * @file HierarchyProbes.cpp
 * @brief Hierarchy 패널의 자동화 탐침입니다 — `Editor.HierarchyVisibleRoots` · `Editor.HierarchyRowIcon`.
 * @details 탐침 레지스트리(`Engine/Automation/Probe`)가 기능 층보다 아래라 기능이 자기 탐침을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorProbeSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/EditorComponentIcon.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Scene/HierarchyPanel.h"
#include "Editor/SelfTest/EditorProbeSupport.h"

#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        [[nodiscard]] bool readHierarchyVisibleRoots( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext*        pContext = getProbeContext( __func__ );
            const HierarchyPanel* pHierarchy =
                pContext != nullptr ? pContext->getSubsystem<EditorPanelManager>().findPanel<HierarchyPanel>() : nullptr;
            if ( pHierarchy == nullptr )
                return false;
            outValue = static_cast<float64>( pHierarchy->getVisibleRootCount() );
            return true;
        }

        /** @brief `-gv_editorProbeObject` 이름의 오브젝트가 Hierarchy 행에 그리는 아이콘의 코드 포인트입니다. 그 이름의 오브젝트가 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readHierarchyRowIcon( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const GameObjectManager* pManager = editor::getActiveObjectManager();
            if ( pManager == nullptr )
                return false;
            const GameObject* pObject = pManager->findGameObjectByName( hashed_string( gv_editorProbeObject.c_str() ) );
            if ( pObject == nullptr )
                return false;
            outValue = static_cast<float64>( EditorComponentIcon::decodeGlyph( EditorComponentIcon::findObjectRow( *pObject )._pGlyph ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_PROBE( editorHierarchyVisibleRoots, "Editor.HierarchyVisibleRoots", "Root rows the Hierarchy showed in the last frame (after its filter)",
                         &readHierarchyVisibleRoots );
    SW_AUTOMATION_PROBE( editorHierarchyRowIcon, "Editor.HierarchyRowIcon", "Icon code point of the Hierarchy row of the object named by gv_editorProbeObject",
                         &readHierarchyRowIcon );
} // namespace sw::editor
