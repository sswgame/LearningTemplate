#include "pch.h"

#include "Engine/Graphics/Renderer/Pipeline/RenderPassAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Serialization/Format/ReflectedXmlFile.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "RenderPassAsset" );

    bool RenderPassAsset::loadFromXmlFile( string_view assetRelativePath )
    {
        _desc = {};
        if ( ReflectedXmlFile::loadDesc( assetRelativePath, _desc ) == false )
            return false;

        SW_LOG_INFO( "Loaded '%#' (Attachments: %#)", _desc._name, _desc._listAttachment.size() );
        return true;
    }

    bool RenderPassAsset::saveToXmlFile( string_view assetRelativePath ) const
    {
        if ( ReflectedXmlFile::saveDesc( assetRelativePath, _desc ) == false )
            return false;

        SW_LOG_INFO( "Saved RenderPass '%#' to: %#", _desc._name, assetRelativePath );
        return true;
    }

    TaskHandle RenderPassAsset::loadFromXmlFileAsync( string_view assetRelativePath )
    {
        TaskHandle handle = engine::getTaskManager().emplaceTask(
            "LoadRenderPassAsync",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, RenderPassAsset::loadFromXmlFileAsyncJob ),
            MakeTaskArgs( this, string( assetRelativePath ) ) );
        handle.submit();
        return handle;
    }

    void RenderPassAsset::loadFromXmlFileAsyncJob( const TaskArgs& args )
    {
        RenderPassAsset* pResource = args.get<RenderPassAsset*>( 0 );
        if ( pResource == nullptr )
            return;
        if ( pResource->loadFromXmlFile( args.get<string>( 1 ) ) == false )
            SW_LOG_WARNING( "Could not load '%#'", args.get<string>( 1 ) );
    }

} // namespace sw
