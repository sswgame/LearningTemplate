#include "pch.h"

#include "Engine/Window/Linux/X11InputMethod.h"

#include "Core/Foundation/Constants.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/String/StringUtil.h"

#if defined( SW_WITH_CLIENT_CODE )
    #include "Core/Foundation/X11Headers.h"

    #include <clocale>
    #include <cwchar>

    // XIM 의 wide_char 와 로캘 변환(`mbrtowc`)의 wchar_t 를 유니코드 코드 포인트로 읽는다 — glibc 는 늘 그렇다. 아닌 C 라이브러리면 컴파일에서 멈춘다.
    #if !defined( __STDC_ISO_10646__ )
        #error "X11InputMethod reads wchar_t as Unicode code points - this C library does not promise __STDC_ISO_10646__"
    #endif

namespace sw
{
    namespace
    {
        /** @brief Xlib 이 부르는 C 콜백들입니다. 사용자 값이 `X11InputMethod*` 입니다. */
        struct X11InputMethodInternal
        {
            static X11InputMethod& getSelf( XPointer pClient ) { return *reinterpret_cast<X11InputMethod*>( pClient ); }

            static void onInstantiate( Display* /*pDisplay*/, XPointer pClient, XPointer /*pCall*/ ) { getSelf( pClient ).onInputMethodAvailable(); }

            static void onDestroyed( XIM /*pIM*/, XPointer pClient, XPointer /*pCall*/ ) { getSelf( pClient ).onInputMethodDestroyed(); }

            /** @brief 조합 시작입니다. 반환값은 조합 글의 최대 길이이고 -1 은 제한 없음이다(XIM 규약). */
            static int32 onPreeditStart( XIM /*pIM*/, XPointer pClient, XPointer /*pCall*/ )
            {
                getSelf( pClient ).onPreeditFinished(); // 전 조합이 남았으면 지운다
                return -1;
            }

            static void onPreeditDone( XIM /*pIM*/, XPointer pClient, XPointer /*pCall*/ ) { getSelf( pClient ).onPreeditFinished(); }

            /** @brief 조합 글 안의 커서는 쓰지 않는다 - 받는 쪽은 글 전체만 받는다(`WindowTextInputEvent`). 콜백은 있어야 한다(XCreateIC 가 넷을 요구한다). */
            static void onPreeditCaret( XIM /*pIM*/, XPointer /*pClient*/, XPointer /*pCall*/ ) {}

            /**
             * @brief 조합 글의 일부가 바뀌었습니다.
             * @details 다중 바이트 글은 로캘 인코딩이다 — 로캘 변환으로 UTF-8 로 바꾼다(`appendLocaleText`). 예전에는 UTF-8 로 가정해 EUC-KR 같은 로캘의 조합 글이 깨졌다
             *          (확정 글은 `Xutf8LookupString` 이라 처음부터 UTF-8 이다).
             */
            static void onPreeditDraw( XIM /*pIM*/, XPointer pClient, XIMPreeditDrawCallbackStruct* pCall )
            {
                if ( pCall == nullptr )
                    return;
                string inserted;
                if ( pCall->text != nullptr )
                {
                    const XIMText& text = *pCall->text;
                    if ( text.encoding_is_wchar != 0 && text.string.wide_char != nullptr )
                    {
                        for ( uint32 index = 0; index < text.length; ++index )
                        {
                            StringUtil::appendUTF8( inserted, static_cast<uint32>( text.string.wide_char[index] ) );
                        }
                    }
                    else if ( text.encoding_is_wchar == 0 && text.string.multi_byte != nullptr )
                        (void)X11InputMethod::appendLocaleText( text.string.multi_byte, inserted ); // 틀린 바이트는 함수가 오류로 남겼다 — 그 앞까지만 조합 글이다
                }
                const uint32 first = pCall->chg_first > 0 ? static_cast<uint32>( pCall->chg_first ) : 0;
                const uint32 count = pCall->chg_length > 0 ? static_cast<uint32>( pCall->chg_length ) : 0;
                getSelf( pClient ).onPreeditEdited( first, count, inserted );
            }

            template <typename TFunction>
            static XIMCallback makeCallback( X11InputMethod* pSelf, TFunction function )
            {
                XIMCallback callback{};
                callback.client_data = reinterpret_cast<XPointer>( pSelf );
                callback.callback    = reinterpret_cast<XIMProc>( function );
                return callback;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool X11InputMethod::appendLocaleText( const char* pMultiByte, string& outUTF8 )
    {
        if ( pMultiByte == nullptr )
            return true;
        std::mbstate_t state{};
        const size_t   length = string_view{ pMultiByte }.size();
        size_t         offset = 0;
        while ( offset < length )
        {
            wchar_t      character = 0;
            const size_t consumed  = std::mbrtowc( &character, pMultiByte + offset, length - offset, &state );
            const bool   bInvalid  = consumed == static_cast<size_t>( -1 ) || consumed == static_cast<size_t>( -2 ) || consumed == 0;
            if ( bInvalid )
            {
                SW_LOG_ERROR_ID( "X11InputMethod.BadLocaleText", "Input method text is not valid in the locale '%#' at byte %# - the rest of the composition is dropped",
                                 std::setlocale( LC_CTYPE, nullptr ), static_cast<uint32>( offset ) );
                return false;
            }
            StringUtil::appendUTF8( outUTF8, static_cast<uint32>( character ) );
            offset += consumed;
        }
        return true;
    }

    X11InputMethod::X11InputMethod()
        : _textSink{}
        , _composition{}
        , _area{}
        , _pDisplay{ nullptr }
        , _pInputMethod{ nullptr }
        , _pInputContext{ nullptr }
        , _clientWindow{ 0 }
        , _focusWindow{ 0 }
        , _bWanted{ SW_FALSE }
        , _bFocused{ SW_FALSE }
        , _bCallbackRegistered{ SW_FALSE }
    {
    }

    void X11InputMethod::initialize( void* pDisplay, uint64 clientWindow, X11InputMethodTextDelegate textSink )
    {
        _pDisplay     = pDisplay;
        _clientWindow = clientWindow;
        _focusWindow  = clientWindow;
        _textSink     = std::move( textSink );

        // XIM 은 C 로캘에서 조합을 하지 않는다. 글자 분류만 사용자 로캘로 바꾼다(LC_NUMERIC 은 그대로 - 소수점 읽기에 닿지 않는다). GLFW · Godot 도 창을 열 때 같은 호출을 한다.
        (void)std::setlocale( LC_CTYPE, "" );
        if ( XSupportsLocale() == 0 )
        {
            SW_LOG_INFO_ID( "X11InputMethod.LocaleNotSupported", "Xlib does not support this locale - keys type characters without an input method" );
            return;
        }
        (void)XSetLocaleModifiers( "" ); // XMODIFIERS(@im=…)를 읽는다
        onInputMethodAvailable();
        // 데몬이 아직 안 떴어도, 나중에 다시 떠도 연다.
        _bCallbackRegistered = XRegisterIMInstantiateCallback( static_cast<Display*>( _pDisplay ), nullptr, nullptr, nullptr, &X11InputMethodInternal::onInstantiate, reinterpret_cast<XPointer>( this ) ) != 0 ? SW_TRUE : SW_FALSE;
    }

    void X11InputMethod::shutdown()
    {
        if ( _pDisplay == nullptr )
            return;
        if ( _bCallbackRegistered )
            (void)XUnregisterIMInstantiateCallback( static_cast<Display*>( _pDisplay ), nullptr, nullptr, nullptr, &X11InputMethodInternal::onInstantiate, reinterpret_cast<XPointer>( this ) );
        _bCallbackRegistered = SW_FALSE;
        if ( _pInputContext != nullptr )
            XDestroyIC( static_cast<XIC>( _pInputContext ) );
        _pInputContext = nullptr;
        if ( _pInputMethod != nullptr )
            XCloseIM( static_cast<XIM>( _pInputMethod ) );
        _pInputMethod = nullptr;
        _composition.clear();
        _bFocused = SW_FALSE;
        _pDisplay = nullptr;
    }

    void X11InputMethod::onInputMethodAvailable()
    {
        if ( _pInputMethod != nullptr )
            return;
        XIM const pIM = XOpenIM( static_cast<Display*>( _pDisplay ), nullptr, nullptr, nullptr );
        if ( pIM == nullptr )
            return; // 머리말: 없는 것이 정상이다. 뜨면 콜백이 다시 부른다
        _pInputMethod       = pIM;
        XIMCallback destroy = X11InputMethodInternal::makeCallback( this, &X11InputMethodInternal::onDestroyed );
        (void)XSetIMValues( pIM, XNDestroyCallback, &destroy, nullptr );
        createContext();
    }

    void X11InputMethod::onInputMethodDestroyed()
    {
        // 서버 쪽이 이미 없다 - XDestroyIC · XCloseIM 을 부르지 않는다(Xlib 규약). 그 뒤로는 문맥 없이 키심으로 글자를 읽는다.
        _pInputMethod  = nullptr;
        _pInputContext = nullptr;
        SW_LOG_INFO_ID( "X11InputMethod.InputMethodGone", "The X input method went away - keys type characters without it until it returns" );
        if ( _composition.isEmpty() == false )
            emitCompositionCleared();
    }

    void X11InputMethod::createContext()
    {
        using Internal = X11InputMethodInternal;
        XIM const  pIM = static_cast<XIM>( _pInputMethod );
        XIMStyles* pStyles{ nullptr };
        if ( XGetIMValues( pIM, XNQueryInputStyle, &pStyles, nullptr ) != nullptr || pStyles == nullptr )
            return;
        const XIMStyle kCallbackStyle = XIMPreeditCallbacks | XIMStatusNothing;
        const XIMStyle kNothingStyle  = XIMPreeditNothing | XIMStatusNothing;
        bool           bHasCallback   = false;
        bool           bHasNothing    = false;
        for ( uint32 index = 0; index < pStyles->count_styles; ++index )
        {
            bHasCallback = bHasCallback || pStyles->supported_styles[index] == kCallbackStyle;
            bHasNothing  = bHasNothing || pStyles->supported_styles[index] == kNothingStyle;
        }
        XFree( pStyles );

        XIC pContext{ nullptr };
        if ( bHasCallback )
        {
            XIMCallback   start   = Internal::makeCallback( this, &Internal::onPreeditStart );
            XIMCallback   done    = Internal::makeCallback( this, &Internal::onPreeditDone );
            XIMCallback   draw    = Internal::makeCallback( this, &Internal::onPreeditDraw );
            XIMCallback   caret   = Internal::makeCallback( this, &Internal::onPreeditCaret );
            XVaNestedList preedit = XVaCreateNestedList( 0, XNPreeditStartCallback, &start, XNPreeditDoneCallback, &done, XNPreeditDrawCallback, &draw, XNPreeditCaretCallback, &caret, nullptr );
            pContext              = XCreateIC( pIM, XNInputStyle, kCallbackStyle, XNClientWindow, static_cast<Window>( _clientWindow ), XNFocusWindow, static_cast<Window>( _focusWindow ), XNPreeditAttributes, preedit, nullptr );
            XFree( preedit );
        }
        else if ( bHasNothing )
            pContext = XCreateIC( pIM, XNInputStyle, kNothingStyle, XNClientWindow, static_cast<Window>( _clientWindow ), XNFocusWindow, static_cast<Window>( _focusWindow ), nullptr );

        if ( pContext == nullptr )
        {
            SW_LOG_INFO_ID( "X11InputMethod.NoInputContext", "The X input method offers no usable input style - keys type characters without it" );
            return;
        }
        _pInputContext = pContext;
        SW_LOG_INFO_ID( "X11InputMethod.InputMethodOpened", "X input method opened (composition text %#)", bHasCallback ? "comes to the application" : "is drawn by the input method" );
        applyFocus();
        applyArea();
    }

    bool X11InputMethod::filterEvent( void* pEvent )
    {
        XEvent&    event = *static_cast<XEvent*>( pEvent );
        const bool bKey  = event.type == KeyPress || event.type == KeyRelease;
        // 입력기의 프로토콜 메시지(ClientMessage 등)는 늘 보인다. 키만 원할 때 보인다 - 꺼진 동안 게임 키가 조합에 먹히지 않는다.
        if ( bKey && _bWanted == SW_FALSE )
            return false;
        return XFilterEvent( &event, 0 ) != 0; // 0 == None. X11MacroUndef.h 가 None 매크로를 지운다.
    }

    void X11InputMethod::emitKeyText( void* pKeyPressEvent )
    {
        if ( _pInputContext == nullptr )
        {
            emitKeySymText( pKeyPressEvent );
            return;
        }
        XKeyPressedEvent* const pEvent = static_cast<XKeyPressedEvent*>( pKeyPressEvent );
        KeySym                  keySym{ 0 };
        Status                  status{ 0 };
        utf8                    arrStack[constant::buffer::kMax256];
        int32                   byteCount = Xutf8LookupString( static_cast<XIC>( _pInputContext ), pEvent, arrStack, static_cast<int32>( sizeof( arrStack ) ), &keySym, &status );
        if ( status == XBufferOverflow )
        {
            // 입력기가 긴 글을 한 번에 확정했다. 글은 버퍼를 키워 다시 읽을 때까지 문맥에 남는다(XIM 규약).
            string longText( static_cast<size_t>( byteCount ), '\0' );
            byteCount = Xutf8LookupString( static_cast<XIC>( _pInputContext ), pEvent, longText.data(), byteCount, &keySym, &status );
            if ( ( status == XLookupChars || status == XLookupBoth ) && byteCount > 0 && _textSink.isBound() )
                _textSink( WindowTextInputKind::Commit, string_view( longText.data(), static_cast<size_t>( byteCount ) ) );
            return;
        }
        if ( ( status == XLookupChars || status == XLookupBoth ) && byteCount > 0 && _textSink.isBound() )
            _textSink( WindowTextInputKind::Commit, string_view( arrStack, static_cast<size_t>( byteCount ) ) );
    }

    void X11InputMethod::emitKeySymText( void* pKeyPressEvent )
    {
        // shortcut: 옛 키심 번호의 레이아웃(키릴 0x6xx 등)은 입력기 없이는 글자가 나오지 않는다, Xlib 내장 입력기마저 열리지 않는 환경(로캘 자료 없음)에서 쓸 일이 생기면 GLFW 의 키심 표를 들인다.
        if ( _textSink.isBound() == false )
            return;
        XKeyEvent* const pEvent = static_cast<XKeyEvent*>( pKeyPressEvent );
        utf8             arrBytes[constant::buffer::kMax16]{};
        KeySym           keySym{ 0 };
        const int32      byteCount = XLookupString( pEvent, arrBytes, static_cast<int32>( sizeof( arrBytes ) ), &keySym, nullptr );
        // ASCII 는 로캘과 무관하게 같은 바이트다(제어 문자 포함 - Win32 의 WM_CHAR 처럼 받는 쪽이 고른다). 그 밖의 바이트는 로캘 인코딩이라 키심으로 읽는다.
        if ( byteCount == 1 && static_cast<uint8>( arrBytes[0] ) < 0x80 )
        {
            _textSink( WindowTextInputKind::Commit, string_view( arrBytes, 1 ) );
            return;
        }
        const uint64 sym      = static_cast<uint64>( keySym );
        const bool   bLatin1  = 0xA0 <= sym && sym <= 0xFF;           // Latin-1 키심은 코드 포인트와 번호가 같다
        const bool   bUnicode = ( sym & 0xFF000000u ) == 0x01000000u; // 유니코드 키심(0x01000000 + 코드 포인트)
        if ( bLatin1 == false && bUnicode == false )
            return;
        string text;
        StringUtil::appendUTF8( text, bLatin1 ? static_cast<uint32>( sym ) : static_cast<uint32>( sym & 0x00FFFFFFu ) );
        _textSink( WindowTextInputKind::Commit, text );
    }

    void X11InputMethod::onFocusChanged( uint64 window, bool bFocused )
    {
        _bFocused = bFocused ? SW_TRUE : SW_FALSE;
        if ( bFocused )
            _focusWindow = window;
        applyFocus();
    }

    void X11InputMethod::setWanted( bool bWanted )
    {
        _bWanted = bWanted ? SW_TRUE : SW_FALSE;
        if ( bWanted == false && _pInputContext != nullptr && _composition.isEmpty() == false )
        {
            utf8* const pPending = Xutf8ResetIC( static_cast<XIC>( _pInputContext ) );
            if ( pPending != nullptr )
                XFree( pPending );
            emitCompositionCleared();
        }
        applyFocus();
    }

    void X11InputMethod::setArea( const WindowTextInputArea& area )
    {
        _area = area;
        applyArea();
    }

    void X11InputMethod::applyFocus()
    {
        if ( _pInputContext == nullptr )
            return;
        XIC const pContext = static_cast<XIC>( _pInputContext );
        // 떼어 낸 창도 같은 문맥을 쓴다 - 포커스 창만 옮긴다. 입력기의 필터가 그 창에 걸려 있어야 `XFilterEvent` 가 키를 본다.
        (void)XSetICValues( pContext, XNFocusWindow, static_cast<Window>( _focusWindow ), nullptr );
        if ( _bWanted && _bFocused )
            XSetICFocus( pContext );
        else
            XUnsetICFocus( pContext );
    }

    void X11InputMethod::applyArea()
    {
        if ( _pInputContext == nullptr )
            return;
        // 후보 창은 이 점 아래에 뜬다. 입력기가 그 속성을 받지 않는 방식이면 답(실패한 속성 이름)을 볼 일이 없다 - 자리만 어긋난다.
        XPoint              spot{ static_cast<short>( _area._position._x ), static_cast<short>( _area._position._y + _area._lineHeight ) };
        const XVaNestedList preedit = XVaCreateNestedList( 0, XNSpotLocation, &spot, nullptr );
        (void)XSetICValues( static_cast<XIC>( _pInputContext ), XNPreeditAttributes, preedit, nullptr );
        XFree( preedit );
    }

    void X11InputMethod::onPreeditEdited( uint32 firstCharacter, uint32 deleteCount, string_view insertedText )
    {
        _composition.applyEdit( firstCharacter, deleteCount, insertedText );
        if ( _textSink.isBound() )
            _textSink( WindowTextInputKind::Composition, _composition.getText() );
    }

    void X11InputMethod::onPreeditFinished()
    {
        if ( _composition.isEmpty() == false )
            emitCompositionCleared();
    }

    void X11InputMethod::emitCompositionCleared()
    {
        _composition.clear();
        if ( _textSink.isBound() )
            _textSink( WindowTextInputKind::Composition, string_view{} );
    }
} // namespace sw
#endif
