#include "pch.h"

#include "ReflectionParser/AnnotationMeta.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/string_splitter.h"

SW_LOG_CALLER( "AnnotationMeta" );
namespace sw
{
    AnnotationMeta::AnnotationMeta()
        : _mapBare{}
        , _mapKey{}
        , _listEntry{}
        , _bLoaded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void AnnotationMeta::clear()
    {
        _mapBare.clear();
        _mapKey.clear();
        _listEntry.clear();
        _bLoaded = SW_FALSE;
    }

    uint64 AnnotationMeta::hashScopeAndKey( string_view scope, string_view key ) noexcept
    {
        const uint64 scopeHash = StringUtil::computeHash64( scope, false );
        const uint64 delimHash = ( scopeHash ^ static_cast<uint64>( ':' ) ) * HashUtil::kFnvPrime64;
        return StringUtil::computeHash64( key, false, delimHash );
    }

    void AnnotationMeta::addAlias( const string_view scope, const string_view alias, AnnotationBinding binding )
    {
        if ( alias.empty() )
            return;
        const uint64 hash = hashScopeAndKey( scope, alias );

        // flag 한 줄이 두 형태를 함께 맡는다. 단독 토큰 `X` 와 `X=true` 다. 두 형태를 따로 적으면 어긋나기 쉽고,
        // 그러면 `PROPERTY( Polymorphic = true )` 가 _mapKey 에 없어 경고 없이 버려진다.
        if ( binding._kind == AnnotationBinding::Kind::Flag )
        {
            _mapKey.insert_or_assign( hash, AnnotationBinding{ binding._field, AnnotationBinding::Kind::Bool } );
            _mapBare.insert_or_assign( hash, std::move( binding ) );
            return;
        }

        // 넷 역할(NetRole)은 단독 토큰뿐이다. FUNCTION( Server ) 는 역할을 고르는 것이라 참/거짓이 없다.
        if ( binding._kind == AnnotationBinding::Kind::NetRole )
            _mapBare.insert_or_assign( hash, std::move( binding ) );
        else
            _mapKey.insert_or_assign( hash, std::move( binding ) );
    }

    bool AnnotationMeta::loadFile( const string_view absPath )
    {
        clear();

        string text;
        if ( FileUtil::readTextFile( absPath, text ) == false )
        {
            SW_LOG_WARNING( "Failed to read: %#", absPath );
            return false;
        }

        string                  currentScope;
        [[maybe_unused]] uint32 bindingCount = 0;

        const string_splitter lines( text, { "\r\n", "\n" } );
        for ( const string_view rawLine : lines.getSplitList() )
        {
            // `trim` 의 `string_view` 오버로드를 쓴다. 줄마다 임시 `string` 을 만들지 않는다.
            const string_view line = StringUtil::trim( rawLine );
            if ( line.empty() || line.front() == '#' || line.front() == ';' )
                continue;

            if ( line.front() == '[' && line.back() == ']' && line.size() >= 3 )
            {
                currentScope = StringUtil::trim( line.substr( 1, line.size() - 2 ) );
                continue;
            }
            if ( currentScope.empty() )
                continue;

            const size_t equalPos = line.find( '=' );
            if ( equalPos == string_view::npos )
                continue;

            const string_view left  = StringUtil::trim( line.substr( 0, equalPos ) );
            const string_view right = StringUtil::trim( line.substr( equalPos + 1 ) );
            const size_t      dot   = left.find( '.' );
            if ( dot == string::npos || dot == 0 || dot + 1 >= left.size() )
            {
                SW_LOG_WARNING( "Expected kind.Field = aliases: %#", line );
                continue;
            }

            AnnotationBinding binding;
            if ( tryParseAnnotationKind( left.substr( 0, dot ), binding._kind ) == false )
            {
                SW_LOG_WARNING( "Unknown kind: %#", line );
                continue;
            }

            binding._field = left.substr( dot + 1 );
            const string_splitter aliases( right, { "," } );
            bool                  bAnyAlias = false;
            for ( const string_view aliasView : aliases.getSplitList() )
            {
                const string_view alias = StringUtil::trim( aliasView );
                if ( alias.empty() )
                    continue;
                addAlias( currentScope, alias, binding );
                bAnyAlias = true;
                ++bindingCount;
            }
            if ( bAnyAlias )
                _listEntry.push_back( AnnotationMetaEntry{ currentScope, std::move( binding ) } );
        }

        _bLoaded = SW_TRUE;
        SW_LOG_TRACE( "%# alias bindings (%#)", bindingCount, absPath );
        return true;
    }

    const AnnotationBinding* AnnotationMeta::findBare( const string_view scope,
                                                       const string_view token ) const
    {
        const auto it = _mapBare.find( hashScopeAndKey( scope, token ) );
        return ( it != _mapBare.end() ) ? &it->second : nullptr;
    }

    const AnnotationBinding* AnnotationMeta::findKey( const string_view scope,
                                                      const string_view key ) const
    {
        const auto it = _mapKey.find( hashScopeAndKey( scope, key ) );
        return ( it != _mapKey.end() ) ? &it->second : nullptr;
    }
} // namespace sw
