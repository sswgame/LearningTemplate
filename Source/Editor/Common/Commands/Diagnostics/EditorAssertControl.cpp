/**
 * @file EditorAssertControl.cpp
 * @brief 단언 대화상자의 에디터 제어입니다 — 환경설정 Editor/Diagnostics 의 등록, 바뀐 값을 Core 에 입히기, 기억한 "늘 무시" 자리 지우기 명령, 감시 값.
 * @details 대화상자를 거는 것은 개발 도구다(실행 종류로 정한다 — `DevTools/Client/Diagnostics/AssertDialog`). 여기서는 걸린 대화상자를 띄울지와 "늘 무시" 를 기억할지만 정한다.
 *          설정은 모든 구성에 있고, 기억한 자리는 대화상자가 있는 검사 구성에만 있다(배포는 0 이다).
 */
#include "pch.h"

#include "Core/Foundation/Macros.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAssertControlInternal
        {
            /** @brief 환경설정 값을 Core 의 대화상자 설정에 입힙니다. 창에서 고쳤거나 파일을 읽었거나 섹션을 되돌렸을 때 불린다. */
            static void onDiagnosticsChanged()
            {
                const EditorDiagnosticsPreferences& preferences = getPreferences<EditorDiagnosticsPreferences>();
                ::sw::internal::setAssertDialogOptions( preferences._bShowAssertDialog, preferences._bRememberIgnoredAsserts );
            }

            /** @brief 기억한 "늘 무시" 자리 수입니다. 대화상자가 없는 구성은 0 입니다. */
            static uint32 countIgnoredSites()
            {
#if defined( SW_DO_CHECKS )
                return ::sw::internal::getIgnoredAssertSiteCount();
#else
                return 0;
#endif
            }

            /** @brief `assert.clearIgnored` — 기억한 "늘 무시" 자리를 모두 잊는다. 다음 단언부터 다시 묻는다. */
            static bool runClearIgnored( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const uint32 count = countIgnoredSites();
#if defined( SW_DO_CHECKS )
                ::sw::internal::clearIgnoredAssertSites();
#endif
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "forgot %# ignored assert site(s)", count );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief 감시 값 `Editor.AssertDialogShown` — 걸린 대화상자를 띄우는 설정이면 1 입니다. */
            [[nodiscard]] static bool readAssertDialogShown( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = ::sw::internal::isAssertDialogShown() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `Editor.AssertIgnoreRemembered` — "늘 무시" 를 기억하는 설정이면 1 입니다. */
            [[nodiscard]] static bool readAssertIgnoreRemembered( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = ::sw::internal::isIgnoredAssertSiteRemembered() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `Editor.AssertIgnoredSites` — 기억한 "늘 무시" 자리 수입니다. */
            [[nodiscard]] static bool readAssertIgnoredSites( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( countIgnoredSites() );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_SETTINGS( EditorDiagnosticsPreferences, "diagnostics", "Editor/Diagnostics", 380, &EditorAssertControlInternal::onDiagnosticsChanged );

    SW_DEV_COMMAND( AssertClearIgnored, "assert.clearIgnored", "assert.clearIgnored",
                    "Forget every assert site the assert dialog was told to ignore for the rest of the run (Cancel) - the next failure asks again",
                    &EditorAssertControlInternal::runClearIgnored );

    SW_AUTOMATION_WATCH( editorAssertDialogShown, "Editor.AssertDialogShown",
                         "1 when an installed assert dialog asks (preference Editor/Diagnostics), 0 when asserts stop without asking",
                         &EditorAssertControlInternal::readAssertDialogShown );
    SW_AUTOMATION_WATCH( editorAssertIgnoreRemembered, "Editor.AssertIgnoreRemembered",
                         "1 when Cancel in the assert dialog ignores that site for the rest of the run (preference Editor/Diagnostics)",
                         &EditorAssertControlInternal::readAssertIgnoreRemembered );
    SW_AUTOMATION_WATCH( editorAssertIgnoredSites, "Editor.AssertIgnoredSites", "Assert sites the dialog remembers to ignore for the rest of the run",
                         &EditorAssertControlInternal::readAssertIgnoredSites );
} // namespace sw::editor
