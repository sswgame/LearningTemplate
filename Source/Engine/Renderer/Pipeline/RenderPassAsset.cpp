#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPassAsset.h"

#include "Core/File/FileUtil.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    bool RenderPassAsset::loadFromXMLFile( const AssetID& assetID )
    {
        _desc = {};
        if ( ReflectedXMLFile::loadDesc( assetID, _desc ) == false )
            return false;

        SW_LOG_INFO_ID( "RenderPassAsset.LoadedAttachments", "Loaded '%#' (Attachments: %#)", _desc._name, _desc._listAttachment.size() );
        return true;
    }

    bool RenderPassAsset::saveToXMLFile( const AbsolutePath& absPath ) const
    {
        if ( ReflectedXMLFile::saveDesc( absPath, _desc ) == false )
            return false;

        SW_LOG_INFO_ID( "RenderPassAsset.SavedRenderPass", "Saved RenderPass '%#' to: %#", _desc._name, absPath.str() );
        return true;
    }

} // namespace sw
