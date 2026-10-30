#include "pch.h"

#include "Core/File/AbsolutePath.h"

#include "Core/Log/Logger.h"

namespace sw
{
    namespace
    {
        // 설치는 기동 스레드 하나에서만 한다(그 뒤 읽기만이라 락이 없다).
        std::optional<ProcessRoots> s_roots;

        bool isSeparator( utf8 ch )
        {
            return ch == '/' || ch == '\\';
        }
    } // namespace

    bool AbsolutePath::isStrictAbsolute( string_view path )
    {
        if ( path.empty() || path.find( '\0' ) != string_view::npos )
            return false;

#if defined( SW_PLATFORM_LINUX )
        return path[0] == '/';
#else
        const bool bDriveLetter = ( 'a' <= path[0] && path[0] <= 'z' ) || ( 'A' <= path[0] && path[0] <= 'Z' );
        if ( bDriveLetter )
            return path.size() >= 3 && path[1] == ':' && isSeparator( path[2] ); // `C:foo` 는 드라이브 상대다
        // UNC: 구분자 둘 뒤에 서버 이름이 온다. 구분자 하나(`\foo`)는 드라이브 없는 루트라 거절한다.
        return path.size() >= 3 && isSeparator( path[0] ) && isSeparator( path[1] ) && isSeparator( path[2] ) == false;
#endif
    }

    std::optional<AbsolutePath> AbsolutePath::tryMake( string_view path )
    {
        if ( isStrictAbsolute( path ) == false )
            return std::nullopt;

        string normalized{ path };
        for ( utf8& ch : normalized )
        {
            if ( ch == '\\' )
                ch = '/';
        }
        return AbsolutePath{ std::move( normalized ) };
    }

    void ProcessRoots::install( ProcessRoots roots )
    {
        if ( s_roots.has_value() )
            SW_FATAL( "ProcessRoots::install called twice" );
        s_roots = std::move( roots );
    }

    const ProcessRoots& ProcessRoots::get()
    {
        if ( s_roots.has_value() == false )
            SW_FATAL( "ProcessRoots::get before install - the process roots are set once at startup" );
        return *s_roots;
    }
} // namespace sw
