/**
 * @file WindowTextInput.h
 * @brief 플랫폼 공통 입력기(IME) 인터페이스의 값입니다 - 창이 내는 조합 · 확정 글(`WindowTextInputEvent`)과, 입력기 프로토콜의 모양을 맞추는 버퍼 둘입니다.
 * @details 모양은 창 시스템마다 같다. 받는 쪽이 글을 원하는지 알리고(`IWindow::requestTextInput`, Win32 `ImmAssociateContextEx` · X11 `XSetICFocus` · Wayland `zwp_text_input_v3.enable` ·
 *          모바일이면 화면 키보드 올리기) 후보 창 자리를 알리면(`IWindow::setTextInputArea`), 창이 조합 중 글과 확정 글을 낸다. SDL `SDL_StartTextInput` · 언리얼 `ITextInputMethodSystem` · Godot `window_set_ime_active` 와 같은 자리다.
 *          OS 헤더가 없어 조합 → 확정 흐름을 단위 테스트가 가짜 입력기로 돌린다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 창이 내는 글의 종류입니다. */
    enum class WindowTextInputKind : uint8
    {
        Composition = 0, ///< 조합 중 글 전체입니다(확정 전, 빈 글이면 조합이 끝났거나 취소됐다).
        Commit,          ///< 확정된 글입니다. 입력기가 없어도 키가 낸 글자가 이것으로 온다.
    };

    /** @brief `NativeWindowEvent::kMessageTextInput` 의 `_lParam` 이 가리키는 값입니다. 글은 처리기가 도는 동안만 산다. */
    struct WindowTextInputEvent
    {
        WindowTextInputKind _kind{ WindowTextInputKind::Commit };
        string_view         _text{};
    };

    /** @brief 입력기를 원하는 쪽입니다(`IWindow::requestTextInput`). 둘이 따로 켜고 끄므로 한쪽이 끄는 일이 다른 쪽을 끄지 않는다. */
    enum class WindowTextInputClient : uint8
    {
        Engine = 0, ///< 런타임 UI 글자 입력 · 개발 콘솔(`InputManager::setKeyboardFocus`)
        Editor,     ///< 에디터 ImGui 글 칸(`Platform_SetImeDataFn`)
        Count,
    };

    /** @brief 글 커서의 자리입니다(키보드 포커스를 가진 창의 클라이언트 픽셀). 입력기가 후보 창을 그 줄 아래에 놓는다. */
    struct WindowTextInputArea
    {
        int2  _position{};      ///< 줄의 왼쪽 위
        int32 _lineHeight{ 0 }; ///< 줄 높이

        bool operator==( const WindowTextInputArea& other ) const { return _position == other._position && _lineHeight == other._lineHeight; }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 입력기가 조합 중 글을 조금씩 고쳐 알리는 모양(X11 XIM 의 `PreeditDraw`)을 글 전체로 바꿉니다.
     * @details 자리와 길이는 글자(코드 포인트) 단위이고 글은 UTF-8 로 듭니다.
     */
    class SW_API WindowTextComposition
    {
    public:
        /** @brief @p firstCharacter 자리에서 @p deleteCount 글자를 지우고 @p insertedText 를 넣습니다. 끝을 넘는 자리 · 길이는 끝에서 멈춘다(입력기의 값을 믿지 않는다). */
        void applyEdit( uint32 firstCharacter, uint32 deleteCount, string_view insertedText );
        void clear() { _text.clear(); }

        const string& getText() const { return _text; }
        bool          isEmpty() const { return _text.empty(); }

    private:
        /** @brief @p characterIndex 번째 글자의 바이트 자리입니다. 넘으면 글 끝입니다. */
        size_t findByteOffset( uint32 characterIndex ) const;

    private:
        string _text;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 이벤트를 모았다가 한 번에 적용하는 모양(Wayland `zwp_text_input_v3` 의 `done`)을 조합 · 확정으로 바꿉니다.
     * @details 프로토콜이 정한 순서다 - 조합을 지우고, 확정 글을 넣고, 새 조합을 넣는다. `done` 에 실리지 않은 조합은 빈 글이다(프로토콜의 초기값).
     */
    class SW_API WindowTextInputFrame
    {
    public:
        void setPendingPreedit( string_view text ) { _pendingPreedit.assign( text.data(), text.size() ); }
        void setPendingCommit( string_view text ) { _pendingCommit.assign( text.data(), text.size() ); }
        /**
         * @brief 모은 것을 적용합니다(`done`). 다음 묶음을 위해 비웁니다.
         * @param outCommit 확정 글입니다. 없으면 비어 있습니다.
         * @param outPreedit 지금 조합 중 글입니다.
         * @return 조합 중 글이 바뀌었으면 true 입니다.
         */
        [[nodiscard]] bool finish( string& outCommit, string& outPreedit );
        /** @brief 포커스를 잃어 조합이 없어졌습니다(`leave`). 조합 중이었으면 true 입니다. */
        [[nodiscard]] bool reset();

    private:
        string _preedit;
        string _pendingPreedit;
        string _pendingCommit;
    };
} // namespace sw
