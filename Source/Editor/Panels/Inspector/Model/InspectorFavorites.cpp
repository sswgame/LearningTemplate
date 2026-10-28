#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorFavorites.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorPaths.h"

#include "Engine/Reflection/ReflectionTypes.h"

#include <algorithm>

namespace sw::editor
{
    SW_LOG_CALLER( "InspectorFavorites" );

    string InspectorFavorites::makeKey( string_view typeName, string_view propertyName )
    {
        string key{ typeName };
        key += '.';
        key += propertyName;
        return key;
    }

    bool InspectorFavorites::contains( string_view key ) const
    {
        return std::binary_search( _listKey.begin(), _listKey.end(), key, []( const auto& lhs, const auto& rhs )
        { return string_view{ lhs } < string_view{ rhs }; } );
    }

    bool InspectorFavorites::add( string_view key )
    {
        if ( key.empty() )
            return false;
        auto it = std::lower_bound( _listKey.begin(), _listKey.end(), key, []( const string& lhs, string_view rhs )
        { return string_view{ lhs } < rhs; } );
        if ( it != _listKey.end() && string_view{ *it } == key )
            return false;
        _listKey.insert( it, string{ key } );
        return true;
    }

    bool InspectorFavorites::remove( string_view key )
    {
        auto it = std::lower_bound( _listKey.begin(), _listKey.end(), key, []( const string& lhs, string_view rhs )
        { return string_view{ lhs } < rhs; } );
        if ( it == _listKey.end() || string_view{ *it } != key )
            return false;
        _listKey.erase( it );
        return true;
    }

    bool InspectorFavorites::toggle( string_view key )
    {
        if ( remove( key ) )
            return false;
        return add( key );
    }

    bool InspectorFavorites::hasAnyForType( string_view typeName ) const
    {
        const string prefix = makeKey( typeName, {} );
        auto         it     = std::lower_bound( _listKey.begin(), _listKey.end(), string_view{ prefix }, []( const string& lhs, string_view rhs )
                    { return string_view{ lhs } < rhs; } );
        return it != _listKey.end() && string_view{ *it }.substr( 0, prefix.size() ) == string_view{ prefix };
    }

    string InspectorFavorites::makePropertyKey( const TypeInfo& type, const PropertyInfo& prop )
    {
        const TypeInfo* pDeclaring = &type;
        for ( const TypeInfo* pType = &type; pType != nullptr; pType = pType->getParentType() )
        {
            if ( pType->findProperty( prop._name ) != nullptr )
            {
                pDeclaring = pType;
                break;
            }
        }
        return makeKey( pDeclaring->_name.c_str(), prop._name.c_str() );
    }

    bool InspectorFavorites::hasAnyForTypeOrBase( const TypeInfo& type ) const
    {
        for ( const TypeInfo* pType = &type; pType != nullptr; pType = pType->getParentType() )
        {
            if ( hasAnyForType( pType->_name.c_str() ) )
                return true;
        }
        return false;
    }

    string InspectorFavorites::toText() const
    {
        string text;
        for ( const string& key : _listKey )
        {
            text += key;
            text += '\n';
        }
        return text;
    }

    void InspectorFavorites::fromText( string_view text )
    {
        _listKey.clear();
        size_t begin = 0;
        while ( begin <= text.size() )
        {
            size_t end = text.find( '\n', begin );
            if ( end == string_view::npos )
                end = text.size();
            string_view line = text.substr( begin, end - begin );
            while ( line.empty() == false && ( line.front() == ' ' || line.front() == '\t' ) )
            {
                line.remove_prefix( 1 );
            }
            while ( line.empty() == false && ( line.back() == ' ' || line.back() == '\t' || line.back() == '\r' ) )
            {
                line.remove_suffix( 1 );
            }
            (void)add( line ); // 빈 줄과 중복은 add 가 거른다
            begin = end + 1;
        }
    }

    bool InspectorFavorites::loadFile( string_view filePath )
    {
        _listKey.clear();
        if ( filePath.empty() || FileUtil::isRegularFile( filePath ) == false )
            return false;
        string text;
        if ( FileUtil::readTextFile( filePath, text ) == false )
        {
            SW_LOG_WARNING( "Inspector favorites could not be read from %#", string( filePath ).c_str() );
            return false;
        }
        fromText( FileUtil::skipUTF8Bom( text ) );
        return true;
    }

    bool InspectorFavorites::saveFile( string_view filePath ) const
    {
        if ( filePath.empty() )
            return false;
        if ( FileUtil::ensureParentDirectoryExists( filePath ) == false || FileUtil::writeTextFile( filePath, toText() ) == false )
        {
            SW_LOG_WARNING( "Inspector favorites could not be saved to %#", string( filePath ).c_str() );
            return false;
        }
        return true;
    }

    string InspectorFavorites::getDefaultFilePath()
    {
        return EditorPaths::resolveEditorStateFile( kFileName );
    }
} // namespace sw::editor
