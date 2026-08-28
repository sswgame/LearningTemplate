#include "pch.h"

#include "Engine/Graphics/RenderPass/RenderPassResource.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RenderPass/RenderPassXmlUtil.h"
#include "Engine/Utility/Resource/AssetFormat.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	SW_LOG_CALLER( "RenderPassResource" );

	namespace
	{
	} // namespace

	bool RenderPassResource::loadFromXmlFile( string_view assetRelativePath )
	{
		string		absPath;
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

		_desc._listAttachment.clear();
		XmlNode attachsNode = root.child( "_attachments" );
		if ( attachsNode.isValid() )
		{
			for ( XmlNode attNode = attachsNode.child( "item" ); attNode.isValid(); attNode = attNode.next( "item" ) )
			{
				RenderPassAttachment att{};

				const utf8* pAttName = attNode.childText( "_name" );
				if ( pAttName != nullptr )
					att._name = pAttName;
				const utf8* pAttFormat = attNode.childText( "_format" );
				if ( pAttFormat != nullptr )
					att._format = pAttFormat;
				att._bClear				   = attNode.childBool( "_bClear", false );
				const utf8* pAttClearColor = attNode.childText( "_clearColor" );
				if ( pAttClearColor != nullptr )
				{
					if ( std::sscanf( pAttClearColor, "%f,%f,%f,%f", &att._arrClearColor[0], &att._arrClearColor[1], &att._arrClearColor[2], &att._arrClearColor[3] ) < 4 )
						std::sscanf( pAttClearColor, "%f %f %f %f", &att._arrClearColor[0], &att._arrClearColor[1], &att._arrClearColor[2], &att._arrClearColor[3] );
				}

				_desc._listAttachment.push_back( std::move( att ) );
			}
		}

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

		XmlNode attachsNode = root.appendChild( "_attachments" );

		for ( const RenderPassAttachment& att : _desc._listAttachment )
		{
			XmlNode attNode = attachsNode.appendChild( "item" );
			attNode.appendChild( "_name", att._name );
			attNode.appendChild( "_format", att._format );
			attNode.appendChild( "_bClear", att._bClear );

			StringBuilder<constant::kMaxBuffer128> colorSS;
			constexpr Format					   colorFmt( 4 );
			colorSS.appendFormat( "%#,%#,%#,%#",
								  Fmt( att._arrClearColor[0], colorFmt ),
								  Fmt( att._arrClearColor[1], colorFmt ),
								  Fmt( att._arrClearColor[2], colorFmt ),
								  Fmt( att._arrClearColor[3], colorFmt ) );
			attNode.appendChild( "_clearColor", colorSS.view() );
		}

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
