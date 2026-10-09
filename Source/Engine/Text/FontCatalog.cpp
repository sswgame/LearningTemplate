#include "pch.h"

#include "Engine/Text/FontCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "FontCatalog" );
} // namespace sw

namespace sw
{
    bool FontCatalogDesc::loadFromResource( string_view resourcePath )
    {
        *this = FontCatalogDesc{};
        if ( XmlSerializer::loadFile( resourcePath, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR( "[Text] Font catalog could not be read or holds unknown keys: %#", resourcePath );
            return false;
        }
        return validate();
    }

    bool FontCatalogDesc::loadFromXmlText( string_view xmlText )
    {
        *this = FontCatalogDesc{};
        if ( XmlSerializer::deserialize( this, *StaticType(), xmlText ) == false )
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
        for ( const FontFamilyDesc& family : _listFamily )
        {
            if ( StringUtil::equals( family._name, name, true ) )
                return &family;
        }
        return nullptr;
    }

    const SystemFontFamilyDesc* FontCatalogDesc::findSystemFamily( string_view name ) const
    {
        for ( const SystemFontFamilyDesc& family : _listSystemFamily )
        {
            if ( StringUtil::equals( family._name, name, true ) )
                return &family;
        }
        return nullptr;
    }
} // namespace sw
