/**
 * @file X11InputMethod.h
 * @brief X 입력기(XIM)입니다 - 한글 조합 · 옛 키심 레이아웃(키릴 등)의 글을 `WindowTextInputKind` 로 냅니다. 헤더에 X11 타입을 두지 않아 어느 플랫폼의 `IWindow.cpp` 에서도 읽힌다.
 * @details 창(`X11Window`)이 이벤트 루프에서 부른다. 규칙은 GLFW · SDL · Godot 와 같다.
 *          - 입력기가 볼 이벤트는 처리기보다 먼저 `XFilterEvent` 를 지난다(`filterEvent`). 입력기가 가져간 키는 키 이벤트로 나가지 않는다.
 *          - 글은 키 누름마다 한 번만 읽는다(`emitKeyText`). `Xutf8LookupString` 은 확정 글을 꺼내는 호출이라 처리기마다(ImGui · 입력 관리자) 부르면 둘째가 빈 글을 받는다 - 그래서 창이 읽어 글 이벤트로 나눠 준다.
 *          - 입력기를 원하는 쪽이 없으면 키를 넘기지 않는다(`setWanted`). 글자는 그래도 나온다(문맥의 키보드 변환).
 *
 *          **입력기가 없는 것은 정상이다.** 로캘 자료가 없거나(Xvfb 의 최소 루트) 입력기 데몬이 아직 안 떴으면 문맥 없이 키심으로 글자를 읽는다. 데몬이 나중에 뜨면 열고(`XRegisterIMInstantiateCallback`), 죽으면 문맥을 버린다(`XNDestroyCallback`).
 *          조합을 그리는 일은 받는 쪽이 한다(`XIMPreeditCallbacks`). 입력기가 그 방식을 내지 않으면 입력기가 스스로 그리고(`XIMPreeditNothing`) 조합 글은 오지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"

#include "Engine/Window/WindowTextInput.h"

namespace sw
{
    SW_DECLARE_DELEGATE( void, X11InputMethodTextDelegate, WindowTextInputKind kind, string_view text );

    /** @class X11InputMethod @brief 연결 하나의 입력기와 입력 문맥입니다. C 콜백이 주소를 들므로 옮기지 않는다. */
    class X11InputMethod
    {
    public:
        X11InputMethod();

        X11InputMethod( const X11InputMethod& )            = delete;
        X11InputMethod& operator=( const X11InputMethod& ) = delete;

        /** @brief 입력기를 열고 문맥을 만듭니다(없으면 머리말대로 나중을 기다린다). @p clientWindow 는 주 창입니다. */
        void initialize( void* pDisplay, uint64 clientWindow, X11InputMethodTextDelegate textSink );
        /** @brief 문맥 · 입력기를 내립니다. 창과 연결을 닫기 전에 부른다. */
        void shutdown();

        /** @brief @p pEvent(`XEvent*`)를 입력기에 보입니다. true 면 입력기가 가져갔다 - 처리기에 넘기지 않는다. */
        [[nodiscard]] bool filterEvent( void* pEvent );
        /** @brief 키 누름 @p pKeyPressEvent(`XKeyPressedEvent*`)가 낸 글을 확정 글로 냅니다. 글이 없으면 아무것도 내지 않습니다. */
        void emitKeyText( void* pKeyPressEvent );
        /** @brief 우리 창(주 창 · 떼어 낸 창)의 포커스 변화입니다. 입력기는 포커스를 가진 창에서만 키를 받는다. */
        void onFocusChanged( uint64 window, bool bFocused );
        /** @brief 입력기를 켜거나 끕니다. 끌 때 조합 중이던 글은 버리고 빈 조합을 냅니다(Godot 와 같다 - 다른 칸으로 옮긴 뒤 옛 글이 들어가지 않는다). */
        void setWanted( bool bWanted );
        /** @brief 후보 창 자리(`XNSpotLocation`, 포커스를 가진 창 기준)를 알립니다. */
        void setArea( const WindowTextInputArea& area );

        // 아래는 .cpp 의 C 콜백만 부른다(XIM 타입을 헤더에 두지 않으려고 공개다).
        void onInputMethodAvailable();
        void onInputMethodDestroyed();
        void onPreeditEdited( uint32 firstCharacter, uint32 deleteCount, string_view insertedText );
        void onPreeditFinished();

    private:
        void createContext();
        void applyFocus();
        void applyArea();
        void emitCompositionCleared();
        /** @brief 문맥 없이 키심으로 글자를 읽습니다(입력기가 없을 때). */
        void emitKeySymText( void* pKeyPressEvent );

    private:
        X11InputMethodTextDelegate _textSink;
        WindowTextComposition      _composition;
        WindowTextInputArea        _area;
        void*                      _pDisplay;
        void*                      _pInputMethod;  ///< `XIM`
        void*                      _pInputContext; ///< `XIC`
        uint64                     _clientWindow;
        uint64                     _focusWindow; ///< 입력기에 알린 포커스 창
        uint8                      _bWanted             : 1;
        uint8                      _bFocused            : 1;
        uint8                      _bCallbackRegistered : 1;
    };
} // namespace sw
