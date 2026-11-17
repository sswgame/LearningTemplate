#include "pch.h"

#include "Engine/Serialization/Serializer/AssetRefBase.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetRegistry.h"
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

    bool AssetRefBase::assign( string_view text )
    {
        const string_view   trimmed = StringUtil::trim( text );
        string              path{ trimmed };
        optional<AssetGUID> guid;
        if ( AssetRegistry::isReferenceText( trimmed ) )
        {
            guid = AssetRegistry::parseReferenceText( trimmed );
            if ( guid.has_value() == false )
            {
                SW_LOG_ERROR_ID( "AssetRef.InvalidGUID", "'%#' is not a GUID reference (guid: and 32 lowercase hex digits) - the reference keeps '%#'", trimmed, _path );
                return false;
            }
            optional<string> resolved = AssetRegistry::resolve( *guid );
            if ( resolved.has_value() == false )
            {
                SW_LOG_ERROR_ID( "AssetRef.UnknownGUID", "'%#' (%# asset) is not in the asset index - the asset was deleted or the index is not installed; the reference keeps '%#'", trimmed,
                                 _pTargetType->_name, _path );
                return false;
            }
            path = std::move( *resolved );
        }
        if ( path.empty() == false && isPathOfTarget( path, *_pTargetType ) == false )
        {
            SW_LOG_ERROR_ID( "AssetRef.WrongType", "'%#' is not a %# asset - the reference keeps '%#'", path, _pTargetType->_name, _path );
            return false;
        }
        // 경로 글(코드의 대입 · 옮기는 동안의 옛 파일)은 색인에서 정체성을 찾아 둔다 — 다시 쓰면 GUID 로 적힌다. 내장 이름은 파일이 아니라 정체성이 없다.
        if ( guid.has_value() == false && path.empty() == false && _pTargetType->isBuiltinName( path ) == false )
            guid = AssetRegistry::resolveGUID( path );
        _path = std::move( path );
        _key  = _path.empty() ? hashed_string{} : hashed_string( _path.c_str() );
        _guid = guid;
        return true;
    }

    string AssetRefBase::toText() const
    {
        return _guid.has_value() ? AssetRegistry::makeReferenceText( *_guid ) : _path;
    }

    void AssetRefBase::registerSerializeHandler( SerializeContext& context )
    {
        context.registerTextHandler(
            hashed_string( kTypeName ),
            []( const void* pPtr )
        { return static_cast<const AssetRefBase*>( pPtr )->toText(); },
            []( void* pPtr, string_view strView )
        { return static_cast<AssetRefBase*>( pPtr )->assign( strView ); } );
    }
} // namespace sw
