#include "pch.h"

#include "Engine/Animation/Sprite/SpriteLibrary.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw
{
    bool SpriteLibrary::loadFromAsset( const AssetID& assetID )
    {
        clear();
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "SpriteLibrary.CouldNotRead", "Sprite library '%#' could not be read", assetID.view() );
            return false;
        }
        return parseXML( text, assetID.view() );
    }

    bool SpriteLibrary::parseXML( string_view xml, string_view sourceLabel )
    {
        clear();
        string flatText;
        if ( DataInheritance::findBasePath( xml ).empty() == false && DataInheritance::flattenReflected( xml, sourceLabel, *StaticType(), flatText ) == false )
            return false;
        const string label{ sourceLabel };
        if ( ConfigManager::readConfigXMLDocument( this, *StaticType(), flatText.empty() ? xml : string_view{ flatText }, label.c_str() ) && resolveLoadedValues( sourceLabel ) )
            return true;
        clear();
        return false;
    }

    void SpriteLibrary::clear()
    {
        _atlasPath.clear();
        _atlasPixelSize = float2{ 0.0f, 0.0f };
        _listCategory.clear();
    }

    bool SpriteLibrary::resolveLoadedValues( string_view sourceLabel )
    {
        if ( _atlasPath.empty() )
        {
            SW_LOG_ERROR_ID( "SpriteLibrary.NoAtlas", "Sprite library '%#' has no _atlasPath", sourceLabel );
            return false;
        }
        bool bValid = true;
        for ( SpriteLibraryCategory& category : _listCategory )
        {
            if ( VectorUtil::findBy<&SpriteLibraryCategory::_name>( _listCategory, category._name ) != &category )
            {
                SW_LOG_ERROR_ID( "SpriteLibrary.CategoryRepeated", "Sprite library '%#' lists category '%#' twice", sourceLabel, category._name.c_str() );
                bValid = false;
            }
            for ( SpriteLibraryLabel& label : category._listLabel )
            {
                if ( VectorUtil::findBy<&SpriteLibraryLabel::_name>( category._listLabel, label._name ) != &label )
                {
                    SW_LOG_ERROR_ID( "SpriteLibrary.LabelRepeated", "Sprite library '%#' lists label '%#/%#' twice", sourceLabel, category._name.c_str(), label._name.c_str() );
                    bValid = false;
                }
                if ( SpriteClipFrame::makeBorderFractions( label._uvRect, label._borderPixels, _atlasPixelSize, label._border ) == false )
                {
                    SW_LOG_ERROR_ID( "SpriteLibrary.BorderNeedsAtlasSize", "Sprite library '%#' label '%#/%#' has a pixel border but the library has no _atlasPixelSize", sourceLabel, category._name.c_str(), label._name.c_str() );
                    bValid = false;
                }
            }
        }
        return bValid;
    }

    const SpriteLibraryLabel* SpriteLibrary::findLabel( string_view category, string_view label ) const
    {
        const SpriteLibraryCategory* pCategory = VectorUtil::findBy<&SpriteLibraryCategory::_name>( _listCategory, category );
        return pCategory != nullptr ? VectorUtil::findBy<&SpriteLibraryLabel::_name>( pCategory->_listLabel, label ) : nullptr;
    }

    SW_ASSET_TYPE( SpriteLibrary, SpriteLibrary::kAssetType );
} // namespace sw
