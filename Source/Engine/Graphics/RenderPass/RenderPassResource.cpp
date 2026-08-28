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
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::fileExists( absPath ) == false )
		{
			SW_LOG_ERROR( "XML file not found: %#", absPath );
			return false;
		}

		vector<uint8> listFileData;
		if ( FileUtil::readFile( absPath, listFileData ) == false )
		{
			SW_LOG_ERROR( "Failed to open XML file: %#", absPath );
			return false;
		}

		if ( listFileData.empty() )
		{
			SW_LOG_ERROR( "XML file is empty: %#", absPath );
			return false;
		}

		string		xmlStr( reinterpret_cast<const utf8*>( listFileData.data() ), listFileData.size() );
		XmlDocument doc;
		if ( doc.parse( xmlStr ) == false )
		{
			SW_LOG_ERROR( "Failed to parse XML: %#", absPath );
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
				const utf8* pAttClear = attNode.childText( "_bClear" );
				if ( pAttClear != nullptr )
					att._bClear = ( string( pAttClear ) == "1" || string( pAttClear ) == "true" );
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

		XmlNode nameNode = root.appendChild( "_name" );
		nameNode.setValue( _desc._name.c_str() );

		XmlNode attachsNode = root.appendChild( "_attachments" );

		for ( const RenderPassAttachment& att : _desc._listAttachment )
		{
			XmlNode attNode = attachsNode.appendChild( "item" );

			XmlNode nName = attNode.appendChild( "_name" );
			nName.setValue( att._name.c_str() );

			XmlNode nFormat = attNode.appendChild( "_format" );
			nFormat.setValue( att._format.c_str() );

			XmlNode nClear = attNode.appendChild( "_bClear" );
			nClear.setValue( att._bClear ? "1" : "0" );

			StringBuilder<constant::kMaxBuffer128> colorSS;
			constexpr Format					   colorFmt( 4 );
			colorSS.appendFormat( "%#,%#,%#,%#",
								  Fmt( att._arrClearColor[0], colorFmt ),
								  Fmt( att._arrClearColor[1], colorFmt ),
								  Fmt( att._arrClearColor[2], colorFmt ),
								  Fmt( att._arrClearColor[3], colorFmt ) );
			XmlNode nColor = attNode.appendChild( "_clearColor" );
			nColor.setValue( colorSS.c_str() );
		}

		string xmlStr = doc.saveToString();

		if ( FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( xmlStr.data() ), xmlStr.size() ) == false )
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
