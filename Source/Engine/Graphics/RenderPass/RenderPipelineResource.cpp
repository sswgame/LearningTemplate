#include "pch.h"

#include "Engine/Graphics/RenderPass/RenderPipelineResource.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RenderPass/RenderPassXmlUtil.h"
#include "Engine/Utility/Resource/AssetFormat.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	namespace
	{
		struct RenderPipelineResourceInternal
		{
			static void parsePipelineGraphPasses( XmlNode passesNode, vector<RenderGraphPassDesc>& outListPasses )
			{
				outListPasses.clear();
				if ( passesNode.isValid() == false )
					return;

				for ( XmlNode passNode = passesNode.child( "item" ); passNode.isValid(); passNode = passNode.next( "item" ) )
				{
					RenderGraphPassDesc pass{};
					const utf8*			pName = passNode.childText( "_name" );
					if ( pName == nullptr )
						pName = passNode.attr( "name" );
					if ( pName != nullptr )
						pass._name = pName;

					const utf8* pType = passNode.childText( "_type" );
					if ( pType == nullptr )
						pType = passNode.attr( "type" );
					if ( pType != nullptr )
						pass._type = pType;

					parseStringList( passNode, "_inputs", pass._listInput );
					parseStringList( passNode, "_outputs", pass._listOutput );
					parseStringList( passNode, "_permutations", pass._listPermutation );

					const utf8* pShaderPath = passNode.childText( "_shaderPath" );
					if ( pShaderPath != nullptr )
						pass._shaderPath = pShaderPath;

					const utf8* pVertexEntryPoint = passNode.childText( "_vertexEntryPoint" );
					if ( pVertexEntryPoint != nullptr )
						pass._vertexEntryPoint = pVertexEntryPoint;

					const utf8* pPixelEntryPoint = passNode.childText( "_pixelEntryPoint" );
					if ( pPixelEntryPoint != nullptr )
						pass._pixelEntryPoint = pPixelEntryPoint;

					const utf8* pComputeEntryPoint = passNode.childText( "_computeEntryPoint" );
					if ( pComputeEntryPoint != nullptr )
						pass._computeEntryPoint = pComputeEntryPoint;

					const utf8* pCullMode = passNode.childText( "_cullMode" );
					if ( pCullMode != nullptr )
						pass._cullMode = pCullMode;

					pass._bEnableDepthTest	= passNode.childBool( "_bEnableDepthTest", pass._bEnableDepthTest );
					pass._bEnableDepthWrite = passNode.childBool( "_bEnableDepthWrite", pass._bEnableDepthWrite );
					pass._bEnableBlend		= passNode.childBool( "_bEnableBlend", pass._bEnableBlend );

					outListPasses.push_back( std::move( pass ) );
				}

				for ( XmlNode passNode = passesNode.child( "Pass" ); passNode.isValid(); passNode = passNode.next( "Pass" ) )
				{
					RenderGraphPassDesc pass{};
					const utf8*			pName = passNode.attr( "name" );
					if ( pName != nullptr )
						pass._name = pName;
					const utf8* pType = passNode.attr( "type" );
					if ( pType != nullptr )
						pass._type = pType;
					parseStringList( passNode, "inputs", pass._listInput );
					parseStringList( passNode, "outputs", pass._listOutput );
					if ( pass._listInput.empty() )
						parseStringList( passNode, "_inputs", pass._listInput );
					if ( pass._listOutput.empty() )
						parseStringList( passNode, "_outputs", pass._listOutput );
					parseStringList( passNode, "_permutations", pass._listPermutation );
					outListPasses.push_back( std::move( pass ) );
				}
			}

			static void parseAttachments( XmlNode attachsNode, vector<RenderPassAttachment>& outListAttachments )
			{
				outListAttachments.clear();
				if ( attachsNode.isValid() == false )
					return;

				for ( XmlNode attNode = attachsNode.child( "item" ); attNode.isValid(); attNode = attNode.next( "item" ) )
				{
					RenderPassAttachment att{};
					const utf8*			 pName = attNode.childText( "_name" );
					if ( pName != nullptr )
						att._name = pName;
					const utf8* pFormat = attNode.childText( "_format" );
					if ( pFormat != nullptr )
						att._format = pFormat;
					att._bClear				= attNode.childBool( "_bClear", att._bClear );
					const utf8* pClearColor = attNode.childText( "_clearColor" );
					if ( pClearColor != nullptr )
					{
						if ( std::sscanf( pClearColor, "%f,%f,%f,%f", &att._arrClearColor[0], &att._arrClearColor[1], &att._arrClearColor[2], &att._arrClearColor[3] ) < 4 )
							std::sscanf( pClearColor, "%f %f %f %f", &att._arrClearColor[0], &att._arrClearColor[1], &att._arrClearColor[2], &att._arrClearColor[3] );
					}
					outListAttachments.push_back( std::move( att ) );
				}
			}

			static string guessShadingModel( string_view name, const vector<RenderGraphPassDesc>& listPasses )
			{
				const string nameNt( name );
				const string lower = StringUtil::toLower( nameNt.c_str() );
				if ( lower.find( "deferred" ) != string::npos )
					return "Deferred";
				for ( const RenderGraphPassDesc& passDesc : listPasses )
				{
					if ( passDesc._type == "GBuffer" || passDesc._type == "Shading" || passDesc._type == "Lighting" )
						return "Deferred";
				}
				return "Forward";
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "RenderPipelineResource" );

	bool RenderPipelineResource::loadFromXmlFile( string_view assetRelativePath )
	{
		string		absPath;
		XmlDocument doc;
		if ( doc.loadPath( assetRelativePath, &absPath ) == false )
		{
			SW_LOG_ERROR( "XML file not found: %#", assetRelativePath );
			return false;
		}

		XmlNode root = doc.root( "RenderPipelineDesc" );
		if ( root.isValid() == false )
		{
			SW_LOG_ERROR( "Missing <RenderPipelineDesc>: %#", absPath );
			return false;
		}

		if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::RenderPipeline, doc, root,
																			   AssetFormatVersions::kRenderPipeline ) == false )
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

		const utf8* pShadingModel = root.childText( "_shadingModel" );
		if ( pShadingModel != nullptr )
			_desc._shadingModel = pShadingModel;

		RenderPipelineResourceInternal::parseAttachments( root.child( "_attachments" ), _desc._listAttachment );

		XmlNode passesNode = root.child( "_passes" );
		if ( passesNode.isValid() )
			RenderPipelineResourceInternal::parsePipelineGraphPasses( passesNode, _desc._listPass );

		parseStringList( root, "_renderPassRefs", _desc._listRenderPassRef );

		if ( _desc._shadingModel.empty() || _desc._shadingModel == "Forward" )
			_desc._shadingModel = RenderPipelineResourceInternal::guessShadingModel( _desc._name, _desc._listPass );

		SW_LOG_INFO( "Loaded '%#' (model=%#, attachments=%#, passes=%#)",
					 _desc._name, _desc._shadingModel, _desc._listAttachment.size(), _desc._listPass.size() );
		return true;
	}

	bool RenderPipelineResource::saveToXmlFile( string_view assetRelativePath ) const
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		XmlDocument doc;
		XmlNode		root = doc.appendRoot( "RenderPipelineDesc" );
		engine::getResourceManager().getAssetFormatRegistry().writeXmlVersion( root, AssetFormatVersions::kRenderPipeline );

		root.appendChild( "_name", _desc._name );
		root.appendChild( "_shadingModel", _desc._shadingModel );

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

		XmlNode passesNode = root.appendChild( "_passes" );
		for ( const RenderGraphPassDesc& pass : _desc._listPass )
		{
			XmlNode passNode = passesNode.appendChild( "item" );
			passNode.appendChild( "_name", pass._name );
			passNode.appendChild( "_type", pass._type );

			appendStringList( passNode, "_inputs", pass._listInput );
			appendStringList( passNode, "_outputs", pass._listOutput );
			if ( pass._shaderPath.empty() == false )
				passNode.appendChild( "_shaderPath", pass._shaderPath );
			if ( pass._vertexEntryPoint.empty() == false )
				passNode.appendChild( "_vertexEntryPoint", pass._vertexEntryPoint );
			if ( pass._pixelEntryPoint.empty() == false )
				passNode.appendChild( "_pixelEntryPoint", pass._pixelEntryPoint );
			if ( pass._computeEntryPoint.empty() == false && pass._computeEntryPoint != "CSMain" )
				passNode.appendChild( "_computeEntryPoint", pass._computeEntryPoint );
			if ( pass._cullMode.empty() == false )
				passNode.appendChild( "_cullMode", pass._cullMode );

			passNode.appendChild( "_bEnableDepthTest", pass._bEnableDepthTest );
			passNode.appendChild( "_bEnableDepthWrite", pass._bEnableDepthWrite );
			passNode.appendChild( "_bEnableBlend", pass._bEnableBlend );

			if ( pass._listPermutation.empty() == false )
				appendStringList( passNode, "_permutations", pass._listPermutation );
		}

		if ( _desc._listRenderPassRef.empty() == false )
			appendStringList( root, "_renderPassRefs", _desc._listRenderPassRef );

		if ( doc.saveFile( absPath ) == false )
		{
			SW_LOG_ERROR( "Failed to write: %#", absPath );
			return false;
		}

		SW_LOG_INFO( "Saved '%#' -> %#", _desc._name, absPath );
		return true;
	}

	TaskHandle RenderPipelineResource::loadFromXmlFileAsync( string_view assetRelativePath )
	{
		TaskHandle handle = engine::getTaskManager().emplaceTask(
			"LoadRenderPipelineAsync",
			SW_DELEGATE_FUNCTION( TaskArgsDelegate, RenderPipelineResource::loadFromXmlFileAsyncJob ),
			MakeTaskArgs( this, string( assetRelativePath ) ) );
		handle.submit();
		return handle;
	}

	void RenderPipelineResource::loadFromXmlFileAsyncJob( const TaskArgs& args )
	{
		RenderPipelineResource* pResource = args.get<RenderPipelineResource*>( 0 );
		if ( pResource == nullptr )
			return;
		pResource->loadFromXmlFile( args.get<string>( 1 ) );
	}

} // namespace sw
