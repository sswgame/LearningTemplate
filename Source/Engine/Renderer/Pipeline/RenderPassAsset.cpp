#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPassAsset.h"

#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    bool RenderPassAsset::loadFromXMLFile( const AssetID& assetID )
    {
        _desc = {};
        return ReflectedXMLFile::loadDesc( assetID, _desc );
    }
} // namespace sw
