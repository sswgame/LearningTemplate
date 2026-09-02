#include "pch.h"

#include "ReflectionParser/ContainerTypeMap.h"

#include "Core/Log/Logger.h"

#include "Engine/Reflection/ReflectionEnumNames.h"

SW_LOG_CALLER( "ContainerTypeMap" );
namespace sw
{
    ContainerTypeMap::ContainerTypeMap()
        : _listRule{}
        , _bLoaded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ContainerTypeMap& ContainerTypeMap::instance()
    {
        static ContainerTypeMap s_map;
        return s_map;
    }

    void ContainerTypeMap::clear()
    {
        _listRule.clear();
        _bLoaded = SW_FALSE;
    }

    void ContainerTypeMap::registerRule( const string& match, ContainerKind kind, const string& type )
    {
        if ( match.empty() || type.empty() )
            return;
        ContainerTypeRule rule;
        rule._match = match;
        rule._kind  = kind;
        rule._type  = type;
        _listRule.push_back( std::move( rule ) );
    }

    void ContainerTypeMap::registerRule( const string& match, const string& kindSpelling,
                                         const string& type )
    {
        ContainerKind kind = ContainerKind::Sequence;
        if ( kindSpelling.empty() == false && tryParseContainerKind( kindSpelling, kind ) == false )
            SW_LOG_WARNING( "Unknown kind '%#', using Sequence", kindSpelling );
        registerRule( match, kind, type );
    }

    const ContainerTypeRule* ContainerTypeMap::match( const string_view clangTypeSpelling ) const
    {
        for ( const ContainerTypeRule& rule : _listRule )
        {
            if ( clangTypeSpelling.find( rule._match ) != string_view::npos )
                return &rule;
        }
        return nullptr;
    }
} // namespace sw
