#include "pch.h"

#include "Server/ServerConsole.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
    #include "Core/Container/StringUtil.h"

namespace sw
{
    namespace
    {
        struct ServerConsoleWindowsInternal
        {
            /** @brief 표준 입력 핸들이 무엇인가 — 기다리는 방법이 종류마다 다르다. */
            enum class InputKind : uint8
            {
                None,    ///< 핸들이 없다(서비스) — 바로 EOF
                Console, ///< 콘솔 — 키 입력을 기다렸다 읽는다
                Pipe,    ///< 파이프 — 쌓인 바이트를 들여다보고 읽는다(읽기가 막히면 멈춤 요청을 못 본다)
                File,    ///< 파일 · NUL — 끝까지 읽는다
            };

            static InputKind classify( HANDLE hInput )
            {
                if ( hInput == nullptr || hInput == INVALID_HANDLE_VALUE )
                    return InputKind::None;
                const DWORD fileType = GetFileType( hInput );
                DWORD       mode     = 0;
                if ( fileType == FILE_TYPE_CHAR && GetConsoleMode( hInput, &mode ) != FALSE )
                    return InputKind::Console;
                if ( fileType == FILE_TYPE_PIPE )
                    return InputKind::Pipe;
                return InputKind::File;
            }
        };
    } // namespace

    bool ServerConsole::readLinePlatform( string& outLine )
    {
        const HANDLE                                  hInput = GetStdHandle( STD_INPUT_HANDLE );
        const ServerConsoleWindowsInternal::InputKind kind   = ServerConsoleWindowsInternal::classify( hInput );
        for ( ;; )
        {
            if ( takeLine( outLine ) )
                return true;
            if ( _stopRequested.load() != 0 )
                return false;
            switch ( kind )
            {
                case ServerConsoleWindowsInternal::InputKind::None:
                {
                    _endOfInput.store( 1 );
                    return false;
                }
                case ServerConsoleWindowsInternal::InputKind::Console:
                {
                    // 입력 레코드가 오면 신호가 선다. 키 누름이 아닌 레코드(포커스 · 마우스 · 창 크기)는 버리고, 키가 있을 때만 줄 읽기로 간다 —
                    // ReadConsoleW 는 Enter 까지 막히므로, 키를 친 뒤의 멈춤은 그 줄이 끝날 때까지 늦어진다(서버 종료는 정상 종료 요청이 먼저 온다).
                    if ( WaitForSingleObject( hInput, 100 ) != WAIT_OBJECT_0 )
                        continue;
                    INPUT_RECORD record{};
                    DWORD        peeked = 0;
                    if ( PeekConsoleInputW( hInput, &record, 1, &peeked ) == FALSE || peeked == 0 )
                        continue;
                    if ( record.EventType != KEY_EVENT || record.Event.KeyEvent.bKeyDown == FALSE )
                    {
                        (void)ReadConsoleInputW( hInput, &record, 1, &peeked );
                        continue;
                    }
                    utf16 arrBuffer[256];
                    DWORD readCount = 0;
                    if ( ReadConsoleW( hInput, arrBuffer, SW_COUNT_OF( arrBuffer ) - 1, &readCount, nullptr ) == FALSE )
                    {
                        _endOfInput.store( 1 );
                        return false;
                    }
                    arrBuffer[readCount] = L'\0';
                    _partialLine += StringUtil::utf16ToUtf8( arrBuffer );
                    break;
                }
                case ServerConsoleWindowsInternal::InputKind::Pipe:
                {
                    DWORD available = 0;
                    if ( PeekNamedPipe( hInput, nullptr, 0, nullptr, &available, nullptr ) == FALSE )
                    {
                        _endOfInput.store( 1 ); // 쓰는 쪽이 닫았다(ERROR_BROKEN_PIPE)
                        return false;
                    }
                    if ( available == 0 )
                    {
                        Sleep( 50 );
                        continue;
                    }
                    utf8  arrBuffer[512];
                    DWORD readCount = 0;
                    if ( ReadFile( hInput, arrBuffer, available < sizeof( arrBuffer ) ? available : static_cast<DWORD>( sizeof( arrBuffer ) ), &readCount, nullptr ) == FALSE ||
                         readCount == 0 )
                    {
                        _endOfInput.store( 1 );
                        return false;
                    }
                    _partialLine.append( arrBuffer, readCount );
                    break;
                }
                case ServerConsoleWindowsInternal::InputKind::File:
                {
                    utf8  arrBuffer[512];
                    DWORD readCount = 0;
                    if ( ReadFile( hInput, arrBuffer, sizeof( arrBuffer ), &readCount, nullptr ) == FALSE || readCount == 0 )
                    {
                        // 끝에 줄바꿈 없는 마지막 줄도 명령이다.
                        if ( _partialLine.empty() == false )
                            _partialLine += '\n';
                        if ( takeLine( outLine ) )
                            return true;
                        _endOfInput.store( 1 );
                        return false;
                    }
                    _partialLine.append( arrBuffer, readCount );
                    break;
                }
            }
        }
    }
} // namespace sw

#endif
