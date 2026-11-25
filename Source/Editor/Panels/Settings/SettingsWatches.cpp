/**
 * @file SettingsWatches.cpp
 * @brief 설정 계열 패널(환경설정 · 단축키 · 모듈 · 전역 변수)의 자동화 감시 값입니다 — `Editor.Preferences*` · `Editor.Shortcut*` · `Editor.ModulePreviewNewlyInactive` · `Editor.GlobalVariablesRebuildCount`.
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorWatchSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Panels/Settings/GlobalVariablesPanel.h"
#include "Editor/Panels/Settings/Model/ShortcutsViewModel.h"
#include "Editor/Panels/Settings/ModulesPanel.h"
#include "Editor/Panels/Settings/PreferencesPanel.h"
#include "Editor/Panels/Settings/ShortcutsPanel.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Reflection/TypeInfo.h"

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
            outValue = static_cast<float64>( ShortcutsViewModel::countModifiedShortcuts() );
            return true;
        }

        /** @brief 모듈 창의 미리보기에서 새로 꺼지는 모듈 수입니다. */
        [[nodiscard]] bool readModulePreviewNewlyInactive( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( ModulesPanel::getPreviewNewlyInactiveCount() );
            return true;
        }

        /**
         * @brief 전역 변수 창이 보일 목록을 다시 지은 횟수입니다. 창이 없으면(에디터 컨텍스트 없음 · 창 인스턴스 없음) 값을 내지 않습니다.
         * @details 창은 등록 세대 · 검색 글이 바뀐 프레임에만 목록을 짓는다. 창을 연 채 몇 프레임 지나도 이 수가 늘지 않으면 매 프레임 재구성이 없다.
         */
        [[nodiscard]] bool readGlobalVariablesRebuildCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const GlobalVariablesPanel* pPanel = EditorPanelManager::findPanelInEditorContext<GlobalVariablesPanel>();
            if ( pPanel == nullptr )
                return false;
            outValue = static_cast<float64>( pPanel->getListRebuildCount() );
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
    SW_AUTOMATION_WATCH( editorGlobalVariablesRebuildCount, "Editor.GlobalVariablesRebuildCount",
                         "Times the Global Variables panel rebuilt its visible variable list (grows only when registrations or the search text change)",
                         &readGlobalVariablesRebuildCount );
} // namespace sw::editor
