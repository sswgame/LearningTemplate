#include "pch.h"

#include "Core/Process/Process.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "Process" );

    namespace
    {
        /**
         * @brief 인자 하나를 CommandLineToArgvW(= MSVC C 런타임) 규칙으로 인용해 붙입니다.
         * @details 공백 · 따옴표가 없으면 그대로 붙인다. 있으면 큰따옴표로 감싸고, 따옴표 앞의 역슬래시 n 개는 2n+1 개(따옴표를 글자로),
         *          닫는 따옴표 앞의 역슬래시 n 개는 2n 개로 늘린다. 다른 글자 앞의 역슬래시는 그대로다(`C:\a b\` 같은 경로가 깨지지 않는다).
         *          `& | < > ^` 가 든 인자도 감싼다 — 표준 규칙으로는 필요 없지만 자식이 `cmd.exe /c` 이면 따옴표 밖의 그 글자가 명령을 나눈다(`code.cmd` 를 띄우는 IDE 열기).
         *          `%` 는 따옴표 안에서도 cmd 가 펼치므로 막지 못한다 — cmd 를 실행 파일로 고르는 쪽이 지는 틈이다.
         */
        void appendQuotedProcessArgument( string_view argument, string& inoutLine )
        {
            const bool bNeedsQuotes = argument.empty() || argument.find_first_of( " \t\n\v\"&|<>^" ) != string_view::npos;
            if ( bNeedsQuotes == false )
            {
                inoutLine += argument;
                return;
            }
            inoutLine += '"';
            size_t backslashCount = 0;
            for ( const utf8 character : argument )
            {
                if ( character == '\\' )
                {
                    ++backslashCount;
                    continue;
                }
                inoutLine.append( character == '"' ? backslashCount * 2 + 1 : backslashCount, '\\' );
                inoutLine += character;
                backslashCount = 0;
            }
            inoutLine.append( backslashCount * 2, '\\' );
            inoutLine += '"';
        }
    } // namespace
} // namespace sw

namespace sw
{
    string ProcessCommand::toCommandLine() const
    {
        // 첫 토큰(실행 파일)은 CommandLineToArgvW 가 역슬래시를 풀지 않고 따옴표만 본다 — Windows 경로에는 따옴표가 없으므로 같은 함수로 감싸도 같다.
        string line;
        appendQuotedProcessArgument( _executable, line );
        for ( const string& argument : _listArgument )
        {
            line += ' ';
            appendQuotedProcessArgument( argument, line );
        }
        return line;
    }

    vector<string> ProcessCommand::splitArgumentLine( string_view line )
    {
        vector<string> listArgument;
        string         current;
        bool           bInQuotes = false;
        bool           bHasToken = false; // `""` 도 빈 인자 하나다
        for ( const utf8 character : line )
        {
            if ( character == '"' )
            {
                bInQuotes = bInQuotes == false;
                bHasToken = true;
                continue;
            }
            const bool bSeparator = bInQuotes == false && ( character == ' ' || character == '\t' || character == '\r' || character == '\n' );
            if ( bSeparator )
            {
                if ( bHasToken )
                    listArgument.push_back( std::move( current ) );
                current.clear();
                bHasToken = false;
                continue;
            }
            current += character;
            bHasToken = true;
        }
        if ( bHasToken )
            listArgument.push_back( std::move( current ) );
        return listArgument;
    }
} // namespace sw

namespace sw
{
    Process::Process()
        : _pNativeHandle{ nullptr }
        , _pStdOutRead{ nullptr }
        , _pNativeThread{ nullptr }
        , _pStdInWrite{ nullptr }
        , _bufferedOutput{}
        , _processID{ 0 }
        , _bNewProcessGroup{ false }
    {
    }

    Process::~Process()
    {
        shutdown();
    }

    Process::Process( Process&& other ) noexcept
        : _pNativeHandle{ other._pNativeHandle }
        , _pStdOutRead{ other._pStdOutRead }
        , _pNativeThread{ other._pNativeThread }
        , _pStdInWrite{ other._pStdInWrite }
        , _bufferedOutput{ std::move( other._bufferedOutput ) }
        , _processID{ other._processID.load() }
        , _bNewProcessGroup{ other._bNewProcessGroup }
    {
        other._pNativeHandle = nullptr;
        other._pStdOutRead   = nullptr;
        other._pNativeThread = nullptr;
        other._pStdInWrite   = nullptr;
        other._processID.store( 0 );
    }

    Process& Process::operator=( Process&& other ) noexcept
    {
        if ( this != &other )
        {
            shutdown();

            _pNativeHandle    = other._pNativeHandle;
            _pStdOutRead      = other._pStdOutRead;
            _pNativeThread    = other._pNativeThread;
            _pStdInWrite      = other._pStdInWrite;
            _bufferedOutput   = std::move( other._bufferedOutput );
            _bNewProcessGroup = other._bNewProcessGroup;
            _processID.store( other._processID.load() );

            other._pNativeHandle = nullptr;
            other._pStdOutRead   = nullptr;
            other._pNativeThread = nullptr;
            other._pStdInWrite   = nullptr;
            other._processID.store( 0 );
        }
        return *this;
    }

    int32 Process::execute( const ProcessCommand& command, const ProcessOptions& options, const ProcessOutputDelegate& onOutput )
    {
        Process proc;
        if ( proc.launch( command, options ) == false )
        {
            SW_LOG_ERROR( "Failed to launch command: %#", command.toCommandLine() );
            return -1;
        }

        string line;
        while ( proc.readOutputLine( line ) )
        {
            if ( onOutput.isBound() )
                onOutput( line );
        }

        return proc.waitForExit();
    }
} // namespace sw
