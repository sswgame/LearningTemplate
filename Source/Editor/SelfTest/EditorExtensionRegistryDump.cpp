#include "pch.h"

#include "Editor/SelfTest/EditorExtensionRegistryDump.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorPopupManager.h"
#include "Editor/Common/GUI/EditorStatusBar.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

namespace sw::editor
{
    namespace
    {
        struct EditorExtensionRegistryDumpInternal
        {
            static const utf8* getCategoryName( EditorPanelCategory category )
            {
                switch ( category )
                {
                    case EditorPanelCategory::Core:
                        return "Core";
                    case EditorPanelCategory::Tool:
                        return "Tool";
                    case EditorPanelCategory::Custom:
                        return "Custom";
                }
            }

            /** @brief 레지스트리의 id 를 순서대로 한 줄씩 남깁니다. */
            template <typename TRegistration>
            static void dumpIDs()
            {
                using Registry = EditorExtensionRegistry<TRegistration>;
                for ( uint32 index = 0; index < Registry::getCount(); ++index )
                {
                    SW_LOG_INFO( "EditorExtensionRegistry|%#|%#", TRegistration::kKindName, Registry::getAt( index )._pID );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorExtensionRegistryDump" );

    // 이 파일만 읽으므로 여기서 정의한다(헤더에 선언하지 않는다).
    /** @brief `-gv_editorRegistryDump=1`: 시작할 때 에디터 레지스트리(패널 · 팝업 · 인스펙터 · 시각화 · 프레임 단계 · 커맨드 메뉴)를 한 줄씩 덤프합니다. */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_editorRegistryDump, false, "시작할 때 에디터 레지스트리(패널 · 팝업 · 인스펙터 · 시각화 · 메뉴)를 로그로 덤프" );

    void EditorExtensionRegistryDump::dumpIfRequested()
    {
        if ( gv_editorRegistryDump == false )
            return;

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        // 패널은 등록 줄이 아니라 매니저가 만든 인스턴스를 본다 — 메뉴에 보이는 제목까지 같이 남긴다.
        for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
        {
            SW_LOG_INFO( "EditorExtensionRegistry|panel|%#|%#|%#", entry._id.c_str(), EditorExtensionRegistryDumpInternal::getCategoryName( entry._category ),
                         entry._title.c_str() );
        }
        EditorExtensionRegistryDumpInternal::dumpIDs<EditorPopupRegistration>();
        EditorExtensionRegistryDumpInternal::dumpIDs<EditorInspectorRegistration>();
        EditorExtensionRegistryDumpInternal::dumpIDs<EditorVisualizerRegistration>();
        EditorExtensionRegistryDumpInternal::dumpIDs<EditorStatusBarItemRegistration>();
        EditorExtensionRegistryDumpInternal::dumpIDs<EditorMultiplayerTabRegistration>();
        EditorExtensionRegistryDumpInternal::dumpIDs<EditorFrameStepRegistration>();

        const EditorCommandRegistry& registry = pContext->getCommandRegistry();
        for ( const EditorMenu& menu : registry.getMenus() )
        {
            string itemText;
            for ( const EditorMenuItem& item : menu._listItem )
            {
                if ( itemText.empty() == false )
                    itemText += ',';
                if ( item._bSeparatorBefore )
                    itemText += "-,";
                itemText += registry.getCommands()[item._commandIndex]._id;
            }
            SW_LOG_INFO( "EditorExtensionRegistry|menu|%#|%#", menu._path.c_str(), itemText.c_str() );
        }
    }
} // namespace sw::editor
