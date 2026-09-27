#include "pch.h"

#include "ReflectionParser/EmitTemplateStore.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

SW_LOG_CALLER( "EmitTemplateStore" );
namespace sw
{
    namespace
    {
        struct EmitTemplateStoreInternal
        {
            /** @brief 식별자 시작 문자인지 판별합니다. */
            static bool isIdentStart( const utf8 character )
            {
                return ( 'A' <= character && character <= 'Z' ) || ( 'a' <= character && character <= 'z' ) || character == '_';
            }

            /** @brief 식별자 중간 문자인지 판별합니다. */
            static bool isIdentChar( const utf8 character )
            {
                return isIdentStart( character ) || ( '0' <= character && character <= '9' );
            }

            /** @brief 변수 목록에서 키를 찾습니다. 없으면 빈 뷰입니다. */
            static string_view findVar( const EmitTemplateStore::TemplateVars vars, const string_view key )
            {
                for ( const auto& [name, value] : vars )
                {
                    if ( name == key )
                        return value;
                }
                return {};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    EmitTemplateStore::EmitTemplateStore()
        : _mapTemplate{}
        , _bLoaded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void EmitTemplateStore::clear()
    {
        _mapTemplate.clear();
        _bLoaded = SW_FALSE;
    }

    bool EmitTemplateStore::loadDirectory( const string_view absDir, const string_view extension )
    {
        clear();

        if ( FileUtil::directoryExists( absDir ) == false )
        {
            SW_LOG_WARNING( "Not a directory: %#", absDir );
            return false;
        }

        vector<string> listFile;
        FileUtil::collectFiles( absDir, extension, listFile, false );

        uint32 count = 0;
        for ( const string& filePath : listFile )
        {
            string content;
            if ( FileUtil::readTextFile( filePath, content ) == false )
                continue;

            _mapTemplate[FileUtil::removeExtension( FileUtil::getFileNamePart( filePath ) )] = std::move( content );
            ++count;
        }

        if ( count == 0 )
        {
            SW_LOG_WARNING( "No %# files in %#", extension, absDir );
            return false;
        }

        _bLoaded = SW_TRUE;
        SW_LOG_TRACE( "Loaded %# templates from %#", count, absDir );
        return true;
    }

    bool EmitTemplateStore::has( const string_view name ) const
    {
        return _mapTemplate.find( string( name ) ) != _mapTemplate.end();
    }

    string EmitTemplateStore::render( const string_view name, const TemplateVars vars ) const
    {
        const auto it = _mapTemplate.find( string( name ) );
        if ( it == _mapTemplate.end() )
        {
            SW_LOG_WARNING( "Missing template: %#", name );
            return {};
        }
        return expand( it->second, vars );
    }

    string EmitTemplateStore::expand( const string_view tpl, const TemplateVars vars )
    {
        size_t extraEstimated = 0;
        for ( const auto& [name, value] : vars )
            extraEstimated += value.size();

        string out;
        out.reserve( tpl.size() + extraEstimated + 64 );

        for ( size_t charIndex = 0; charIndex < tpl.size(); )
        {
            if ( tpl[charIndex] != '$' )
            {
                out.push_back( tpl[charIndex++] );
                continue;
            }

            // 이스케이프: $$ → $
            if ( charIndex + 1 < tpl.size() && tpl[charIndex + 1] == '$' )
            {
                out.push_back( '$' );
                charIndex += 2;
                continue;
            }

            string_view key;
            size_t      keyEnd = charIndex + 1;
            if ( keyEnd < tpl.size() && tpl[keyEnd] == '{' )
            {
                ++keyEnd;
                const size_t close = tpl.find( '}', keyEnd );
                if ( close == string_view::npos )
                {
                    out.push_back( tpl[charIndex++] );
                    continue;
                }
                key    = tpl.substr( keyEnd, close - keyEnd );
                keyEnd = close + 1;
            }
            else if ( keyEnd < tpl.size() && EmitTemplateStoreInternal::isIdentStart( tpl[keyEnd] ) )
            {
                const size_t start = keyEnd;
                ++keyEnd;
                while ( keyEnd < tpl.size() && EmitTemplateStoreInternal::isIdentChar( tpl[keyEnd] ) )
                    ++keyEnd;
                key = tpl.substr( start, keyEnd - start );
            }
            else
            {
                out.push_back( tpl[charIndex++] );
                continue;
            }

            const string_view value = EmitTemplateStoreInternal::findVar( vars, key );
            if ( value.empty() == false )
                out.append( value.data(), value.size() );
            charIndex = keyEnd;
        }

        return out;
    }
} // namespace sw
