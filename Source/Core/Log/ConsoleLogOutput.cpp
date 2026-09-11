#include "pch.h"

#include "Core/Log/ConsoleLogOutput.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Common/StdHeaders.h"

namespace sw
{
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
#endif
        // 콘솔이 없어도 표준 출력은 언제나 쓸 수 있다 — 색만 포기한다.
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
            if ( level == LogLevel::Error )
                std::fflush( stdout );
            SetConsoleTextAttribute( consoleHandle, static_cast<WORD>( _defaultConsoleAttribute ) );
        }
        else
        {
            std::fputs( pMessage, stdout );
            if ( level == LogLevel::Error )
                std::fflush( stdout );
            OutputDebugStringA( pMessage );
        }
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
        // Linux/POSIX ANSI Escape Sequences: Bold Red (Error), Bold Yellow (Warning), Bold Green (Info), Gray (Trace)
        static constexpr const utf8* arrAnsiColor[] =
            {
                "\033[1;31m", // Error: Bold Red
                "\033[1;33m", // Warning: Bold Yellow
                "\033[1;32m", // Info: Bold Green
                "\033[0;90m", // Trace: Gray
            };
        static constexpr const utf8* kAnsiReset = "\033[0m";

        const int32 index = static_cast<int32>( level );
        if ( 0 <= index && index < static_cast<int32>( LogLevel::Count ) )
        {
            std::fputs( arrAnsiColor[index], stdout );
            std::fputs( pMessage, stdout );
            std::fputs( kAnsiReset, stdout );
        }
        else
        {
            std::fputs( pMessage, stdout );
        }
        if ( level == LogLevel::Error )
            std::fflush( stdout );
#else
        std::fputs( pMessage, stdout );
        if ( level == LogLevel::Error )
            std::fflush( stdout );
#endif
    }
} // namespace sw
