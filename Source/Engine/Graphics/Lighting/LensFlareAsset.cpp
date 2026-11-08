#include "pch.h"

#include "Engine/Graphics/Lighting/LensFlareAsset.h"

#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    bool LensFlareAsset::loadFromAsset( const AssetID& assetID, string* pOutError )
    {
        LensFlareAsset loaded;
        if ( ReflectedXMLFile::loadDesc( assetID, loaded ) == false )
        {
            if ( pOutError != nullptr )
                *pOutError = "the lens flare file could not be read";
            return false;
        }
        if ( loaded._listElement.empty() )
        {
            if ( pOutError != nullptr )
                *pOutError = "the lens flare has no elements";
            return false;
        }
        *this = std::move( loaded );
        return true;
    }
} // namespace sw
