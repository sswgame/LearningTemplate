#include "pch.h"

#include "Engine/Window/DevConsoleOverlay.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Engine/Window/IWindow.h"
    #include "Engine/Window/NativeWindowEvent.h"

namespace sw
{
    namespace
    {
        struct DevConsoleOverlayInternal
        {
            /** @brief 코드 포인트 하나를 UTF-8 로 붙입니다. */
            static void appendUtf8( string& inoutText, uint32 codepoint )
            {
                if ( codepoint < 0x80 )
                {
                    inoutText += static_cast<utf8>( codepoint );
                }
                else if ( codepoint < 0x800 )
                {
                    inoutText += static_cast<utf8>( 0xC0 | ( codepoint >> 6 ) );
                    inoutText += static_cast<utf8>( 0x80 | ( codepoint & 0x3F ) );
                }
                else if ( codepoint < 0x10000 )
                {
                    inoutText += static_cast<utf8>( 0xE0 | ( codepoint >> 12 ) );
                    inoutText += static_cast<utf8>( 0x80 | ( ( codepoint >> 6 ) & 0x3F ) );
                    inoutText += static_cast<utf8>( 0x80 | ( codepoint & 0x3F ) );
                }
            }

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
        };
    } // namespace

    DevConsoleOverlay::DevConsoleOverlay()
        : _console{}
        , _inputLine{}
        , _pWindow{ nullptr }
        , _bOpen{ SW_FALSE }
        , _bDirty{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    DevConsoleOverlay::~DevConsoleOverlay()
    {
        shutdown();
    }

    bool DevConsoleOverlay::initialize( IWindow* pOwner )
    {
        shutdown();
        if ( pOwner == nullptr )
            return false;
        _pWindow = IDevConsoleWindow::createPlatform( *pOwner );
        _bDirty  = SW_TRUE;
        return _pWindow != nullptr;
    }

    void DevConsoleOverlay::shutdown()
    {
        _pWindow.reset();
    }

    bool DevConsoleOverlay::handleEvent( const NativeWindowEvent& event )
    {
        DevConsoleKey key{};
        if ( _pWindow != nullptr && _pWindow->decodeKey( event, key ) && handleKey( key ) )
            return true;
        // 열려 있으면 키보드는 모두 콘솔 몫이다 — 치는 글자가 게임 입력(이동 · 점프)으로 새면 안 된다. 뗀 키는 게임으로 보내 눌림 고착을 막는다.
        return isOpen() && event.isKeyboardInput() && event.isInputRelease() == false;
    }

    bool DevConsoleOverlay::handleKey( const DevConsoleKey& key )
    {
        if ( isOpen() == false )
        {
            if ( key._kind != DevConsoleKey::Kind::Toggle )
                return false;
            setOpen( true );
            return true;
        }

        switch ( key._kind )
        {
            case DevConsoleKey::Kind::None:
            {
                return false;
            }
            case DevConsoleKey::Kind::Toggle:
            case DevConsoleKey::Kind::Close:
            {
                setOpen( false );
                break;
            }
            case DevConsoleKey::Kind::Character:
            {
                if ( _inputLine.size() < kMaxInputLength && key._codepoint >= 0x20 )
                    DevConsoleOverlayInternal::appendUtf8( _inputLine, key._codepoint );
                break;
            }
            case DevConsoleKey::Kind::Backspace:
            {
                DevConsoleOverlayInternal::removeLastCharacter( _inputLine );
                break;
            }
            case DevConsoleKey::Kind::Enter:
            {
                (void)_console.submit( _inputLine ); // 답과 실패는 출력 줄에 남는다
                _inputLine.clear();
                break;
            }
            case DevConsoleKey::Kind::Tab:
            {
                vector<string> listCandidate;
                (void)_console.complete( _inputLine, listCandidate ); // 바뀌지 않아도 후보는 보여 준다
                if ( listCandidate.size() > 1 )
                {
                    string candidates;
                    for ( const string& candidate : listCandidate )
                    {
                        candidates += candidate;
                        candidates += "  ";
                    }
                    _console.appendNote( candidates );
                }
                break;
            }
            case DevConsoleKey::Kind::HistoryBack:
            {
                if ( const string* pLine = _console.moveHistoryBack() )
                    _inputLine = *pLine;
                break;
            }
            case DevConsoleKey::Kind::HistoryForward:
            {
                if ( const string* pLine = _console.moveHistoryForward() )
                    _inputLine = *pLine;
                break;
            }
        }
        _bDirty = SW_TRUE;
        return true;
    }

    void DevConsoleOverlay::setOpen( bool bOpen )
    {
        _bOpen  = bOpen ? SW_TRUE : SW_FALSE;
        _bDirty = SW_TRUE;
        _console.resetHistoryCursor();
        if ( _pWindow != nullptr )
            _pWindow->setVisible( bOpen );
    }

    void DevConsoleOverlay::update()
    {
        if ( _bOpen == SW_FALSE || _pWindow == nullptr )
            return;
        // 창 위치는 게임 창을 따라가야 해서(창을 옮기거나 크기를 바꾼다) 열려 있는 동안 프레임마다 넘긴다.
        vector<string> listLine;
        vector<uint8>  listErrorFlag;
        buildVisibleLines( listLine, listErrorFlag );
        _pWindow->present( listLine, listErrorFlag );
        _bDirty = SW_FALSE;
    }

    void DevConsoleOverlay::buildVisibleLines( vector<string>& outListLine, vector<uint8>& outListErrorFlag ) const
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
