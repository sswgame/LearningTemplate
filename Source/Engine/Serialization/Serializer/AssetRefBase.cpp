#include "pch.h"

#include "Engine/Serialization/Serializer/AssetRefBase.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    bool AssetRefBase::isPathOfTarget( string_view path, const AssetTypeDef& type )
    {
        if ( type._suffixCount == 0 )
            return true;
        for ( uint32 index = 0; index < type._suffixCount; ++index )
        {
            if ( StringUtil::endsWith( path, type._pSuffix[index]._text, true ) )
                return true;
        }
        return false;
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
