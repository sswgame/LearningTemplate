#include "pch.h"

#include "Editor/Common/Asset/AssetImportPathFilter.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    namespace
    {
        struct AssetImportPathFilterInternal
        {
            /** @brief 문자열 목록을 읽습니다. 경로 구분자는 `/` 로 맞춥니다(`\` 로 적어도 같은 패턴이다). */
            static void parseStringList( const JSONValue& arrValue, vector<string>& outListItem )
            {
                outListItem.clear();
                if ( arrValue.isArray() == false )
                    return;

                const size_t count = arrValue.size();
                for ( size_t index = 0; index < count; ++index )
                {
                    const JSONValue item = arrValue.at( index );
                    if ( item.isString() )
                        outListItem.push_back( FileUtil::normalizeSeparators( item.asString() ) );
                }
            }

            /** @brief 경로를 `/` 마디로 나눕니다(빈 마디는 버립니다). 뷰는 @p path 가 살아 있는 동안만 유효합니다. */
            static void splitSegments( string_view path, vector<string_view>& outListSegment )
            {
                outListSegment.clear();
                size_t start = 0;
                while ( start <= path.size() )
                {
                    size_t end = path.find( '/', start );
                    if ( end == string_view::npos )
                        end = path.size();
                    if ( end > start )
                        outListSegment.push_back( path.substr( start, end - start ) );
                    start = end + 1;
                }
            }

            /** @brief 패턴 마디 @p patternIndex 부터가 경로 마디 @p textIndex 부터에 모두 맞는지 봅니다. 마디 `**` 만 폴더를 넘습니다. */
            static bool matchesSegments( const vector<string_view>& listPattern, size_t patternIndex, const vector<string_view>& listText, size_t textIndex )
            {
                while ( patternIndex < listPattern.size() )
                {
                    if ( listPattern[patternIndex] == "**" )
                    {
                        for ( size_t skipIndex = textIndex; skipIndex <= listText.size(); ++skipIndex )
                        {
                            if ( matchesSegments( listPattern, patternIndex + 1, listText, skipIndex ) )
                                return true;
                        }
                        return false;
                    }
                    if ( textIndex >= listText.size() || StringUtil::matchesWildcard( listPattern[patternIndex], listText[textIndex] ) == false )
                        return false;
                    ++patternIndex;
                    ++textIndex;
                }
                return textIndex == listText.size();
            }

            /** @brief 패턴 목록 중 하나가 맞는지 봅니다. `/` 없는 패턴은 파일 이름에, 있는 패턴은 경로 마디에 맞춥니다. */
            static bool matchesAnyPattern( const vector<string>& listPattern, string_view fileName, const vector<string_view>& listTextSegment )
            {
                vector<string_view> listPatternSegment;
                return VectorUtil::containsIf( listPattern, [&]( const string& pattern )
                {
                    if ( pattern.find( '/' ) == string::npos )
                        return StringUtil::matchesWildcard( pattern, fileName );
                    splitSegments( pattern, listPatternSegment );
                    return matchesSegments( listPatternSegment, 0, listTextSegment, 0 );
                } );
            }

            /** @brief 경로 목록 중 하나(폴더 마디의 연속)가 디렉터리 안에 마디 경계로 들어 있는지 봅니다. @p dirHaystack 은 앞뒤에 `/` 를 붙인 디렉터리입니다. */
            static bool containsAnyPath( const vector<string>& listPath, const string& dirHaystack )
            {
                return VectorUtil::containsIf( listPath, [&]( const string& path )
                {
                    const string_view trimmed = StringUtil::trim( path );
                    size_t            begin   = 0;
                    size_t            end     = trimmed.size();
                    while ( begin < end && trimmed[begin] == '/' )
                    {
                        ++begin;
                    }
                    while ( end > begin && trimmed[end - 1] == '/' )
                    {
                        --end;
                    }
                    if ( begin == end )
                        return false;
                    return StringUtil::contains( dirHaystack, "/" + string( trimmed.substr( begin, end - begin ) ) + "/", true );
                } );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void AssetImportPathFilter::parse( const JSONValue& jsonValue )
    {
        if ( jsonValue.has( "include_patterns" ) )
            AssetImportPathFilterInternal::parseStringList( jsonValue.get( "include_patterns" ), _listIncludePattern );

        if ( jsonValue.has( "exclude_patterns" ) )
            AssetImportPathFilterInternal::parseStringList( jsonValue.get( "exclude_patterns" ), _listExcludePattern );

        if ( jsonValue.has( "include_paths" ) )
            AssetImportPathFilterInternal::parseStringList( jsonValue.get( "include_paths" ), _listIncludePath );

        if ( jsonValue.has( "exclude_paths" ) )
            AssetImportPathFilterInternal::parseStringList( jsonValue.get( "exclude_paths" ), _listExcludePath );
    }

    bool AssetImportPathFilter::matchesPath( string_view relativePath ) const
    {
        const string normalized  = FileUtil::normalizeSeparators( relativePath );
        const string fileName    = FileUtil::getFileNamePart( normalized );
        const string dirHaystack = "/" + FileUtil::getDirectoryPart( normalized ) + "/";

        vector<string_view> listTextSegment;
        AssetImportPathFilterInternal::splitSegments( normalized, listTextSegment );

        if ( AssetImportPathFilterInternal::containsAnyPath( _listExcludePath, dirHaystack ) )
            return false;
        if ( AssetImportPathFilterInternal::matchesAnyPattern( _listExcludePattern, fileName, listTextSegment ) )
            return false;
        if ( _listIncludePath.empty() == false && AssetImportPathFilterInternal::containsAnyPath( _listIncludePath, dirHaystack ) == false )
            return false;
        if ( _listIncludePattern.empty() == false && AssetImportPathFilterInternal::matchesAnyPattern( _listIncludePattern, fileName, listTextSegment ) == false )
            return false;
        return true;
    }

    bool AssetImportPathFilter::isCatchAll() const
    {
        return _listIncludePattern.empty() && _listIncludePath.empty() && _listExcludePattern.empty() && _listExcludePath.empty();
    }
} // namespace sw::editor
