/**
 * @file CharacterEditorCommands.cpp
 * @brief 캐릭터 편집 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 Character Editor 패널과 Paired Animation 창을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/CharacterEditorPanel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/PairedAnimationPanel.h"

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorCommandsInternal
        {
            static void openCharacterEditorPanel()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                if ( editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<CharacterEditorPanel>( true ) )
                    SW_LOG_INFO_ID( "CharacterEditorCommands.CharacterEditorPanelOpened", "Character Editor panel opened" );
            }

            static void openPairedAnimationPanel()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                if ( editorContext.getSubsystem<EditorPanelManager>().setPanelOpen( PairedAnimationPanel::kPanelID, true ) )
                    SW_LOG_INFO_ID( "CharacterEditorCommands.PairedAnimationPanelOpened", "Paired Animation panel opened" );
            }
        };
    } // namespace

    SW_EDITOR_COMMAND( CharacterEditorOpen, "charactereditor.openPanel", &CharacterEditorCommandsInternal::openCharacterEditorPanel, nullptr, NoScene );

    SW_EDITOR_COMMAND_TEXT( CharacterEditor, "charactereditor.commands", "editor/commands/charactereditor.commands.xml" ); // 이 모듈 명령의 표시 · 배치
    SW_EDITOR_COMMAND( PairedAnimationOpen, "charactereditor.openPairedAnimation", &CharacterEditorCommandsInternal::openPairedAnimationPanel, nullptr, NoScene );
} // namespace sw::editor
