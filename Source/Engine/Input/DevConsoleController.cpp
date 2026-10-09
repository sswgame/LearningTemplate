#include "pch.h"

#include "Engine/Input/DevConsoleController.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Log/Logger.h"
    #include "Core/String/StringUtil.h"

    #include "Engine/Input/InputManager.h"
    #include "Engine/Input/InputMap.h"
    #include "Engine/Window/DevConsoleWindow.h"
    #include "Engine/Window/IWindow.h"

namespace sw
{
    SW_LOG_CALLER( "DevConsole" );

    namespace
    {
        struct DevConsoleControllerInternal
        {
            /** @brief 셸 맵에 있어야 하는 콘솔 액션입니다. 이름은 `InputMapDefaults` 상수 하나만 씁니다. */
            static constexpr const utf8* kArrActionName[] = {
                InputMapDefaults::kDevConsoleToggleAction,
                InputMapDefaults::kDevConsoleCloseAction,
                InputMapDefaults::kDevConsoleSubmitAction,
                InputMapDefaults::kDevConsoleCompleteAction,
                InputMapDefaults::kDevConsoleHistoryBackAction,
                InputMapDefaults::kDevConsoleHistoryForwardAction,
                InputMapDefaults::kDevConsoleDeleteBackwardAction,
            };

            /** @brief 끝의 UTF-8 글자 하나를 지웁니다(이어지는 바이트까지). */
            static void removeLastCharacter( string& inoutText )
            {
                while ( inoutText.empty() == false )
                {
                    const uint8 last = static_cast<uint8>( inoutText.back() );
                    inoutText.pop_back();
                    if ( ( last & 0xC0 ) != 0x80 )
                        return;
                }
            }

            static bool wasTriggered( const InputMap& shellMap, const utf8* pActionName )
            {
                return shellMap.wasActionTriggered( hashed_string( pActionName ) );
            }
        };
    } // namespace

    DevConsoleController::DevConsoleController()
        : _console{}
        , _inputLine{}
        , _pendingText{}
        , _pWindow{ nullptr }
        , _pInput{ nullptr }
        , _bOpen{ SW_FALSE }
        , _bShellMapChecked{ SW_FALSE }
        , _bShellMapComplete{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    DevConsoleController::~DevConsoleController()
    {
        shutdown();
    }

    bool DevConsoleController::initialize( InputManager* pInput, IWindow* pOwner )
    {
        shutdown();
        _pInput = pInput;
        if ( _pInput != nullptr )
            _pInput->setTextInputCallback( SW_DELEGATE_METHOD( InputManager::TextInputDelegate, &DevConsoleController::onTextInput, this ), InputKeyboardFocus::DevConsole );
        if ( pOwner != nullptr )
            _pWindow = IDevConsoleWindow::createPlatform( *pOwner );
        return _pWindow != nullptr;
    }

    void DevConsoleController::shutdown()
    {
        if ( _pInput != nullptr )
        {
            _pInput->setTextInputCallback( {}, InputKeyboardFocus::DevConsole );
            if ( _pInput->getKeyboardFocus() == InputKeyboardFocus::DevConsole )
                _pInput->setKeyboardFocus( InputKeyboardFocus::Game );
            _pInput = nullptr;
        }
        _pWindow.reset();
        _bOpen = SW_FALSE;
        _pendingText.clear();
    }

    void DevConsoleController::update( InputMap& shellMap )
    {
        // 이번 프레임 글자는 여기서 붙이거나 버린다 — 남겨 두면 다음 프레임에 늦게 붙는다.
        const string text = std::move( _pendingText );
        _pendingText.clear();

        if ( hasConsoleActions( shellMap ) )
        {
            const bool bToggle = DevConsoleControllerInternal::wasTriggered( shellMap, InputMapDefaults::kDevConsoleToggleAction );
            const bool bClose  = isOpen() && DevConsoleControllerInternal::wasTriggered( shellMap, InputMapDefaults::kDevConsoleCloseAction );
            if ( bToggle || bClose )
            {
                // 이 프레임의 글자는 여닫는 키가 낸 것이다(` · ~) — 입력 줄에 넣지 않는다.
                setOpen( bToggle ? isOpen() == false : false );
            }
            else if ( isOpen() )
            {
                appendText( text );
                if ( DevConsoleControllerInternal::wasTriggered( shellMap, InputMapDefaults::kDevConsoleDeleteBackwardAction ) )
                    DevConsoleControllerInternal::removeLastCharacter( _inputLine );
                if ( DevConsoleControllerInternal::wasTriggered( shellMap, InputMapDefaults::kDevConsoleHistoryBackAction ) )
                {
                    if ( const string* pLine = _console.moveHistoryBack() )
                        _inputLine = *pLine;
                }
                if ( DevConsoleControllerInternal::wasTriggered( shellMap, InputMapDefaults::kDevConsoleHistoryForwardAction ) )
                {
                    if ( const string* pLine = _console.moveHistoryForward() )
                        _inputLine = *pLine;
                }
                if ( DevConsoleControllerInternal::wasTriggered( shellMap, InputMapDefaults::kDevConsoleCompleteAction ) )
                    completeInputLine();
                if ( DevConsoleControllerInternal::wasTriggered( shellMap, InputMapDefaults::kDevConsoleSubmitAction ) )
                {
                    (void)_console.submit( _inputLine ); // 답과 실패는 출력 줄에 남는다
                    _inputLine.clear();
                }
            }
            // 편집 액션은 열려 있는 동안만 켠다 — 닫혀 있을 때 Enter · Esc · 화살표는 게임의 것이다.
            shellMap.setLayerEnabled( hashed_string( InputMapDefaults::kDevConsoleLayerName ), isOpen() );
        }

        if ( _bOpen == SW_FALSE || _pWindow == nullptr )
            return;
        // 창 위치는 게임 창을 따라가야 해서(창을 옮기거나 크기를 바꾼다) 열려 있는 동안 프레임마다 넘긴다.
        vector<string> listLine;
        vector<uint8>  listErrorFlag;
        buildVisibleLines( listLine, listErrorFlag );
        _pWindow->present( listLine, listErrorFlag );
    }

    void DevConsoleController::setOpen( bool bOpen )
    {
        _bOpen = bOpen ? SW_TRUE : SW_FALSE;
        _pendingText.clear();
        _console.resetHistoryCursor();
        if ( _pInput != nullptr )
            _pInput->setKeyboardFocus( bOpen ? InputKeyboardFocus::DevConsole : InputKeyboardFocus::Game );
        if ( _pWindow != nullptr )
            _pWindow->setVisible( bOpen );
    }

    void DevConsoleController::onTextInput( string_view text )
    {
        _pendingText.append( text.data(), text.size() );
    }

    void DevConsoleController::appendText( string_view text )
    {
        size_t offset = 0;
        while ( offset < text.size() )
        {
            const uint32 codepoint = StringUtil::decodeUtf8( text, offset );
            // Enter · Tab · Backspace 는 액션으로 받는다. 그 키가 함께 내는 제어 문자(\r · \t · DEL)는 글자가 아니다.
            if ( codepoint < 0x20 || codepoint == 0x7F )
                continue;
            if ( _inputLine.size() >= kMaxInputLength )
                return;
            StringUtil::appendUtf8( _inputLine, codepoint );
        }
    }

    void DevConsoleController::completeInputLine()
    {
        vector<string> listCandidate;
        (void)_console.complete( _inputLine, listCandidate ); // 바뀌지 않아도 후보는 보여 준다
        if ( listCandidate.size() <= 1 )
            return;
        string candidates;
        for ( const string& candidate : listCandidate )
        {
            candidates += candidate;
            candidates += "  ";
        }
        _console.appendNote( candidates );
    }

    bool DevConsoleController::hasConsoleActions( const InputMap& shellMap )
    {
        if ( _bShellMapChecked == SW_TRUE )
            return _bShellMapComplete == SW_TRUE;
        _bShellMapChecked  = SW_TRUE;
        _bShellMapComplete = SW_TRUE;
        if ( shellMap.hasLayer( hashed_string( InputMapDefaults::kDevConsoleLayerName ) ) == false )
        {
            SW_LOG_ERROR( "Shell input map has no '%#' layer - the dev console cannot be opened with keys", InputMapDefaults::kDevConsoleLayerName );
            _bShellMapComplete = SW_FALSE;
        }
        for ( const utf8* pActionName : DevConsoleControllerInternal::kArrActionName )
        {
            if ( shellMap.hasAction( hashed_string( pActionName ) ) )
                continue;
            SW_LOG_ERROR( "Shell input map has no '%#' action - the dev console cannot be opened with keys", pActionName );
            _bShellMapComplete = SW_FALSE;
        }
        return _bShellMapComplete == SW_TRUE;
    }

    void DevConsoleController::buildVisibleLines( vector<string>& outListLine, vector<uint8>& outListErrorFlag ) const
    {
        outListLine.clear();
        outListErrorFlag.clear();
        const vector<DevConsoleLine>& listOutput = _console.getOutput();
        const size_t                  first      = listOutput.size() > kVisibleOutputCount ? listOutput.size() - kVisibleOutputCount : 0;
        for ( size_t index = first; index < listOutput.size(); ++index )
        {
            outListLine.push_back( listOutput[index]._text );
            outListErrorFlag.push_back( listOutput[index]._bError );
        }
        outListLine.push_back( "] " + _inputLine + "_" );
        outListErrorFlag.push_back( SW_FALSE );
    }
} // namespace sw

#endif
