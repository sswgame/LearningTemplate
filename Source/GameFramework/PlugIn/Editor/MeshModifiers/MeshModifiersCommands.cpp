/**
 * @file MeshModifiersCommands.cpp
 * @brief 메시 모디파이어 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 Mesh Modifiers 패널을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/MeshModifiersPanel.h"

namespace sw::editor
{
    namespace
    {
        void openMeshModifiersPanel()
        {
            EditorContext* pContext = EditorContext::get();
            if ( pContext != nullptr && pContext->getSubsystem<EditorPanelManager>().setPanelOpen<MeshModifiersPanel>( true ) )
                SW_LOG_INFO( "Mesh Modifiers panel opened" );
        }
    } // namespace

    SW_EDITOR_COMMAND( MeshModifiersOpen, "meshmodifiers.openPanel", 9110, "Mesh Modifiers", editoricon::kCube, "Mesh", "메시 모디파이어 스택 패널을 엽니다",
                       "Open the mesh modifier stack panel", {}, &openMeshModifiersPanel, nullptr, NoScene, nullptr );
} // namespace sw::editor
