#include "pch.h"

#include "Core/Log/ConsoleLogOutput.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"

#if defined( SW_PLATFORM_LINUX )
    #include <unistd.h>
#endif

namespace sw
{
    namespace
    {
        struct ConsoleLogOutputInternal
        {
            /** @brief 줄마다 비울지(`setFlushEveryLine`) — 프로세스 전역이다. */
            static inline atomic<uint32> s_flushEveryLine{ 0 };

            /** @brief 이 수준의 줄을 바로 비우는가 — Error 는 늘, 나머지는 `setFlushEveryLine` 일 때. */
            static bool shouldFlush( LogLevel level ) { return level == LogLevel::Error || s_flushEveryLine.load( std::memory_order_relaxed ) != 0; }
        };
    } // namespace

    void ConsoleLogOutput::setFlushEveryLine( bool bFlushEveryLine )
    {
        ConsoleLogOutputInternal::s_flushEveryLine.store( bFlushEveryLine ? 1u : 0u, std::memory_order_relaxed );
    }

    ConsoleLogOutput::ConsoleLogOutput()
        : _mutex{}
        , _pCachedConsoleHandle{ nullptr }
        , _defaultConsoleAttribute{ 0 }
        , _bHasConsole{ false }
    {
    }

    ConsoleLogOutput::~ConsoleLogOutput()
    {
        ConsoleLogOutput::close();
    }

    bool ConsoleLogOutput::open()
    {
        std::scoped_lock<mutex> lock{ _mutex };

#if defined( SW_PLATFORM_WINDOWS )
        SetConsoleOutputCP( CP_UTF8 );
        SetConsoleCP( CP_UTF8 );

        const HANDLE consoleHandle = GetStdHandle( STD_OUTPUT_HANDLE );
        DWORD        dwMode{ 0 };
        if ( consoleHandle != nullptr && consoleHandle != INVALID_HANDLE_VALUE && GetConsoleMode( consoleHandle, &dwMode ) )
        {
            CONSOLE_SCREEN_BUFFER_INFO consoleInfo{};
            GetConsoleScreenBufferInfo( consoleHandle, &consoleInfo );
            _pCachedConsoleHandle    = consoleHandle;
            _defaultConsoleAttribute = static_cast<uint16>( consoleInfo.wAttributes );
            _bHasConsole             = true;
        }
        else
        {
            _pCachedConsoleHandle = nullptr;
            _bHasConsole          = false;
        }
#elif defined( SW_PLATFORM_LINUX )
        // 터미널이 아니면(journald · 파일 · 시험 파이프) 색 이스케이프를 쓰지 않는다.
        _bHasConsole = isatty( STDOUT_FILENO ) != 0;
#endif
        // 콘솔이 없어도 표준 출력에는 언제나 쓸 수 있다. 색만 포기한다.
        return true;
    }

    void ConsoleLogOutput::close()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        std::fflush( stdout );
        _pCachedConsoleHandle = nullptr;
        _bHasConsole          = false;
    }

    void ConsoleLogOutput::write( const LogRecord& record )
    {
        const LogLevel level    = record._level;
        const utf8*    pMessage = record._formatted.c_str();

        std::scoped_lock<mutex> lock{ _mutex };

#if defined( SW_PLATFORM_WINDOWS )
        if ( _bHasConsole && _pCachedConsoleHandle != nullptr )
        {
            HANDLE                consoleHandle = static_cast<HANDLE>( _pCachedConsoleHandle );
            static constexpr WORD arrLevelColor[] =
                {
                    FOREGROUND_RED | FOREGROUND_INTENSITY,
                    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
                    FOREGROUND_GREEN | FOREGROUND_INTENSITY,
                    FOREGROUND_INTENSITY,
                };

            SetConsoleTextAttribute( consoleHandle, arrLevelColor[static_cast<int32>( level )] );
            std::fputs( pMessage, stdout );
            if ( ConsoleLogOutputInternal::shouldFlush( level ) )
                std::fflush( stdout );
            SetConsoleTextAttribute( consoleHandle, static_cast<WORD>( _defaultConsoleAttribute ) );
        }
        else
        {
            std::fputs( pMessage, stdout );
            if ( ConsoleLogOutputInternal::shouldFlush( level ) )
                std::fflush( stdout );
            OutputDebugStringA( pMessage );
        }
#elif defined( SW_PLATFORM_LINUX )
        // Linux · POSIX ANSI 이스케이프: 굵은 빨강(Error), 굵은 노랑(Warning), 굵은 초록(Info), 회색(Trace)
        static constexpr const utf8* arrAnsiColor[] =
            {
                "\033[1;31m", // Error: 굵은 빨강
                "\033[1;33m", // Warning: 굵은 노랑
                "\033[1;32m", // Info: 굵은 초록
                "\033[0;90m", // Trace: 회색
            };
        static constexpr const utf8* kAnsiReset = "\033[0m";

        const int32 index = static_cast<int32>( level );
        if ( _bHasConsole && 0 <= index && index < static_cast<int32>( LogLevel::Count ) )
        {
            std::fputs( arrAnsiColor[index], stdout );
            std::fputs( pMessage, stdout );
            std::fputs( kAnsiReset, stdout );
        }
        else
        {
            std::fputs( pMessage, stdout );
        }
        if ( ConsoleLogOutputInternal::shouldFlush( level ) )
            std::fflush( stdout );
#else
        std::fputs( pMessage, stdout );
        if ( ConsoleLogOutputInternal::shouldFlush( level ) )
            std::fflush( stdout );
#endif
    }
} // namespace sw
