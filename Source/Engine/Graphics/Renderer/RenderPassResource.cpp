#include "pch.h"

#include "Engine/Graphics/Renderer/RenderPassResource.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Renderer/RenderPassXmlUtil.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "RenderPassResource" );

    namespace
    {
    } // namespace

    bool RenderPassResource::loadFromXmlFile( string_view assetRelativePath )
    {
        string      absPath;
        XmlDocument doc;
        if ( doc.loadPath( assetRelativePath, &absPath ) == false )
        {
            SW_LOG_ERROR( "XML file not found: %#", assetRelativePath );
            return false;
        }

        XmlNode root = doc.root( "RenderPassDesc" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "XML missing root <RenderPassDesc>: %#", absPath );
            return false;
        }

        if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::RenderPass, doc, root, AssetFormatVersions::kRenderPass ) == false )
        {
            SW_LOG_ERROR( "formatVersion upgrade failed: %#", absPath );
            return false;
        }

        _desc = {};

        const utf8* pName = root.childText( "_name" );
        if ( pName == nullptr )
            pName = root.attr( "name" );
        if ( pName != nullptr )
            _desc._name = pName;

        RenderPassXmlUtil::parseAttachmentList( root.child( "_attachments" ), _desc._listAttachment );

        SW_LOG_INFO( "Loaded '%#' (Attachments: %#)",
                     _desc._name, _desc._listAttachment.size() );
        return true;
    }

    bool RenderPassResource::saveToXmlFile( string_view assetRelativePath ) const
    {
        string absPath = ResourceUtil::getResourcePath( assetRelativePath );
        if ( absPath.empty() )
            absPath = assetRelativePath;

        XmlDocument doc;

        XmlNode root = doc.appendRoot( "RenderPassDesc" );
        engine::getResourceManager().getAssetFormatRegistry().writeXmlVersion( root, AssetFormatVersions::kRenderPass );

        root.appendChild( "_name", _desc._name );

        RenderPassXmlUtil::appendAttachmentList( root, _desc._listAttachment );

        if ( doc.saveFile( absPath ) == false )
        {
            SW_LOG_ERROR( "Failed to write XML file: %#", absPath );
            return false;
        }

        SW_LOG_INFO( "Saved RenderPass '%#' to: %#", _desc._name, absPath );
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
