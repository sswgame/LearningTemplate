#include "pch.h"

#include "GameFramework/Kits/Farming/CropCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "CropCatalog" );

    namespace
    {
        struct CropCatalogInternal
        {
            /** @brief "Spring,Fall" 같은 목록을 계절 마스크로 읽습니다. 모르는 이름은 경고하고 건너뜁니다. */
            static uint8 parseSeasonMask( string_view text, string_view sourceName, const utf8* pCropId )
            {
                (void)sourceName;
                (void)pCropId;
                uint8  mask       = 0;
                size_t tokenStart = 0;
                while ( tokenStart <= text.size() )
                {
                    size_t tokenEnd = text.find_first_of( ",;| ", tokenStart );
                    if ( tokenEnd == string_view::npos )
                        tokenEnd = text.size();
                    const string_view token = text.substr( tokenStart, tokenEnd - tokenStart );
                    if ( token.empty() == false )
                    {
                        FarmSeason season{ FarmSeason::Spring };
                        if ( parseFarmSeason( token, season ) )
                            mask = static_cast<uint8>( mask | makeFarmSeasonBit( season ) );
                        else
                            SW_LOG_WARNING( "%#: crop '%#' has an unknown season in '%#'", sourceName, pCropId, text );
                    }
                    tokenStart = tokenEnd + 1;
                }
                return mask;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CropCatalog::CropCatalog()
        : _listCrop{}
    {
    }

    bool CropCatalog::loadFromResource( string_view path )
    {
        XmlDocument doc;
        string      absPath;
        if ( doc.loadPath( path, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to read crop catalog %#", path );
            return false;
        }
        const XmlNode root = doc.getRoot( "CropCatalog" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <CropCatalog> root in %#", absPath );
            return false;
        }
        return loadRoot( root, absPath ) > 0;
    }

    bool CropCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_WARNING( "Failed to parse crop catalog text %#", sourceName );
            return false;
        }
        const XmlNode root = doc.getRoot( "CropCatalog" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <CropCatalog> root in %#", sourceName );
            return false;
        }
        return loadRoot( root, sourceName ) > 0;
    }

    void CropCatalog::addCrop( const CropDef& crop )
    {
        for ( CropDef& existing : _listCrop )
        {
            if ( existing._id == crop._id )
            {
                existing = crop;
                return;
            }
        }
        _listCrop.push_back( crop );
    }

    const CropDef* CropCatalog::findCrop( const hashed_string& cropId ) const
    {
        for ( const CropDef& crop : _listCrop )
        {
            if ( crop._id == cropId )
                return &crop;
        }
        return nullptr;
    }

    const CropDef* CropCatalog::findCropBySeed( const hashed_string& seedItem ) const
    {
        for ( const CropDef& crop : _listCrop )
        {
            if ( crop._seedItem == seedItem )
                return &crop;
        }
        return nullptr;
    }

    int32 CropCatalog::findSellPrice( const hashed_string& itemId ) const
    {
        for ( const CropDef& crop : _listCrop )
        {
            if ( crop._produceItem == itemId )
                return crop._sellPrice;
            if ( crop._seedItem == itemId )
                return crop._seedPrice / 2;
        }
        return 0;
    }

    uint32 CropCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Crop" ); node; node = node.findNextSibling( "Crop" ) )
        {
            const utf8* pId = node.findAttribute( "id" );
            if ( StringUtil::isNullOrEmpty( pId ) )
            {
                SW_LOG_WARNING( "%#: <Crop> without an id - skipped", sourceName );
                continue;
            }
            CropDef crop;
            crop._id             = hashed_string( pId );
            const utf8* pName    = node.findAttribute( "name" );
            crop._name           = pName != nullptr ? pName : pId;
            const utf8* pSeed    = node.findAttribute( "seed" );
            const utf8* pProduce = node.findAttribute( "produce" );
            crop._seedItem       = hashed_string( pSeed != nullptr ? pSeed : pId );
            crop._produceItem    = hashed_string( pProduce != nullptr ? pProduce : pId );
            crop._growthDays     = MathUtil::max( 1, node.getAttributeInt( "days", crop._growthDays ) );
            crop._regrowDays     = MathUtil::max( 0, node.getAttributeInt( "regrow", crop._regrowDays ) );
            crop._seedPrice      = MathUtil::max( 0, node.getAttributeInt( "seedPrice", crop._seedPrice ) );
            crop._sellPrice      = MathUtil::max( 0, node.getAttributeInt( "sellPrice", crop._sellPrice ) );
            crop._harvestCount   = MathUtil::max( 1, node.getAttributeInt( "harvest", crop._harvestCount ) );
            const utf8* pSeasons = node.findAttribute( "seasons" );
            crop._seasonMask     = pSeasons != nullptr ? CropCatalogInternal::parseSeasonMask( string_view( pSeasons ), sourceName, pId )
                                                       : makeFarmSeasonBit( FarmSeason::Spring );
            if ( crop._seasonMask == 0 )
            {
                SW_LOG_WARNING( "%#: crop '%#' grows in no season - skipped", sourceName, pId );
                continue;
            }
            if ( crop._regrowDays > crop._growthDays )
                SW_LOG_WARNING( "%#: crop '%#' regrows slower (%#) than it first grows (%#)", sourceName, pId, crop._regrowDays, crop._growthDays );
            addCrop( crop );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Crop> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
