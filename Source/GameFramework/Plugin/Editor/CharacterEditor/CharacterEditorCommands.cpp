/**
 * @file CharacterEditorCommands.cpp
 * @brief 캐릭터 편집 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 Character Editor 패널과 Paired Animation 창을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include "Engine/Localization/LocalizationManager.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/CharacterEditorPanel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/PairedAnimationPanel.h"

namespace sw
{
    // 에디터 확장 CharacterEditor 의 글(엔진 현지화 폴더의 `editorplugins/charactereditor/`) — 이 모듈이 올라온 동안만 조회 사슬에 든다. 배포 팩에 담지 않는다.
    SW_LOCALIZATION_PROJECT( EditorPluginCharacterEditorText, "engine/localization/editorplugins/charactereditor/charactereditor.locproject.xml" );
} // namespace sw

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
