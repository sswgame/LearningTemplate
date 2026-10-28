/**
 * @file SettingsWatchs.cpp
 * @brief 설정 계열 패널(환경설정 · 단축키 · 모듈)의 자동화 감시 값입니다 — `Editor.Preferences*` · `Editor.Shortcut*` · `Editor.ModulePreviewNewlyInactive`.
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorWatchSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/Settings/ModulesPanel.h"
#include "Editor/Panels/Settings/PreferencesPanel.h"
#include "Editor/Panels/Settings/ShortcutsPanel.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 환경설정 창의 섹션 목록에서 지난 프레임에 보인 섹션 수입니다. 검색이 거른 뒤의 수입니다. */
        [[nodiscard]] bool readPreferencesVisibleSections( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( PreferencesPanel::getVisibleSectionCount() );
            return true;
        }

        /** @brief 환경설정 파일에 저장된 키 수입니다. 모든 섹션의 합이며, 기본과 다른 값만 저장됩니다. */
        [[nodiscard]] bool readPreferencesSavedKeyCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( EditorPreferencesStore::countSavedKeys( EditorPreferencesStore::getDefaultFilePath() ) );
            return true;
        }

        /** @brief 단축키 편집기가 현재 보고하는 충돌 수입니다. */
        [[nodiscard]] bool readShortcutConflictCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( ShortcutsPanel::getPendingConflictCount() );
            return true;
        }

        /** @brief 사용자 단축키 덮어쓰기 수입니다. 기본과 다른 커맨드의 수입니다. */
        [[nodiscard]] bool readShortcutOverrideCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( editor::getSubsystem<EditorContext>().getSubsystem<EditorInputActions>().countModifiedShortcuts() );
            return true;
        }

        /** @brief 모듈 창의 미리보기에서 새로 꺼지는 모듈 수입니다. */
        [[nodiscard]] bool readModulePreviewNewlyInactive( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( ModulesPanel::getPreviewNewlyInactiveCount() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorPreferencesVisibleSections, "Editor.PreferencesVisibleSections", "Sections the Preferences window listed in the last frame (after its search)",
                         &readPreferencesVisibleSections );
    SW_AUTOMATION_WATCH( editorPreferencesSavedKeyCount, "Editor.PreferencesSavedKeyCount", "Keys saved in EditorPreferences.json (only values that differ from the defaults)",
                         &readPreferencesSavedKeyCount );
    SW_AUTOMATION_WATCH( editorShortcutConflictCount, "Editor.ShortcutConflictCount", "Conflicts the Keyboard Shortcuts window is asking about",
                         &readShortcutConflictCount );
    SW_AUTOMATION_WATCH( editorShortcutOverrideCount, "Editor.ShortcutOverrideCount", "Commands whose shortcut the user changed",
                         &readShortcutOverrideCount );
    SW_AUTOMATION_WATCH( editorModulePreviewNewlyInactive, "Editor.ModulePreviewNewlyInactive", "Modules the Modules window's preview would turn off (0 when not asking)",
                         &readModulePreviewNewlyInactive );
} // namespace sw::editor
