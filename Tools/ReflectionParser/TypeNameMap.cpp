#include "pch.h"

#include "ReflectionParser/TypeNameMap.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "ReflectionParser/ParserContext.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserUtil.h"

SW_LOG_CALLER( "TypeNameMap" );
namespace sw
{
    namespace
    {
        struct TypeNameMapInternal
        {
            /** @brief clang 수식어(const/class 등)와 참조를 제거합니다. */
            static string stripClangDecorations( string_view tView )
            {
                tView                        = StringUtil::trim( tView );
                const ParserClangConfig& cfg = ParserContext::getSharedConfig();

                bool bStripped{ true };
                while ( bStripped && tView.empty() == false )
                {
                    bStripped = false;
                    for ( const string& prefix : cfg._listTypeStripPrefix )
                    {
                        if ( StringUtil::startsWith( tView, prefix ) )
                        {
                            tView.remove_prefix( prefix.size() );
                            tView     = StringUtil::trim( tView );
                            bStripped = true;
                        }
                    }
                }

                while ( tView.empty() == false && tView.back() == '&' )
                {
                    tView.remove_suffix( 1 );
                    tView = StringUtil::trim( tView );
                }
                return string( tView );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TypeNameMap::TypeNameMap()
        : _mapAliasToCanonical{}
        , _bLoaded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    TypeNameMap& TypeNameMap::instance()
    {
        static TypeNameMap s_map;
        return s_map;
    }

    void TypeNameMap::clear()
    {
        _mapAliasToCanonical.clear();
        _bLoaded = SW_FALSE;
    }

    void TypeNameMap::addKey( const string& key, const string& canonical )
    {
        if ( key.empty() )
            return;
        _mapAliasToCanonical.insert_or_assign( key, canonical );
    }

    void TypeNameMap::registerEntry( const string& canonical, const string& nameSpace,
                                     const vector<string>& aliases )
    {
        if ( canonical.empty() )
            return;

        addKey( canonical, canonical );
        if ( nameSpace.empty() == false )
        {
            StringBuilder<constant::kMaxBuffer128> qualified;
            qualified.appendFormat( "%#::%#", nameSpace, canonical );
            addKey( string( qualified.view() ), canonical );
        }

        for ( const string& alias : aliases )
        {
            addKey( alias, canonical );
            if ( nameSpace.empty() == false )
            {
                StringBuilder<constant::kMaxBuffer128> qualified;
                qualified.appendFormat( "%#::%#", nameSpace, alias );
                addKey( string( qualified.view() ), canonical );
            }
        }
    }

    string TypeNameMap::normalize( const string& clangSpelling ) const
    {
        string t = TypeNameMapInternal::stripClangDecorations( clangSpelling );
        if ( t.empty() )
            return t;

        const auto it = _mapAliasToCanonical.find( t );
        if ( it != _mapAliasToCanonical.end() )
            t = it->second;
        else
        {
            const string_view bare = ParserUtil::scopeLeaf( t );
            if ( bare != t )
            {
                const auto itBare = _mapAliasToCanonical.find( string{ bare } );
                if ( itBare != _mapAliasToCanonical.end() )
                    t = itBare->second;
            }
        }

        return t;
    }

    string normalizeTypeName( const string& clangSpelling )
    {
        return TypeNameMap::instance().normalize( clangSpelling );
    }
} // namespace sw
