#include "pch.h"

#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderResourceXml.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "RenderPassResource" );

    bool RenderPassResource::loadFromXmlFile( string_view assetRelativePath )
    {
        _desc = {};
        if ( RenderResourceXml::loadDesc( assetRelativePath, _desc ) == false )
            return false;

        SW_LOG_INFO( "Loaded '%#' (Attachments: %#)", _desc._name, _desc._listAttachment.size() );
        return true;
    }

    bool RenderPassResource::saveToXmlFile( string_view assetRelativePath ) const
    {
        if ( RenderResourceXml::saveDesc( assetRelativePath, _desc ) == false )
            return false;

        SW_LOG_INFO( "Saved RenderPass '%#' to: %#", _desc._name, assetRelativePath );
        return true;
    }

    TaskHandle RenderPassResource::loadFromXmlFileAsync( string_view assetRelativePath )
    {
        TaskHandle handle = engine::getTaskManager().emplaceTask(
            "LoadRenderPassAsync",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, RenderPassResource::loadFromXmlFileAsyncJob ),
            MakeTaskArgs( this, string( assetRelativePath ) ) );
        handle.submit();
        return handle;
    }

    void RenderPassResource::loadFromXmlFileAsyncJob( const TaskArgs& args )
    {
        RenderPassResource* pResource = args.get<RenderPassResource*>( 0 );
        if ( pResource == nullptr )
            return;
        pResource->loadFromXmlFile( args.get<string>( 1 ) );
    }

} // namespace sw
