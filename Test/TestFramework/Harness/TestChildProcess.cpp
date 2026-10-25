#include "pch.h"

#include "TestFramework/Harness/TestChildProcess.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/String/StringBuilder.h"

#include <cstdlib>

namespace test
{
    namespace
    {
        struct TestChildProcessInternal
        {
            /** @brief 환경 변수 하나를 걸거나(값) 지운다(널). 자식은 띄우는 순간의 환경을 물려받는다. */
            static void setEnvironmentVariable( const utf8* pName, const utf8* pValue )
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
            [[nodiscard]] static bool readEnvironmentVariable( const utf8* pName, sw::string& outValue )
            {
                sw::string  pValueText;
                const utf8* pValue = sw::ProcessEnvironment::tryRead( pName, pValueText ) ? pValueText.c_str() : nullptr;
                if ( pValue == nullptr )
                    return false;
                outValue = pValue;
                return true;
            }
        };
    } // namespace
} // namespace test

namespace test
{
    ProcessRunResult runThisExecutableAsChild( sw::string_view caseFullName, sw::vector_reference<const ChildEnvironmentVariable> listEnvironment,
                                               uint32 timeoutSeconds )
    {
        sw::StringBuilder<sw::constant::kMaxPathSize> arguments;
        arguments.append( "--test_filter=" ).append( caseFullName );
        return runThisExecutableWithArguments( arguments.view(), listEnvironment, timeoutSeconds );
    }

    ProcessRunResult runThisExecutableWithArguments( sw::string_view arguments, sw::vector_reference<const ChildEnvironmentVariable> listEnvironment,
                                                     uint32 timeoutSeconds )
    {
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
            saved._bHadValue = TestChildProcessInternal::readEnvironmentVariable( variable._pName, saved._value );
            listSaved.push_back( std::move( saved ) );
            TestChildProcessInternal::setEnvironmentVariable( variable._pName, variable._value.c_str() );
        }

        sw::StringBuilder<sw::constant::kMaxPathSize> command;
        command.append( '"' ).append( sw::FileUtil::getExecutablePath().c_str() ).append( "\" " ).append( arguments );

        sw::ProcessOptions options;
        options._workingDirectory = sw::FileUtil::getCurrentPath();

        // 자식은 띄우는 순간의 환경을 물려받는다 — 첫 줄을 받았을 때는 이미 떠 있으니 그때 되돌려도 되지만, 출력이 없는 자식도 있어
        // 판이 끝난 뒤 되돌린다. 그 사이 부모의 다른 스레드가 이 변수를 읽지 않는다(케이스는 한 번에 하나다).
        ProcessRunResult result = runProcessWithDeadline( command.view(), options, timeoutSeconds );

        for ( const SavedVariable& saved : listSaved )
        {
            TestChildProcessInternal::setEnvironmentVariable( saved._pName, saved._bHadValue ? saved._value.c_str() : nullptr );
        }
        return result;
    }
} // namespace test
