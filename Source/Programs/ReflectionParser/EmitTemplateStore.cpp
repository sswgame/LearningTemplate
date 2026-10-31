#include "pch.h"

#include "Programs/ReflectionParser/EmitTemplateStore.h"

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
    bool EmitTemplateStore::loadDirectory( const string_view absDir, const string_view extension )
    {
        for ( string& content : _arrTemplate )
        {
            content.clear();
        }

        if ( FileUtil::isDirectory( absDir ) == false )
        {
            SW_LOG_ERROR( "Not a directory: %#", absDir );
            return false;
        }

        vector<string> listFile;
        if ( FileUtil::collectFiles( absDir, extension, listFile, false ) == false )
        {
            // 일부 템플릿만 읽으면 빠진 템플릿의 출력이 조용히 비어 나온다 — 읽지 않는다(경로는 collectFiles 가 알린다).
            SW_LOG_ERROR( "Emit templates under '%#' could not all be listed", absDir );
            return false;
        }

        bool arrLoaded[static_cast<size_t>( EmitTemplate::Count )] = {};
        for ( const string& filePath : listFile )
        {
            // 하나라도 못 읽으면 그 템플릿의 출력이 조용히 빈다 - 읽지 않는다.
            string content;
            if ( FileUtil::readTextFile( filePath, content ) == false )
            {
                SW_LOG_ERROR( "Emit template '%#' could not be read", filePath );
                return false;
            }

            const string name  = FileUtil::removeExtension( FileUtil::getFileNamePart( filePath ) );
            size_t       index = 0;
            while ( index < std::size( kArrEmitTemplateName ) && name != kArrEmitTemplateName[index] )
            {
                ++index;
            }
            // 모르는 이름은 철자가 틀린 골격이다 - 쓰이지 않고 남아 고친 줄 알게 한다.
            if ( index == std::size( kArrEmitTemplateName ) )
            {
                SW_LOG_ERROR( "Emit template '%#' is not a known template (EmitTemplate) - rename or remove it", filePath );
                return false;
            }
            _arrTemplate[index] = std::move( content );
            arrLoaded[index]    = true;
        }

        bool bAllLoaded = true;
        for ( size_t index = 0; index < std::size( kArrEmitTemplateName ); ++index )
        {
            if ( arrLoaded[index] )
                continue;
            SW_LOG_ERROR( "Emit template '%#%#' is missing under '%#'", kArrEmitTemplateName[index], extension, absDir );
            bAllLoaded = false;
        }
        if ( bAllLoaded == false )
            return false;

        SW_LOG_TRACE( "Loaded %# templates from %#", static_cast<uint32>( listFile.size() ), absDir );
        return true;
    }

    string EmitTemplateStore::render( const EmitTemplate kind, const TemplateVars vars ) const
    {
        return expand( _arrTemplate[static_cast<size_t>( kind )], vars );
    }

    string EmitTemplateStore::expand( const string_view tpl, const TemplateVars vars )
    {
        size_t extraEstimated = 0;
        for ( const auto& [name, value] : vars )
        {
            extraEstimated += value.size();
        }

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
                {
                    ++keyEnd;
                }
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
