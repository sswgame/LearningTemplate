#include "pch.h"

#include "Engine/Window/WindowFileDrop.h"

namespace sw
{
    namespace
    {
        struct WindowFileDropUtilInternal
        {
            static constexpr int32  kNotHexDigit        = -1; ///< `readHexDigit` 의 "16진이 아니다"
            static constexpr int32  kDecimalDigitCount  = 10; ///< 16진 a ~ f 의 값이 시작하는 자리
            static constexpr int32  kHexBase            = 16;
            static constexpr size_t kPercentEscapeWidth = 2; ///< `%` 뒤 16진 두 글자

            /** @brief 16진 한 글자의 값입니다. 16진이 아니면 -1 입니다. */
            static int32 readHexDigit( utf8 character )
            {
                if ( character >= '0' && character <= '9' )
                    return character - '0';
                if ( character >= 'a' && character <= 'f' )
                    return character - 'a' + kDecimalDigitCount;
                if ( character >= 'A' && character <= 'F' )
                    return character - 'A' + kDecimalDigitCount;
                return kNotHexDigit;
            }

            /** @brief `file://` URI 한 줄을 경로로 풉니다. 받지 않는 줄이면 false 입니다. */
            static bool decodeFileURI( string_view line, string& outPath )
            {
                static constexpr string_view kScheme        = "file://";
                static constexpr string_view kLocalhostHost = "localhost";
                if ( line.size() < kScheme.size() || line.substr( 0, kScheme.size() ) != kScheme )
                    return false;
                string_view rest = line.substr( kScheme.size() );
                if ( rest.size() >= kLocalhostHost.size() && rest.substr( 0, kLocalhostHost.size() ) == kLocalhostHost )
                    rest.remove_prefix( kLocalhostHost.size() );
                if ( rest.empty() || rest.front() != '/' )
                    return false; // 다른 호스트의 파일은 받지 않는다
                outPath.clear();
                for ( size_t index = 0; index < rest.size(); ++index )
                {
                    const utf8 character = rest[index];
                    if ( character != '%' )
                    {
                        outPath.push_back( character );
                        continue;
                    }
                    const int32 high = index + kPercentEscapeWidth < rest.size() ? readHexDigit( rest[index + 1] ) : kNotHexDigit;
                    const int32 low  = high != kNotHexDigit ? readHexDigit( rest[index + kPercentEscapeWidth] ) : kNotHexDigit;
                    if ( high == kNotHexDigit || low == kNotHexDigit )
                        return false;
                    const utf8 decoded = static_cast<utf8>( high * kHexBase + low );
                    if ( decoded == '\0' )
                        return false; // 경로 가운데의 NUL 은 뒤를 자르는 공격이다
                    outPath.push_back( decoded );
                    index += kPercentEscapeWidth;
                }
                return outPath.empty() == false;
            }
        };
    } // namespace

    uint32 WindowFileDropUtil::parseURIList( string_view text, vector<string>& outListPath )
    {
        using Internal = WindowFileDropUtilInternal;
        uint32 rejected{ 0 };
        size_t start{ 0 };
        while ( start < text.size() )
        {
            size_t end = text.find( '\n', start );
            if ( end == string_view::npos )
                end = text.size();
            string_view line = text.substr( start, end - start );
            start            = end + 1;
            if ( line.empty() == false && line.back() == '\r' )
                line.remove_suffix( 1 );
            if ( line.empty() || line.front() == '#' )
                continue;
            string path;
            if ( Internal::decodeFileURI( line, path ) )
                outListPath.push_back( std::move( path ) );
            else
                ++rejected;
        }
        return rejected;
    }
} // namespace sw
