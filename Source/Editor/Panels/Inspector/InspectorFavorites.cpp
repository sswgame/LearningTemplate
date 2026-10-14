#include "pch.h"

#include "Editor/Panels/Inspector/InspectorFavorites.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/EditorPaths.h"

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
                line.remove_prefix( 1 );
            while ( line.empty() == false && ( line.back() == ' ' || line.back() == '\t' || line.back() == '\r' ) )
                line.remove_suffix( 1 );
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
        fromText( FileUtil::skipUtf8Bom( text ) );
        return true;
    }

    bool InspectorFavorites::saveFile( string_view filePath ) const
    {
        if ( filePath.empty() )
            return false;
        FileUtil::ensureParentDirectoryExists( filePath );
        if ( FileUtil::writeTextFile( filePath, toText() ) == false )
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
