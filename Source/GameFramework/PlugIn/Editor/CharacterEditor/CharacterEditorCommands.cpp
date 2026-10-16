/**
 * @file CharacterEditorCommands.cpp
 * @brief 캐릭터 편집 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 Character Editor 패널을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

namespace sw::editor
{
    namespace
    {
        void openCharacterEditorPanel()
        {
            EditorContext* pContext = EditorContext::get();
            if ( pContext != nullptr && pContext->getSubsystem<EditorPanelManager>().setPanelOpen( "charactereditor.main", true ) )
                SW_LOG_INFO( "Character Editor panel opened" );
        }
    } // namespace

    SW_EDITOR_COMMAND( CharacterEditorOpen, "charactereditor.openPanel", 9120, "Character Editor", editoricon::kSkeleton, "Character", "캐릭터 편집 패널을 엽니다",
                       "Open the character editor panel", {}, &openCharacterEditorPanel, nullptr, nullptr );
} // namespace sw::editor
