#include "pch.h"

#include "Core/Path/AbsolutePath.h"

namespace sw
{
    namespace
    {
#if defined( SW_PLATFORM_WINDOWS )
        bool isSeparator( utf8 ch )
        {
            return ch == '/' || ch == '\\';
        }
#endif

        /** @brief 구분자를 `/` 로 맞춘 엄격한 절대 경로 @p path 의 뿌리(`/` · `C:/` · `//서버/`) 길이입니다. */
        size_t getRootLength( const string& path )
        {
#if defined( SW_PLATFORM_WINDOWS )
            if ( path.size() >= 2 && path[0] == '/' && path[1] == '/' )
            {
                const size_t serverEnd = path.find( '/', 2 );
                return serverEnd == string::npos ? path.size() : serverEnd + 1;
            }
            return 3;
#elif defined( SW_PLATFORM_POSIX )
            (void)path;
            return 1;
#else
    #error "Unsupported platform"
#endif
        }

        /**
         * @brief 구분자를 `/` 로 맞춘 엄격한 절대 경로 @p path 의 `.` · `..` 구간을 글로 풉니다. 뿌리 위로 올라가면 false 입니다.
         * @details 사람이 적은 `../Cooked` 를 그대로 이으면 포함 판정(`FileUtil::tryMakePathUnderRoot` — `..` 가 든 경로는 밖으로 본다)이 그 아래 파일을 모두 밖으로 봐
         *          쿠킹 스테이징이 팩에서 통째로 빠졌다. 받는 자리에서 한 번 푼다(언리얼 `FPaths::ConvertRelativePathToFull` 의 `CollapseRelativeDirectories`).
         */
        bool collapseDotSegments( string& path )
        {
            const size_t rootLength = getRootLength( path );
            string       collapsed  = path.substr( 0, rootLength );
            size_t       start      = rootLength;
            while ( start <= path.size() )
            {
                size_t end = path.find( '/', start );
                end        = end == string::npos ? path.size() : end;
                const string_view component( path.data() + start, end - start );
                start = end + 1;
                if ( component.empty() || component == "." )
                    continue;
                if ( component == ".." )
                {
                    if ( collapsed.size() == rootLength )
                        return false;
                    const size_t slash = collapsed.rfind( '/' );
                    collapsed.resize( slash < rootLength ? rootLength : slash );
                    continue;
                }
                if ( collapsed.size() > rootLength )
                    collapsed.push_back( '/' );
                collapsed.append( component.data(), component.size() );
            }
            path = std::move( collapsed );
            return true;
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool AbsolutePath::isStrictAbsolute( string_view path )
    {
        if ( path.empty() || path.find( '\0' ) != string_view::npos )
            return false;

#if defined( SW_PLATFORM_POSIX )
        return path[0] == '/';
#elif defined( SW_PLATFORM_WINDOWS )
        const bool bDriveLetter = ( 'a' <= path[0] && path[0] <= 'z' ) || ( 'A' <= path[0] && path[0] <= 'Z' );
        if ( bDriveLetter )
            return path.size() >= 3 && path[1] == ':' && isSeparator( path[2] ); // `C:foo` 는 드라이브 상대다
        // UNC: 구분자 둘 뒤에 서버 이름이 온다. 구분자 하나(`\foo`)는 드라이브 없는 루트라 거절한다.
        return path.size() >= 3 && isSeparator( path[0] ) && isSeparator( path[1] ) && isSeparator( path[2] ) == false;
#else
    #error "Unsupported platform"
#endif
    }

    optional<AbsolutePath> AbsolutePath::tryMake( string_view path )
    {
        if ( isStrictAbsolute( path ) == false )
            return nullopt;

        string normalized{ path };
        for ( utf8& ch : normalized )
        {
            if ( ch == '\\' )
                ch = '/';
        }
        return AbsolutePath{ std::move( normalized ) };
    }

    optional<AbsolutePath> AbsolutePath::tryResolve( string_view text, const AbsolutePath& baseDirectory )
    {
        if ( text.empty() )
            return nullopt;
        const bool bAbsolute = isStrictAbsolute( text );
        // 절대도 상대도 아닌 모양 - 드라이브 상대(`C:foo`)와 드라이브 없는 루트(`/foo` · `\foo`, Windows).
        const bool bDriveRelative = text.size() >= 2 && text[1] == ':';
        const bool bRootless      = text[0] == '/' || text[0] == '\\';
        if ( bAbsolute == false && ( bDriveRelative || bRootless ) )
            return nullopt;
        string joined;
        if ( bAbsolute == false )
        {
            joined = baseDirectory.str();
            joined.push_back( '/' );
        }
        joined.append( text.data(), text.size() );
        const optional<AbsolutePath> joinedPath = tryMake( joined );
        if ( joinedPath.has_value() == false )
            return nullopt;
        string collapsed = joinedPath->str();
        if ( collapseDotSegments( collapsed ) == false )
            return nullopt;
        return AbsolutePath{ std::move( collapsed ) };
    }

    AbsolutePath AbsolutePath::join( string_view relative ) const
    {
        if ( _path.empty() || relative.find( '\0' ) != string_view::npos )
            return AbsolutePath{ string{} };
        const size_t rootLength = getRootLength( _path );
        string       joined     = _path;
        while ( joined.size() > rootLength && joined.back() == '/' )
        {
            joined.pop_back();
        }
        const size_t floorLength = joined.size(); // `..` 는 이 길이 밑으로 내려가지 못한다(상위 탈출)
        joined.reserve( joined.size() + 1 + relative.size() );
        size_t start = 0;
        while ( start <= relative.size() )
        {
            size_t end                  = relative.find_first_of( "/\\", start );
            end                         = end == string_view::npos ? relative.size() : end;
            const string_view component = relative.substr( start, end - start );
            start                       = end + 1;
            if ( component.empty() || component == "." )
                continue;
            if ( component == ".." )
            {
                if ( joined.size() <= floorLength )
                    return AbsolutePath{ string{} };
                const size_t slash = joined.rfind( '/' );
                joined.resize( slash < floorLength ? floorLength : slash );
                continue;
            }
            if ( joined.back() != '/' )
                joined.push_back( '/' );
            joined.append( component.data(), component.size() );
        }
        return AbsolutePath{ std::move( joined ) };
    }

    AbsolutePath AbsolutePath::getParent() const
    {
        if ( _path.empty() )
            return AbsolutePath{ string{} };
        const size_t rootLength = getRootLength( _path );
        size_t       end        = _path.size();
        while ( end > rootLength && _path[end - 1] == '/' )
        {
            --end;
        }
        if ( end <= rootLength )
            return AbsolutePath{ string{} }; // 뿌리의 부모는 없다
        const size_t slash = _path.rfind( '/', end - 1 );
        return AbsolutePath{ _path.substr( 0, slash + 1 <= rootLength ? rootLength : slash ) };
    }
} // namespace sw
