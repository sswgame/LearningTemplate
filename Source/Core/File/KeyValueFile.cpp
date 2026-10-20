#include "pch.h"

#include "Core/File/KeyValueFile.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

namespace sw
{

    bool KeyValueFile::parse( string_view text, KeyValueMap& outMap, KeyValueParseOptions opt )
    {
        outMap.clear();
        forEachContentLine(
            text,
            [&]( string_view line )
        {
            if ( opt._bSkipSemicolonComments != SW_FALSE && line.front() == ';' )
                return;
            if ( opt._bSkipBracketSections != SW_FALSE && line.front() == '[' )
                return;

            const size_t equalPos = line.find( '=' );
            if ( equalPos == string_view::npos || equalPos == 0 )
                return;

            string_view key = StringUtil::trim( line.substr( 0, equalPos ) );
            string_view val = StringUtil::trim( line.substr( equalPos + 1 ) );
            if ( key.empty() )
                return;
            // **뒤에 적힌 것이 이긴다.** `emplace` 는 이미 있는 키를 **덮지 않으므로**, 그것을 쓰면 손으로
            // 고친 설정 파일에서 같은 키를 아래에 다시 적어도 위의 옛 값이 그대로 읽힌다. INI 계열의
            // 통상 규약에도, 이 파일의 `dump` 가 키마다 한 줄만 쓰는 것에도 이쪽이 맞는다.
            outMap[string( key )] = string( val );
        },
            opt._commentChar );

        return outMap.empty() == false;
    }

    bool KeyValueFile::loadFile( string_view absPath, KeyValueMap& outMap, KeyValueParseOptions opt )
    {
        string text;
        if ( FileUtil::readTextFile( absPath, text ) == false )
            return false;
        return parse( text, outMap, opt );
    }

    const string* KeyValueFile::find( const KeyValueMap& mapData, string_view key )
    {
        const KeyValueMap::const_iterator it = mapData.find( key );
        return it != mapData.end() ? &it->second : nullptr;
    }

    string KeyValueFile::dump( const KeyValueMap& mapData, string_view headerComment, string_view sectionName )
    {
        string text;
        if ( headerComment.empty() == false )
        {
            if ( headerComment.front() != '#' )
                text += "# ";
            text.append( headerComment.data(), headerComment.size() );
            if ( text.empty() == false && text.back() != '\n' )
                text += '\n';
        }
        if ( sectionName.empty() == false )
        {
            text += '[';
            text.append( sectionName.data(), sectionName.size() );
            text += "]\n";
        }
        for ( const KeyValueMap::value_type& pair : mapData )
        {
            text += pair.first;
            text += '=';
            text += pair.second;
            text += '\n';
        }
        return text;
    }

    bool KeyValueFile::saveFile( string_view absPath, const KeyValueMap& mapData, string_view headerComment,
                                 string_view sectionName )
    {
        if ( absPath.empty() )
            return false;
        return FileUtil::writeTextFile( absPath, dump( mapData, headerComment, sectionName ) );
    }

    namespace
    {
        /** @brief 키 하나의 글을 타입으로 읽습니다. 없거나 빈 값은 1(없음), 깨지면 키와 함께 오류를 남기고 2 입니다. */
        template <typename T, typename ParseFn>
        uint8 readKeyValue( const KeyValueMap& mapData, string_view key, T& outValue, const utf8* pTypeLabel, ParseFn&& parse )
        {
            const auto iter = mapData.find( key );
            if ( iter == mapData.end() || StringUtil::trim( iter->second ).empty() )
                return 1;
            if ( parse( StringUtil::trim( iter->second ), outValue ) )
                return 0;
            SW_LOG_ERROR( "Key '%#' is not a %#: '%#'", key, pTypeLabel, iter->second );
            return 2;
        }
    } // namespace

    uint8 KeyValueFile::parseValue( const KeyValueMap& mapData, string_view key, int32& outValue )
    {
        return readKeyValue( mapData, key, outValue, "integer", []( string_view text, int32& value )
        { return StringUtil::parseInt( text, value ); } );
    }

    uint8 KeyValueFile::parseValue( const KeyValueMap& mapData, string_view key, float32& outValue )
    {
        return readKeyValue( mapData, key, outValue, "number", []( string_view text, float32& value )
        { return StringUtil::parseFloat( text, value ); } );
    }

    uint8 KeyValueFile::parseValue( const KeyValueMap& mapData, string_view key, bool& outValue )
    {
        return readKeyValue( mapData, key, outValue, "boolean", []( string_view text, bool& value )
        { return StringUtil::tryParseBool( text, value ); } );
    }

    uint8 KeyValueFile::parseValue( const KeyValueMap& mapData, string_view key, string& outValue )
    {
        return readKeyValue( mapData, key, outValue, "text", []( string_view text, string& value )
        {
            value = string( text );
            return true;
        } );
    }

    void KeyValueFile::reportMissing( string_view key )
    {
        SW_LOG_ERROR( "Required key '%#' is missing", key );
    }
} // namespace sw
