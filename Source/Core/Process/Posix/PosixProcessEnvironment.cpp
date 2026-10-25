#include "pch.h"

#include "Core/Process/ProcessEnvironment.h"

#if !defined( SW_PLATFORM_WINDOWS )
    #include <cstdlib>

namespace sw
{
    namespace
    {
        /** @brief `setenv` · `unsetenv` 가 거절하는 이름(빈 이름 · `=` 포함)이 아니면 true 입니다. */
        bool isValidName( string_view name )
        {
            return name.empty() == false && name.find( '=' ) == string_view::npos;
        }
    } // namespace

    bool ProcessEnvironment::tryRead( string_view name, string& outValue )
    {
        outValue.clear();
        if ( isValidName( name ) == false )
            return false;
        const utf8* const pValue = std::getenv( string{ name }.c_str() );
        if ( pValue == nullptr )
            return false;
        outValue = pValue;
        return true;
    }

    bool ProcessEnvironment::trySet( string_view name, string_view value )
    {
        return isValidName( name ) && ::setenv( string{ name }.c_str(), string{ value }.c_str(), 1 ) == 0;
    }

    bool ProcessEnvironment::tryUnset( string_view name )
    {
        return isValidName( name ) && ::unsetenv( string{ name }.c_str() ) == 0;
    }
} // namespace sw
#endif
