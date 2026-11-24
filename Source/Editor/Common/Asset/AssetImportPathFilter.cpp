#include "pch.h"

#include "Editor/Common/Asset/AssetImportPathFilter.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/ASCIIUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    namespace
    {
        struct AssetImportPathFilterInternal
        {
            /** @brief 패턴 목록 중 하나가 @p normalized 경로에 맞는지 봅니다(마디 매칭은 `StringUtil::matchesPathPattern`). */
            static bool matchesAnyPattern( const vector<string>& listPattern, string_view normalized )
            {
                return VectorUtil::containsIf( listPattern, [&]( const string& pattern )
                { return StringUtil::matchesPathPattern( pattern, normalized ); } );
            }

            /** @brief 경로 목록 중 하나(폴더 마디의 연속)가 디렉터리 안에 마디 경계로 들어 있는지 봅니다. @p dirHaystack 은 앞뒤에 `/` 를 붙인 디렉터리입니다. */
            static bool containsAnyPath( const vector<string>& listPath, string_view dirHaystack )
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
                    StringBuilder<constant::buffer::kMax256> segmentBuilder;
                    segmentBuilder.appendFormat( "/%#/", trimmed.substr( begin, end - begin ) );
                    return StringUtil::contains( dirHaystack, segmentBuilder.view(), true );
                } );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool AssetImportPathFilter::matchesPath( string_view relativePath ) const
    {
        const string                             normalized = FileUtil::normalizeSeparators( relativePath );
        StringBuilder<constant::buffer::kMax256> haystackBuilder;
        haystackBuilder.appendFormat( "/%#/", FileUtil::getDirectoryPart( normalized ) );
        const string_view dirHaystack = haystackBuilder.view();

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

namespace sw::editor
{
    string_view AssetImportPathFilter::findSingleFilePattern() const
    {
        for ( const string& pattern : _listIncludePattern )
        {
            if ( pattern.find_first_of( "*?" ) == string::npos )
                return pattern;
        }
        return {};
    }

    void AssetImportRuleUtil::reportSingleFilePattern( string_view configName, const string& ruleName, string_view pattern )
    {
        SW_LOG_ERROR_ID( "AssetImportConfig.SingleFilePattern", "%#: rule %# includes the single file '%#'. Put a one-file exception in that asset's .meta (_listImportOverride), not in the folder rules.",
                         configName, ruleName.c_str(), pattern );
    }

    bool AssetImportRuleUtil::readMetaOverrides( const AbsolutePath& sourcePath, vector<AssetImportOverride>& outListOverride )
    {
        outListOverride.clear();
        const AbsolutePath     metaPath = AssetMeta::makePath( sourcePath );
        string                 text;
        const OptionalFileRead read = FileUtil::readOptionalTextFile( metaPath, text );
        if ( read == OptionalFileRead::Missing )
            return true;
        AssetMeta meta;
        if ( read == OptionalFileRead::Failed || meta.parseXML( text, metaPath.str() ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "AssetImportConfig.MetaUnreadable", "Could not read the import overrides of '%#' - fix its .meta before importing", metaPath.c_str() );
            return false;
        }
        outListOverride = std::move( meta._listImportOverride );
        return true;
    }

    expected<void> AssetImportRuleUtil::makeOverrideObject( const vector<AssetImportOverride>& listOverride, std::initializer_list<string_view> listForbiddenKey, JSONDocument& outDoc )
    {
        const auto isNameChar = []( const utf8 c )
        { return StringUtil::isDigitChar( c ) || ( 'a' <= ASCIIUtil::toLower( c ) && ASCIIUtil::toLower( c ) <= 'z' ) || c == '_'; };
        StringBuilder<constant::buffer::kMax1024> text;
        text.append( '{' );
        for ( size_t index = 0; index < listOverride.size(); ++index )
        {
            const AssetImportOverride& item       = listOverride[index];
            const bool                 bNameValid = item._key.empty() == false && std::all_of( item._key.begin(), item._key.end(), isNameChar );
            const bool                 bForbidden = std::find( listForbiddenKey.begin(), listForbiddenKey.end(), string_view{ item._key } ) != listForbiddenKey.end();
            const auto                 itEnd      = listOverride.begin() + static_cast<ptrdiff_t>( index );
            const bool                 bRepeated  = std::any_of( listOverride.begin(), itEnd, [&item]( const AssetImportOverride& other )
                             { return other._key == item._key; } );
            if ( bNameValid == false || bForbidden || bRepeated )
                return unexpected( Error{ ErrorKind::InvalidArgument, "an import override names no setting (a rule key other than name and filter, written once)", string_view{ item._key } } );
            // 값 하나씩 따로 읽어 본다 — `1, "filter": …` 같은 글이 다른 칸을 끼워 넣지 못한다.
            JSONDocument                             check;
            StringBuilder<constant::buffer::kMax256> valueBuilder;
            valueBuilder.appendFormat( "{\"v\":%#}", item._value );
            if ( check.tryParse( valueBuilder.view() ) == false || check.getRoot().getMemberNames().size() != 1 )
                return unexpected( Error{ ErrorKind::InvalidArgument, "an import override value is not one JSON value (strings are quoted: \"raw\")", string_view{ item._key } } );
            text.appendFormat( "%#\"%#\":%#", ( index == 0 ? "" : "," ), item._key, item._value );
        }
        text.append( '}' );
        if ( outDoc.parse( text.view() ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the import overrides are not one JSON object" } );
        return {};
    }
} // namespace sw::editor
