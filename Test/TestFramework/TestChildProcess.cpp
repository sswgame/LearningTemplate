#include "pch.h"

#include "TestFramework/TestChildProcess.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace test
{
    namespace
    {
        /** @brief 환경 변수 하나를 걸거나(값) 지운다(널). 자식은 띄우는 순간의 환경을 물려받는다. */
        void setEnvironmentVariableInternal( const utf8* pName, const utf8* pValue )
        {
#if defined( SW_PLATFORM_WINDOWS )
            const sw::wstring wideName  = sw::StringUtil::utf8ToUtf16( pName );
            const sw::wstring wideValue = ( pValue != nullptr ) ? sw::StringUtil::utf8ToUtf16( pValue ) : sw::wstring{};
            ::SetEnvironmentVariableW( wideName.c_str(), ( pValue != nullptr ) ? wideValue.c_str() : nullptr );
#else
            if ( pValue != nullptr )
                ::setenv( pName, pValue, 1 );
            else
                ::unsetenv( pName );
#endif
        }

        /** @brief 지금 값을 돌려준다(없으면 false). */
        [[nodiscard]] bool readEnvironmentVariableInternal( const utf8* pName, sw::string& outValue )
        {
            const utf8* pValue = std::getenv( pName );
            if ( pValue == nullptr )
                return false;
            outValue = pValue;
            return true;
        }
    } // namespace

    sw::string ChildRunResult::getOutputTail( uint32 lineCount ) const
    {
        size_t start = _output.size();
        for ( uint32 line = 0; line <= lineCount && start > 0; ++line )
        {
            const size_t newline = _output.rfind( '\n', start - 1 );
            if ( newline == sw::string::npos )
            {
                start = 0;
                break;
            }
            start = newline;
        }
        return _output.substr( start );
    }

    ChildRunResult runThisExecutableAsChild( sw::string_view caseFullName, sw::vector_reference<const ChildEnvironmentVariable> listEnvironment,
                                             uint32 timeoutSeconds )
    {
#if defined( SW_SANITIZER_ADDRESS ) || defined( SW_SANITIZER_THREAD )
        timeoutSeconds *= 10;
#endif
        // 부모의 환경을 기억했다가 되돌린다 — 다음 케이스가 자식 역할을 물려받지 않게.
        struct SavedVariable
        {
            const utf8* _pName;
            bool        _bHadValue;
            sw::string  _value;
        };
        sw::vector<SavedVariable> listSaved;
        listSaved.reserve( listEnvironment.size() );
        for ( const ChildEnvironmentVariable& variable : listEnvironment )
        {
            SavedVariable saved{ variable._pName, false, {} };
            saved._bHadValue = readEnvironmentVariableInternal( variable._pName, saved._value );
            listSaved.push_back( std::move( saved ) );
            setEnvironmentVariableInternal( variable._pName, variable._value.c_str() );
        }

        sw::StringBuilder<sw::constant::kMaxPathSize> command;
        command.append( '"' ).append( sw::FileUtil::getExecutablePath().c_str() ).append( "\" --test_filter=" ).append( caseFullName );

        sw::ProcessOptions options;
        options._workingDirectory = sw::FileUtil::getCurrentPath();

        ChildRunResult result;
        sw::Process    process;
        result._bLaunched = process.launch( command.view(), options );

        // 자식이 떠 있는 동안만 환경을 걸어 둔다(자식은 띄우는 순간의 것을 물려받았다).
        for ( const SavedVariable& saved : listSaved )
        {
            setEnvironmentVariableInternal( saved._pName, saved._bHadValue ? saved._value.c_str() : nullptr );
        }

        if ( result._bLaunched == false )
            return result;

        // 시한 감시 — 출력을 읽는 이 스레드는 자식이 말없이 멈추면 영영 깨지 않으므로 다른 스레드가 죽인다.
        std::mutex              mutex;
        std::condition_variable wake;
        bool                    bFinished{ false };
        bool                    bTimedOut{ false };
        std::thread             watchdog( [&]()
        {
            std::unique_lock<std::mutex> lock( mutex );
            if ( wake.wait_for( lock, std::chrono::seconds( timeoutSeconds ), [&]()
                        { return bFinished; } ) == false )
            {
                bTimedOut = true;
                process.terminate( 1 );
            }
        } );

        sw::string line;
        while ( process.readOutputLine( line ) )
        {
            result._output += line;
            result._output.push_back( '\n' );
        }
        result._exitCode = process.waitForExit();

        {
            std::lock_guard<std::mutex> lock( mutex );
            bFinished = true;
        }
        wake.notify_one();
        watchdog.join();

        result._bTimedOut = bTimedOut;
        return result;
    }
} // namespace test
