#include "pch.h"

#include "Editor/Common/Asset/AssetImportPathFilter.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    namespace
    {
        struct AssetImportPathFilterInternal
        {
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
                        outListItem.push_back( item.asString() );
                }
            }

            /** @brief 경로 목록 중 하나가 디렉터리나 전체 경로의 부분 문자열인지 봅니다. */
            static bool containsAnyPath( const vector<string>& listPath, const string& dirName, const string& normalized )
            {
                for ( const string& path : listPath )
                {
                    if ( dirName.find( path ) != string::npos || normalized.find( path ) != string::npos )
                        return true;
                }
                return false;
            }

            /** @brief 패턴 목록 중 하나가 파일 이름이나 전체 경로에 맞는지 봅니다. */
            static bool matchesAnyPattern( const vector<string>& listPattern, const string& fileName, const string& normalized )
            {
                for ( const string& pattern : listPattern )
                {
                    if ( StringUtil::matchesWildcard( pattern, fileName ) || StringUtil::matchesWildcard( pattern, normalized ) )
                        return true;
                }
                return false;
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
        const string normalized = FileUtil::normalizeSeparators( relativePath );
        const string fileName   = FileUtil::getFileNamePart( normalized );
        const string dirName    = FileUtil::getDirectoryPart( normalized );

        if ( AssetImportPathFilterInternal::containsAnyPath( _listExcludePath, dirName, normalized ) )
            return false;
        if ( AssetImportPathFilterInternal::matchesAnyPattern( _listExcludePattern, fileName, normalized ) )
            return false;
        if ( _listIncludePath.empty() == false && AssetImportPathFilterInternal::containsAnyPath( _listIncludePath, dirName, normalized ) == false )
            return false;
        if ( _listIncludePattern.empty() == false && AssetImportPathFilterInternal::matchesAnyPattern( _listIncludePattern, fileName, normalized ) == false )
            return false;
        return true;
    }

    bool AssetImportPathFilter::isCatchAll() const
    {
        return _listIncludePattern.empty() && _listIncludePath.empty() && _listExcludePattern.empty() && _listExcludePath.empty();
    }
} // namespace sw::editor
