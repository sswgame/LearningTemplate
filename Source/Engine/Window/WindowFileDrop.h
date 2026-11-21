/**
 * @file WindowFileDrop.h
 * @brief OS 파일 끌어 놓기(탐색기 · 파일 관리자에서 창으로 파일을 끌어 놓기)의 플랫폼 공통 값입니다 — 창이 내는 사건(`WindowFileDropEvent`)과 창의 능력(`WindowFileDropSupport`).
 * @details 창 시스템마다 프로토콜이 다르다: Win32 `WM_DROPFILES`(`DragAcceptFiles` — 놓은 순간만, 메인 스레드가 MTA 라 OLE `IDropTarget` 을 쓰지 않는다), X11 XDND(들어옴 · 위치 · 놓기 · 나감),
 *          Wayland `wl_data_device`(enter · motion · drop · leave). 받는 쪽은 사건 하나로 받는다(SDL `SDL_EVENT_DROP_*` · GLFW `glfwSetDropCallback` · Godot `files_dropped` 자리).
 *          경로는 **믿지 않는 바깥 입력**이다 — 받는 쪽이 정규화 · 확장자 · 크기를 검사한다(에디터 `ContentBrowserFileDropPlan`). 원시 입력 큐(`RawInputEvent`)에 싣지 않는다 — 길이가 정해지지 않은 글이고 리플레이 대상이 아니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 창이 파일 끌어 놓기를 어디까지 받는지입니다(능력 표 — `GenericWindow::getFileDropSupport`). */
    enum class WindowFileDropSupport : uint8
    {
        None = 0, ///< 받지 않는다(웹 · Android · 머리 없는 실행 — 창 시스템에 그런 입력이 없거나 아직 잇지 않았다)
        DropOnly, ///< 놓은 순간만 온다(Win32 `WM_DROPFILES`) — 끄는 동안의 미리보기는 없다
        Hover,    ///< 들어옴 · 위치 · 나감도 온다(X11 XDND · Wayland) — 끄는 동안 받는 쪽이 놓을 자리를 보일 수 있다
    };

    /** @brief 끌어 놓기 사건의 단계입니다. */
    enum class WindowFileDropPhase : uint8
    {
        Enter = 0, ///< 파일을 끈 포인터가 창에 들어왔다(경로는 아직 없다 — 놓기 전에는 데이터를 받지 않는다)
        Over,      ///< 창 안에서 움직였다(경로 없음)
        Leave,     ///< 놓지 않고 나갔다
        Drop,      ///< 놓았다 — `_listPath` 가 놓은 파일의 디스크 경로다
    };

    /**
     * @struct WindowFileDropEvent
     * @brief 창이 내는 끌어 놓기 사건 하나입니다. 처리기(`GenericWindow::bindFileDropHandler`)가 도는 동안만 산다.
     */
    struct WindowFileDropEvent
    {
        WindowFileDropPhase _phase{ WindowFileDropPhase::Drop };
        int32               _x{ 0 };     ///< 창 클라이언트 좌표(픽셀). 놓은 자리 · 지금 자리
        int32               _y{ 0 };     ///< 위와 같다
        vector<string>      _listPath{}; ///< `Drop` 의 파일 경로(OS 가 준 그대로 — 믿지 않는다)
    };

    SW_DECLARE_DELEGATE( void, WindowFileDropDelegate, const WindowFileDropEvent& event );

    /**
     * @struct WindowFileDropUtil
     * @brief 창 시스템이 주는 글을 경로로 바꾸는 순수 함수입니다(OS 헤더 없음 — 단위 테스트가 본다).
     */
    struct SW_API WindowFileDropUtil
    {
        /**
         * @brief `text/uri-list`(RFC 2483 — X11 XDND · Wayland 가 주는 형식)를 로컬 파일 경로로 바꿔 @p outListPath 끝에 덧붙입니다.
         * @details 줄은 CRLF 또는 LF, `#` 줄은 주석이다. `file://` 만 받는다(`file:///경로` 와 `file://localhost/경로`) — 다른 호스트 · 다른 스킴(http 등)은 버린다.
         *          `%XX` 를 풀고, 풀린 글에 NUL 이 있거나 `%` 뒤가 16진이 아니면 그 줄을 버린다. 빈 경로도 버린다.
         * @return 버린 줄 수입니다(받는 쪽이 거절 사유로 알린다).
         */
        static uint32 parseURIList( string_view text, vector<string>& outListPath );
    };
} // namespace sw
