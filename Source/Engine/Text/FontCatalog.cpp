#include "pch.h"

#include "Engine/Text/FontCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "FontCatalog" );
} // namespace sw

namespace sw
{
    bool FontCatalogDesc::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        *this                       = FontCatalogDesc{};
        if ( XMLSerializer::loadFile( assetID, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR( "[Text] Font catalog could not be read or holds unknown keys: %#", assetPath );
            return false;
        }
        return validate();
    }

    bool FontCatalogDesc::loadFromXMLText( string_view xmlText )
    {
        *this = FontCatalogDesc{};
        if ( XMLSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR( "[Text] Font catalog text could not be read or holds unknown keys" );
            return false;
        }
        return validate();
    }

    bool FontCatalogDesc::validate() const
    {
        bool           bValid = true;
        vector<string> listName;
        for ( const FontFamilyDesc& family : _listFamily )
        {
            listName.push_back( family._name );
        }
        for ( const SystemFontFamilyDesc& family : _listSystemFamily )
        {
            listName.push_back( family._name );
        }
        for ( size_t index = 0; index < listName.size(); ++index )
        {
            if ( listName[index].empty() )
            {
                SW_LOG_ERROR( "[Text] Font catalog has a family without a name" );
                bValid = false;
                continue;
            }
            for ( size_t other = index + 1; other < listName.size(); ++other )
            {
                if ( StringUtil::equals( listName[index], listName[other], true ) )
                {
                    SW_LOG_ERROR( "[Text] Font catalog names family '%#' twice", listName[index].c_str() );
                    bValid = false;
                }
            }
        }
        for ( const FontFamilyDesc& family : _listFamily )
        {
            if ( family._listFace.empty() )
            {
                SW_LOG_ERROR( "[Text] Font family '%#' has no faces", family._name.c_str() );
                bValid = false;
            }
            for ( const FontFaceDesc& face : family._listFace )
            {
                if ( face._path.empty() )
                {
                    SW_LOG_ERROR( "[Text] Font family '%#' has a face without a path", family._name.c_str() );
                    bValid = false;
                }
            }
        }
        if ( findFamily( _defaultFamily ) == nullptr )
        {
            SW_LOG_ERROR( "[Text] Font catalog default family '%#' is not a shipped family", _defaultFamily.c_str() );
            bValid = false;
        }
        return bValid;
    }

    const FontFamilyDesc* FontCatalogDesc::findFamily( string_view name ) const
    {
        return VectorUtil::findIf( _listFamily, [&]( const FontFamilyDesc& family )
        { return StringUtil::equals( family._name, name, true ); } );
    }

    const SystemFontFamilyDesc* FontCatalogDesc::findSystemFamily( string_view name ) const
    {
        return VectorUtil::findIf( _listSystemFamily, [&]( const SystemFontFamilyDesc& family )
        { return StringUtil::equals( family._name, name, true ); } );
    }

    /** @brief 글꼴 카탈로그 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_RESOURCE_DATA_KIND( FontCatalog, "fontcatalog", nullptr, nullptr, "fontcatalog.xml", &AssetDataKindUtil::loadCatalog<FontCatalogDesc> );
} // namespace sw
