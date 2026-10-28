#include "pch.h"

#include "Core/Log/Sink/ConsoleLogOutput.h"

#include "Core/Common/PlatformOSHeaders.h"
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
            /** @brief 깃발 비트입니다 — 줄마다 비운다(`setFlushEveryLine`). */
            static constexpr uint32 kFlagFlushEveryLine = 1u;
            /** @brief 깃발 비트입니다 — 쓰지 않는다(`setMuted`). */
            static constexpr uint32 kFlagMuted = 2u;
            /** @brief 두 깃발을 한 원자에 모았다 — 프로세스 전역이다. */
            static inline atomic<uint32> s_flags{ 0 };

            static void setFlag( uint32 flag, bool bOn )
            {
                if ( bOn )
                    s_flags.fetch_or( flag, std::memory_order_relaxed );
                else
                    s_flags.fetch_and( ~flag, std::memory_order_relaxed );
            }
            static bool hasFlag( uint32 flag ) { return ( s_flags.load( std::memory_order_relaxed ) & flag ) != 0; }

            /** @brief 이 수준의 줄을 바로 비우는가 — Error 는 늘, 나머지는 `setFlushEveryLine` 일 때. */
            static bool shouldFlush( LogLevel level ) { return level == LogLevel::Error || hasFlag( kFlagFlushEveryLine ); }
        };
    } // namespace

    void ConsoleLogOutput::setFlushEveryLine( bool bFlushEveryLine )
    {
        ConsoleLogOutputInternal::setFlag( ConsoleLogOutputInternal::kFlagFlushEveryLine, bFlushEveryLine );
    }

    void ConsoleLogOutput::setMuted( bool bMuted )
    {
        ConsoleLogOutputInternal::setFlag( ConsoleLogOutputInternal::kFlagMuted, bMuted );
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
        ScopedLock<mutex> lock{ _mutex };

#if defined( SW_PLATFORM_WINDOWS )
        SetConsoleOutputCP( CP_UTF8 );
        SetConsoleCP( CP_UTF8 );

        const HANDLE consoleHandle = GetStdHandle( STD_OUTPUT_HANDLE );
        DWORD        dwMode{ 0 };
        const bool   bConsoleModeQueried = consoleHandle != nullptr && consoleHandle != INVALID_HANDLE_VALUE && GetConsoleMode( consoleHandle, &dwMode );
        if ( bConsoleModeQueried )
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
        // 터미널이 아니면(journald · 파일 · 테스트 파이프) 색 이스케이프를 쓰지 않는다.
        _bHasConsole = isatty( STDOUT_FILENO ) != 0;
#endif
        // 콘솔이 없어도 표준 출력에는 언제나 쓸 수 있다. 색만 포기한다.
        return true;
    }

    void ConsoleLogOutput::close()
    {
        ScopedLock<mutex> lock{ _mutex };
        std::fflush( stdout );
        _pCachedConsoleHandle = nullptr;
        _bHasConsole          = false;
    }

    void ConsoleLogOutput::write( const LogRecord& record )
    {
        if ( ConsoleLogOutputInternal::hasFlag( ConsoleLogOutputInternal::kFlagMuted ) )
            return;
        const LogLevel level    = record._level;
        const utf8*    pMessage = record._formatted.c_str();

        ScopedLock<mutex> lock{ _mutex };

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

        const int32 index              = static_cast<int32>( level );
        const bool  bCanPrintAnsiColor = _bHasConsole && 0 <= index && index < static_cast<int32>( LogLevel::Count );
        if ( bCanPrintAnsiColor )
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
