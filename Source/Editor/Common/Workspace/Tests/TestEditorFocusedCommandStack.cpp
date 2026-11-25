// EditorFocusedCommandStackTest — Ctrl+Z 가 갈 스택을 처리하는 순간의 포커스 창으로 고른다(`IEditorPanelService::chooseFocusedCommandStack`, 감사 E-3).
// 예전에는 패널 관리자가 그린 뒤 걸어 둔 스택을 썼다 — 포커스를 옮긴 바로 그 프레임의 Ctrl+Z 가 앞 창의 스택으로 갔다.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Workspace/IEditorPanelService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorFocusedCommandStackTestInternal
    {
        static constexpr uint32 kDocumentWindow = 11; ///< 자기 스택을 가진 문서 패널 창
        static constexpr uint32 kSceneWindow    = 22; ///< 메인 스택을 답하는 창(씬 뷰)
        static constexpr uint32 kHistoryWindow  = 33; ///< 겨눈 스택을 지키는 창(History)
        static constexpr uint32 kMenuWindow     = 44; ///< 패널이 아닌 창(메뉴 · 팝업)
    };
} // namespace

/** @brief [EditorFocusedCommandStackTest] 포커스 창이 패널이면 그 답, 메인을 답하면 메인이다 — 걸어 둔 스택(앞 프레임의 포커스)보다 앞선다 */
SW_TEST_CASE( EditorFocusedCommandStackTest, FocusedPanelWinsOverTheRegisteredStack )
{
    using Internal = EditorFocusedCommandStackTestInternal;
    CommandStack mainStack{
        sw::UndoBufferLimit{ 128u, 1ull << 20 }
    };
    CommandStack documentStack{
        sw::UndoBufferLimit{ 128u, 1ull << 20 }
    };

    const vector<EditorPanelCommandStack> listPanel{
        EditorPanelCommandStack{Internal::kDocumentWindow, &documentStack, false,   "document"},
        EditorPanelCommandStack{   Internal::kSceneWindow,        nullptr, false, "scene_view"},
        EditorPanelCommandStack{ Internal::kHistoryWindow,        nullptr,  true,    "history"},
    };
    SW_EXPECT_TRUE( IEditorPanelService::chooseFocusedCommandStack( Internal::kDocumentWindow, listPanel, "other_document", &mainStack ) == &documentStack );
    SW_EXPECT_TRUE( IEditorPanelService::chooseFocusedCommandStack( Internal::kSceneWindow, listPanel, "other_document", &mainStack ) == &mainStack );
}

/** @brief [EditorFocusedCommandStackTest] History · 메뉴 · 포커스 없음은 걸어 둔 스택(앞 포커스 패널)을 쓰고, 그것도 없으면 메인이다 */
SW_TEST_CASE( EditorFocusedCommandStackTest, NonPanelFocusKeepsTheRegisteredStack )
{
    using Internal = EditorFocusedCommandStackTestInternal;
    CommandStack mainStack{
        sw::UndoBufferLimit{ 128u, 1ull << 20 }
    };
    CommandStack documentStack{
        sw::UndoBufferLimit{ 128u, 1ull << 20 }
    };

    const vector<EditorPanelCommandStack> listPanel{
        EditorPanelCommandStack{Internal::kDocumentWindow, &documentStack, false, "document"},
        EditorPanelCommandStack{ Internal::kHistoryWindow,        nullptr,  true,  "history"},
    };
    SW_EXPECT_TRUE( IEditorPanelService::chooseFocusedCommandStack( Internal::kHistoryWindow, listPanel, "document", &mainStack ) == &documentStack );
    SW_EXPECT_TRUE( IEditorPanelService::chooseFocusedCommandStack( Internal::kMenuWindow, listPanel, "document", &mainStack ) == &documentStack );
    SW_EXPECT_TRUE( IEditorPanelService::chooseFocusedCommandStack( 0, listPanel, "document", &mainStack ) == &documentStack );
    SW_EXPECT_TRUE( IEditorPanelService::chooseFocusedCommandStack( Internal::kMenuWindow, listPanel, "", &mainStack ) == &mainStack );
}

/** @brief [EditorFocusedCommandStackTest] 앞 포커스 패널이 닫혀 목록에 없으면 메인이다 — 스택 포인터를 기억하지 않아 닫힌 문서 · 사라진 스택을 겨누지 않는다 */
SW_TEST_CASE( EditorFocusedCommandStackTest, ClosedRememberedPanelFallsBackToMain )
{
    using Internal = EditorFocusedCommandStackTestInternal;
    CommandStack mainStack{
        sw::UndoBufferLimit{ 128u, 1ull << 20 }
    };
    const vector<EditorPanelCommandStack> listPanel{
        EditorPanelCommandStack{ Internal::kHistoryWindow, nullptr, true, "history" },
    };
    SW_EXPECT_TRUE( IEditorPanelService::chooseFocusedCommandStack( Internal::kHistoryWindow, listPanel, "document", &mainStack ) == &mainStack );
    SW_EXPECT_TRUE( IEditorPanelService::chooseFocusedCommandStack( Internal::kMenuWindow, listPanel, "document", &mainStack ) == &mainStack );
}
