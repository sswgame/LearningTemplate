#include "pch.h"

#include "Engine/Graphics/Lighting/LensFlareAsset.h"

#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    expected<void> LensFlareAsset::loadFromAsset( const AssetID& assetID )
    {
        LensFlareAsset loaded;
        if ( ReflectedXMLFile::loadDesc( assetID, loaded ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the lens flare file could not be read" } );
        if ( loaded._listElement.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the lens flare has no elements" } );
        *this = std::move( loaded );
        return {};
    }
} // namespace sw
