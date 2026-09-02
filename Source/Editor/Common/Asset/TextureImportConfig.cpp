#include "pch.h"

#include "Editor/Common/Asset/TextureImportConfig.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Json/JsonDocument.h"

namespace sw::editor
{
    namespace
    {
        bool matchWildcardInternal( string_view pattern, string_view text )
        {
            const utf8* pPat    = pattern.data();
            const utf8* pStr    = text.data();
            const utf8* pPatEnd = pPat + pattern.size();
            const utf8* pStrEnd = pStr + text.size();
            const utf8* pStar   = nullptr;
            const utf8* pMatch  = nullptr;

            while ( pStr < pStrEnd )
            {
                if ( pPat < pPatEnd && ( *pPat == '?' || std::tolower( static_cast<uint8>( *pPat ) ) == std::tolower( static_cast<uint8>( *pStr ) ) ) )
                {
                    ++pPat;
                    ++pStr;
                }
                else if ( pPat < pPatEnd && *pPat == '*' )
                {
                    pStar  = pPat++;
                    pMatch = pStr;
                }
                else if ( pStar != nullptr )
                {
                    pPat = pStar + 1;
                    pStr = ++pMatch;
                }
                else
                {
                    return false;
                }
            }

            while ( pPat < pPatEnd && *pPat == '*' )
            {
                ++pPat;
            }

            return pPat == pPatEnd;
        }

        TextureSwizzle parseSwizzleInternal( string_view swizzleStr )
        {
            if ( swizzleStr == "BGRA" || swizzleStr == "bgra" )
                return TextureSwizzle::BGRA;
            if ( swizzleStr == "ARGB" || swizzleStr == "argb" )
                return TextureSwizzle::ARGB;
            if ( swizzleStr == "RGB1" || swizzleStr == "rgb1" )
                return TextureSwizzle::RGB1;

            return TextureSwizzle::RGBA;
        }

        void parseStringListInternal( const sw::JsonValue& arrValue, vector<string>& outListItem )
        {
            outListItem.clear();
            if ( arrValue.isArray() == false )
                return;

            const size_t count = arrValue.size();
            for ( size_t index = 0; index < count; ++index )
            {
                const JsonValue item = arrValue.at( index );
                if ( item.isString() )
                {
                    outListItem.push_back( item.asString() );
                }
            }
        }
    } // namespace

    SW_LOG_CALLER( "TextureImportConfig" );

    TextureImportConfig::TextureImportConfig()
        : _mapPreset{}
        , _listRule{}
    {
    }

    bool TextureImportConfig::loadFromFile( string_view configPath )
    {
        string text;
        if ( FileUtil::readTextFile( configPath, text ) == false || text.empty() )
        {
            SW_LOG_WARNING( "Failed to read TextureImportConfig file: %#", configPath.data() );
            return false;
        }

        return loadFromJsonString( text );
    }

    bool TextureImportConfig::loadFromJsonString( string_view jsonString )
    {
        JsonDocument doc;
        if ( doc.parse( jsonString ) == false )
        {
            SW_LOG_ERROR( "Failed to parse TextureImportConfig JSON." );
            return false;
        }

        const JsonValue root = doc.root();
        if ( root.isObject() == false )
        {
            SW_LOG_ERROR( "TextureImportConfig root is not an object." );
            return false;
        }

        _mapPreset.clear();
        _listRule.clear();

        // 1) Parse Presets
        const JsonValue presetsVal = root.get( "presets" );
        if ( presetsVal.isObject() )
        {
            const vector<string> listName = presetsVal.memberNames();
            for ( const auto& name : listName )
            {
                const JsonValue   presetObj = presetsVal.get( name );
                TextureImportRule rule;
                rule._name = name;

                if ( presetObj.has( "inherits" ) )
                {
                    const string inheritName = presetObj.get( "inherits" ).asString();
                    auto         itParent    = _mapPreset.find( inheritName );
                    if ( itParent != _mapPreset.end() )
                    {
                        rule           = itParent->second;
                        rule._name     = name;
                        rule._inherits = inheritName;
                    }
                }

                parseRuleObject( presetObj, rule );
                _mapPreset[name] = rule;
            }
        }

        // 2) Parse Rules
        const JsonValue rulesVal = root.get( "rules" );
        if ( rulesVal.isArray() )
        {
            const size_t count = rulesVal.size();
            for ( size_t index = 0; index < count; ++index )
            {
                const JsonValue   ruleObj = rulesVal.at( index );
                TextureImportRule rule;

                if ( ruleObj.has( "inherits" ) )
                {
                    const string inheritName = ruleObj.get( "inherits" ).asString();
                    auto         itParent    = _mapPreset.find( inheritName );
                    if ( itParent != _mapPreset.end() )
                    {
                        rule           = itParent->second;
                        rule._inherits = inheritName;
                    }
                }

                parseRuleObject( ruleObj, rule );
                _listRule.push_back( rule );
            }
        }

        SW_LOG_INFO( "Loaded TextureImportConfig: %# presets, %# rules.", _mapPreset.size(), _listRule.size() );
        return true;
    }

    void TextureImportConfig::parseRuleObject( const sw::JsonValue& jsonValue, TextureImportRule& inoutRule )
    {
        if ( jsonValue.has( "name" ) )
            inoutRule._name = jsonValue.get( "name" ).asString();

        if ( jsonValue.has( "inherits" ) )
            inoutRule._inherits = jsonValue.get( "inherits" ).asString();

        if ( jsonValue.has( "format" ) )
            inoutRule._format = jsonValue.get( "format" ).asString();

        if ( jsonValue.has( "swizzle" ) )
            inoutRule._swizzle = parseSwizzleInternal( jsonValue.get( "swizzle" ).asString() );

        if ( jsonValue.has( "generate_mips" ) )
            inoutRule._bGenerateMips = jsonValue.get( "generate_mips" ).asBool() ? SW_TRUE : SW_FALSE;

        if ( jsonValue.has( "srgb" ) )
            inoutRule._bSrgb = jsonValue.get( "srgb" ).asBool() ? SW_TRUE : SW_FALSE;

        if ( jsonValue.has( "invert_green" ) )
            inoutRule._bInvertGreen = jsonValue.get( "invert_green" ).asBool() ? SW_TRUE : SW_FALSE;

        if ( jsonValue.has( "include_patterns" ) )
            parseStringListInternal( jsonValue.get( "include_patterns" ), inoutRule._listIncludePattern );

        if ( jsonValue.has( "exclude_patterns" ) )
            parseStringListInternal( jsonValue.get( "exclude_patterns" ), inoutRule._listExcludePattern );

        if ( jsonValue.has( "include_paths" ) )
            parseStringListInternal( jsonValue.get( "include_paths" ), inoutRule._listIncludePath );

        if ( jsonValue.has( "exclude_paths" ) )
            parseStringListInternal( jsonValue.get( "exclude_paths" ), inoutRule._listExcludePath );
    }

    bool TextureImportConfig::matchRule( string_view relativePath, TextureImportRule& outRule ) const
    {
        const string normalized = FileUtil::normalizeSeparators( relativePath );
        const string fileName   = FileUtil::getFileNamePart( normalized );
        const string dirName    = FileUtil::getDirectoryPart( normalized );

        for ( const auto& rule : _listRule )
        {
            // 1) Exclude paths check
            bool bExcludedPath = false;
            for ( const auto& exPath : rule._listExcludePath )
            {
                if ( dirName.find( exPath ) != string::npos || normalized.find( exPath ) != string::npos )
                {
                    bExcludedPath = true;
                    break;
                }
            }
            if ( bExcludedPath )
                continue;

            // 2) Exclude patterns check
            bool bExcludedPattern = false;
            for ( const auto& exPattern : rule._listExcludePattern )
            {
                if ( matchWildcardInternal( exPattern, fileName ) || matchWildcardInternal( exPattern, normalized ) )
                {
                    bExcludedPattern = true;
                    break;
                }
            }
            if ( bExcludedPattern )
                continue;

            // 3) Include paths check
            if ( rule._listIncludePath.empty() == false )
            {
                bool bPathMatched = false;
                for ( const auto& inPath : rule._listIncludePath )
                {
                    if ( dirName.find( inPath ) != string::npos || normalized.find( inPath ) != string::npos )
                    {
                        bPathMatched = true;
                        break;
                    }
                }
                if ( bPathMatched == false )
                    continue;
            }

            // 4) Include patterns check
            if ( rule._listIncludePattern.empty() == false )
            {
                bool bPatternMatched = false;
                for ( const auto& inPattern : rule._listIncludePattern )
                {
                    if ( matchWildcardInternal( inPattern, fileName ) || matchWildcardInternal( inPattern, normalized ) )
                    {
                        bPatternMatched = true;
                        break;
                    }
                }
                if ( bPatternMatched == false )
                    continue;
            }

            // All matching conditions satisfied! (First match wins)
            outRule = rule;
            return true;
        }

        return false;
    }
} // namespace sw::editor
