#include "pch.h"

#include "Engine/Text/FontCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
} // namespace sw

namespace sw
{
    bool FontCatalogDef::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        *this                       = FontCatalogDef{};
        if ( XMLSerializer::loadFromAsset( assetID, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR_ID( "FontCatalog.FontCatalogCouldNot", "[Text] Font catalog could not be read or holds unknown keys: %#", assetPath );
            return false;
        }
        return validate();
    }

    bool FontCatalogDef::loadFromXMLText( string_view xmlText )
    {
        *this = FontCatalogDef{};
        if ( XMLSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR_ID( "FontCatalog.FontCatalogTextCould", "[Text] Font catalog text could not be read or holds unknown keys" );
            return false;
        }
        return validate();
    }

    bool FontCatalogDef::validate() const
    {
        bool           bValid = true;
        vector<string> listName;
        for ( const FontFamilyDef& family : _listFamily )
        {
            listName.push_back( family._name );
        }
        for ( const SystemFontFamilyDef& family : _listSystemFamily )
        {
            listName.push_back( family._name );
        }
        for ( size_t index = 0; index < listName.size(); ++index )
        {
            if ( listName[index].empty() )
            {
                SW_LOG_ERROR_ID( "FontCatalog.FontCatalogFamilyWithout", "[Text] Font catalog has a family without a name" );
                bValid = false;
                continue;
            }
            for ( size_t other = index + 1; other < listName.size(); ++other )
            {
                if ( StringUtil::equals( listName[index], listName[other], true ) )
                {
                    SW_LOG_ERROR_ID( "FontCatalog.FontCatalogNamesFamily", "[Text] Font catalog names family '%#' twice", listName[index].c_str() );
                    bValid = false;
                }
            }
        }
        for ( const FontFamilyDef& family : _listFamily )
        {
            if ( family._listFace.empty() )
            {
                SW_LOG_ERROR_ID( "FontCatalog.FontFamilyNoFaces", "[Text] Font family '%#' has no faces", family._name.c_str() );
                bValid = false;
            }
            for ( const FontFaceDef& face : family._listFace )
            {
                if ( face._path.empty() )
                {
                    SW_LOG_ERROR_ID( "FontCatalog.FontFamilyFaceWithout", "[Text] Font family '%#' has a face without a path", family._name.c_str() );
                    bValid = false;
                }
            }
        }
        if ( findFamily( _defaultFamily ) == nullptr )
        {
            SW_LOG_ERROR_ID( "FontCatalog.FontCatalogDefaultFamily", "[Text] Font catalog default family '%#' is not a shipped family", _defaultFamily.c_str() );
            bValid = false;
        }
        return bValid;
    }

    const FontFamilyDef* FontCatalogDef::findFamily( string_view name ) const
    {
        return VectorUtil::findIf( _listFamily, [&]( const FontFamilyDef& family )
        { return StringUtil::equals( family._name, name, true ); } );
    }

    const SystemFontFaceDef* SystemFontFamilyDef::findPlatformFace( string_view platformName ) const
    {
        for ( const SystemFontFaceDef& face : _listPlatformFace )
        {
            if ( face._platform == platformName )
                return &face;
        }
        return nullptr;
    }

    const SystemFontFamilyDef* FontCatalogDef::findSystemFamily( string_view name ) const
    {
        return VectorUtil::findIf( _listSystemFamily, [&]( const SystemFontFamilyDef& family )
        { return StringUtil::equals( family._name, name, true ); } );
    }

    /** @brief 글꼴 카탈로그 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( FontCatalog, AssetTypeDef::makeDataFile( "fontcatalog", AssetPathRule{ {}, {}, "fontcatalog.xml" }, AssetTypeLoader::of<FontCatalogDef>() ) );
} // namespace sw
