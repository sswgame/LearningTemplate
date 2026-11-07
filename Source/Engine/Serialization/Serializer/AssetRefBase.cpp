#include "pch.h"

#include "Engine/Serialization/Serializer/AssetRefBase.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    bool AssetRefBase::isPathOfTarget( string_view path, const AssetTypeDef& type )
    {
        if ( type.isBuiltinName( path ) )
            return true;
        for ( uint32 index = 0; index < type._suffixCount; ++index )
        {
            if ( StringUtil::endsWith( path, type._pSuffix[index]._text, true ) )
                return true;
        }
        // 접미사가 있는 종류는 접미사로만 가른다. 접미사가 없는 카탈로그는 경로 규칙(`/data/crops.xml`)으로 가른다.
        if ( type._suffixCount > 0 || type._pathRule.isEmpty() )
            return false;
        return type._pathRule.matches( path );
    }

    bool AssetRefBase::isBuiltin() const
    {
        return _pTargetType->isBuiltinName( _path );
    }

    bool AssetRefBase::assign( string_view path )
    {
        const string_view trimmed = StringUtil::trim( path );
        if ( trimmed.empty() == false && isPathOfTarget( trimmed, *_pTargetType ) == false )
        {
            SW_LOG_ERROR_ID( "AssetRef.WrongType", "'%#' is not a %# asset - the reference keeps '%#'", trimmed, _pTargetType->_name, _path );
            return false;
        }
        _path = string( trimmed );
        _key  = trimmed.empty() ? hashed_string{} : hashed_string( _path.c_str() );
        return true;
    }

    void AssetRefBase::registerSerializeHandler( SerializeContext& context )
    {
        context.registerTextHandler(
            hashed_string( kTypeName ),
            []( const void* pPtr )
        { return static_cast<const AssetRefBase*>( pPtr )->_path; },
            []( void* pPtr, string_view strView )
        { return static_cast<AssetRefBase*>( pPtr )->assign( strView ); } );
    }
} // namespace sw
