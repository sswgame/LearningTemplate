#include "pch.h"

#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyGridFavorites.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorPaths.h"

#include "Engine/Reflection/TypeInfo.h"

#include <algorithm>

namespace sw::editor
{
    string PropertyGridFavorites::makeKey( string_view typeName, string_view propertyName )
    {
        string key{ typeName };
        key += '.';
        key += propertyName;
        return key;
    }

    bool PropertyGridFavorites::contains( string_view key ) const
    {
        return std::binary_search( _listKey.begin(), _listKey.end(), key, []( const auto& lhs, const auto& rhs )
        { return string_view{ lhs } < string_view{ rhs }; } );
    }

    bool PropertyGridFavorites::add( string_view key )
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

    bool PropertyGridFavorites::remove( string_view key )
    {
        auto it = std::lower_bound( _listKey.begin(), _listKey.end(), key, []( const string& lhs, string_view rhs )
        { return string_view{ lhs } < rhs; } );
        if ( it == _listKey.end() || string_view{ *it } != key )
            return false;
        _listKey.erase( it );
        return true;
    }

    bool PropertyGridFavorites::toggle( string_view key )
    {
        if ( remove( key ) )
            return false;
        return add( key );
    }

    bool PropertyGridFavorites::hasAnyForType( string_view typeName ) const
    {
        const string prefix = makeKey( typeName, {} );
        auto         it     = std::lower_bound( _listKey.begin(), _listKey.end(), string_view{ prefix }, []( const string& lhs, string_view rhs )
                    { return string_view{ lhs } < rhs; } );
        return it != _listKey.end() && string_view{ *it }.substr( 0, prefix.size() ) == string_view{ prefix };
    }

    string PropertyGridFavorites::makePropertyKey( const TypeInfo& type, const PropertyInfo& prop )
    {
        const TypeInfo* pDeclaring = &type;
        for ( const TypeInfo* pType : type.getTypeChain() )
        {
            if ( pType->findProperty( prop._name ) != nullptr )
            {
                pDeclaring = pType;
                break;
            }
        }
        return makeKey( pDeclaring->_name.c_str(), prop._name.c_str() );
    }

    string PropertyGridFavorites::makeMethodKey( const TypeInfo& type, const FunctionInfo& method )
    {
        const TypeInfo* pDeclaring = &type;
        for ( const TypeInfo* pType : type.getTypeChain() )
        {
            if ( pType->findMethod( method._hashName ) != nullptr )
            {
                pDeclaring = pType;
                break;
            }
        }
        return makeKey( pDeclaring->_name.c_str(), method._name );
    }

    bool PropertyGridFavorites::hasAnyForTypeOrBase( const TypeInfo& type ) const
    {
        for ( const TypeInfo* pType : type.getTypeChain() )
        {
            if ( hasAnyForType( pType->_name.c_str() ) )
                return true;
        }
        return false;
    }

    string PropertyGridFavorites::toText() const
    {
        string text;
        for ( const string& key : _listKey )
        {
            text += key;
            text += '\n';
        }
        return text;
    }

    void PropertyGridFavorites::fromText( string_view text )
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

    bool PropertyGridFavorites::loadFromFile( const AbsolutePath& filePath )
    {
        _listKey.clear();
        if ( filePath.empty() )
            return false;
        string text;
        if ( FileUtil::readOptionalTextFile( filePath, text ) != OptionalFileRead::Read )
            return false; // 없으면 처음이다, 못 읽었으면 readOptionalTextFile 이 오류로 남겼다
        fromText( FileUtil::skipUTF8Bom( text ) );
        return true;
    }

    bool PropertyGridFavorites::saveToFile( const AbsolutePath& filePath ) const
    {
        if ( filePath.empty() )
            return false;
        if ( FileUtil::writeTextFile( filePath, toText() ) == false )
        {
            SW_LOG_WARNING_ID( "PropertyGridFavorites.CouldNotSave", "Inspector favorites could not be saved to %#", string( filePath.str() ).c_str() );
            return false;
        }
        return true;
    }

    AbsolutePath PropertyGridFavorites::getFilePath() const
    {
        return EditorPaths::resolveEditorStateFile( _pFileName );
    }
} // namespace sw::editor
