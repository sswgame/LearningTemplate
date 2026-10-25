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

            /** @brief 패턴 목록 중 하나가 @p normalized 경로에 맞는지 봅니다(마디 매칭은 `StringUtil::matchesPathPattern`). */
            static bool matchesAnyPattern( const vector<string>& listPattern, string_view normalized )
            {
                return VectorUtil::containsIf( listPattern, [&]( const string& pattern )
                { return StringUtil::matchesPathPattern( pattern, normalized ); } );
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
        const string dirHaystack = "/" + FileUtil::getDirectoryPart( normalized ) + "/";

        if ( AssetImportPathFilterInternal::containsAnyPath( _listExcludePath, dirHaystack ) )
            return false;
        if ( AssetImportPathFilterInternal::matchesAnyPattern( _listExcludePattern, normalized ) )
            return false;
        if ( _listIncludePath.empty() == false && AssetImportPathFilterInternal::containsAnyPath( _listIncludePath, dirHaystack ) == false )
            return false;
        if ( _listIncludePattern.empty() == false && AssetImportPathFilterInternal::matchesAnyPattern( _listIncludePattern, normalized ) == false )
            return false;
        return true;
    }

    bool AssetImportPathFilter::isCatchAll() const
    {
        return _listIncludePattern.empty() && _listIncludePath.empty() && _listExcludePattern.empty() && _listExcludePath.empty();
    }
} // namespace sw::editor
