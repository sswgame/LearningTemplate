/**
 * @file ImGuiTextInputBridge.h
 * @brief 창의 입력기(`IWindow::requestTextInput` · `kMessageTextInput`)를 ImGui 에 잇습니다. X11 · Wayland 플랫폼 백엔드가 함께 쓴다.
 * @details Win32 는 ImGui 내장(`Platform_SetImeDataFn` 기본 구현, IMM32)과 OS 의 조합 창이 맡아 이 다리가 없다. X11 · Wayland 는 조합 글을 받는 쪽이 그려야 하는데(`XIMPreeditCallbacks` · `zwp_text_input_v3`)
 *          ImGui 글 칸은 조합 글을 모르므로 글 커서 자리에 덧그린다(SDL · GLFW 예제의 조합 표시와 같은 자리). 조합이 없으면 그리는 것이 없어 스크린샷이 바뀌지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

struct ImGuiPlatformImeData;
struct ImGuiViewport;

namespace sw
{
    struct NativeWindowEvent;

    class IWindow;
} // namespace sw

namespace sw::editor
{
    /** @class ImGuiTextInputBridge @brief 파일 머리말 참고. */
    class ImGuiTextInputBridge
    {
    public:
        ImGuiTextInputBridge();

        /** @brief `Platform_SetImeDataFn` 을 겁니다. */
        void initialize( IWindow& window );
        /** @brief 건 것을 풉니다. 초기화한 적이 없어도 안전하다. */
        void shutdown();
        /** @brief @p event 가 글 이벤트면 ImGui 에 넣고 true 입니다. */
        [[nodiscard]] bool processEvent( const NativeWindowEvent& event );
        /** @brief 조합 중 글을 글 커서 자리에 그립니다. `ImGui::Render` 앞에 부른다. */
        void drawComposition() const;

        /** @brief ImGui 가 글 커서를 옮기거나 글 칸을 켜고 끌 때 부릅니다(.cpp 의 콜백만 부른다). */
        void onImeData( const ImGuiViewport& viewport, const ImGuiPlatformImeData& data );

    private:
        IWindow* _pWindow;
        string   _composition; ///< 조합 중 글(UTF-8)
        float32  _inputX;      ///< 글 커서(ImGui 좌표)
        float32  _inputY;
        float32  _lineHeight;
        uint32   _viewportID; ///< 글 커서가 있는 뷰포트(`ImGuiID`)
    };
} // namespace sw::editor
