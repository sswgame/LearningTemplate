/**
 * @file CharacterEditorCommands.cpp
 * @brief 캐릭터 편집 확장의 커맨드를 등록합니다. 메뉴(Panel)와 팔레트에서 Character Editor 패널과 Paired Animation 창을 엽니다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
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
                    SW_LOG_INFO( "Character Editor panel opened" );
            }

            static void openPairedAnimationPanel()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                if ( editorContext.getSubsystem<EditorPanelManager>().setPanelOpen( PairedAnimationPanel::kPanelID, true ) )
                    SW_LOG_INFO( "Paired Animation panel opened" );
            }
        };
    } // namespace

    SW_EDITOR_COMMAND( CharacterEditorOpen, "charactereditor.openPanel", 9120, "Character Editor", editoricon::kSkeleton, "Character", "캐릭터 편집 패널을 엽니다",
                       "Open the character editor panel", nullptr, &CharacterEditorCommandsInternal::openCharacterEditorPanel, nullptr, NoScene, nullptr, None, 0 );
    SW_EDITOR_COMMAND( PairedAnimationOpen, "charactereditor.openPairedAnimation", 9121, "Paired Animation", editoricon::kSkeleton, "Character",
                       "페어드 애니메이션 편집 창을 엽니다", "Open the paired animation editor (roles on one clock, alignment offsets, shared notifies)", nullptr,
                       &CharacterEditorCommandsInternal::openPairedAnimationPanel, nullptr, NoScene, nullptr, None, 0 );
} // namespace sw::editor
