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
		void parsePipelineGraphPasses( XmlNode passesNode, vector<RenderGraphPassDesc>& outListPasses )
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

				parseStringList( passNode, "_inputs", pass._listInputs );
				parseStringList( passNode, "_outputs", pass._listOutputs );
				parseStringList( passNode, "_permutations", pass._listPermutations );

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

				const utf8* pEnableDepthTest = passNode.childText( "_bEnableDepthTest" );
				if ( pEnableDepthTest != nullptr )
					pass._bEnableDepthTest = ( string( pEnableDepthTest ) == "1" || string( pEnableDepthTest ) == "true" );

				const utf8* pEnableDepthWrite = passNode.childText( "_bEnableDepthWrite" );
				if ( pEnableDepthWrite != nullptr )
					pass._bEnableDepthWrite = ( string( pEnableDepthWrite ) == "1" || string( pEnableDepthWrite ) == "true" );

				const utf8* pEnableBlend = passNode.childText( "_bEnableBlend" );
				if ( pEnableBlend != nullptr )
					pass._bEnableBlend = ( string( pEnableBlend ) == "1" || string( pEnableBlend ) == "true" );

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
				parseStringList( passNode, "inputs", pass._listInputs );
				parseStringList( passNode, "outputs", pass._listOutputs );
				if ( pass._listInputs.empty() )
					parseStringList( passNode, "_inputs", pass._listInputs );
				if ( pass._listOutputs.empty() )
					parseStringList( passNode, "_outputs", pass._listOutputs );
				parseStringList( passNode, "_permutations", pass._listPermutations );
				outListPasses.push_back( std::move( pass ) );
			}
		}

		void parseAttachments( XmlNode attachsNode, vector<RenderPassAttachment>& outListAttachments )
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
				const utf8* pClear = attNode.childText( "_bClear" );
				if ( pClear != nullptr )
					att._bClear = ( string( pClear ) == "1" || string( pClear ) == "true" );
				const utf8* pClearColor = attNode.childText( "_clearColor" );
				if ( pClearColor != nullptr )
				{
					if ( std::sscanf( pClearColor, "%f,%f,%f,%f", &att._arrClearColor[0], &att._arrClearColor[1], &att._arrClearColor[2], &att._arrClearColor[3] ) < 4 )
						std::sscanf( pClearColor, "%f %f %f %f", &att._arrClearColor[0], &att._arrClearColor[1], &att._arrClearColor[2], &att._arrClearColor[3] );
				}
				outListAttachments.push_back( std::move( att ) );
			}
		}

		string guessShadingModel( string_view name, const vector<RenderGraphPassDesc>& listPasses )
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

	} // namespace

	bool RenderPipelineResource::loadFromXmlFile( string_view assetRelativePath )
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::fileExists( absPath ) == false )
		{
			SW_LOG_ERROR( "[RenderPipelineResource] XML file not found: %#", absPath );
			return false;
		}

		vector<uint8> listFileData;
		if ( FileUtil::readFile( absPath, listFileData ) == false || listFileData.empty() )
		{
			SW_LOG_ERROR( "[RenderPipelineResource] Failed to read XML: %#", absPath );
			return false;
		}

		string		xmlStr( reinterpret_cast<const utf8*>( listFileData.data() ), listFileData.size() );
		XmlDocument doc;
		if ( doc.parse( xmlStr ) == false )
		{
			SW_LOG_ERROR( "[RenderPipelineResource] Failed to parse XML: %#", absPath );
			return false;
		}

		XmlNode root = doc.root( "RenderPipelineDesc" );
		if ( root.isValid() == false )
		{
			SW_LOG_ERROR( "[RenderPipelineResource] Missing <RenderPipelineDesc>: %#", absPath );
			return false;
		}

		if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::RenderPipeline, doc, root,
																			   AssetFormatVersions::kRenderPipeline ) == false )
		{
			SW_LOG_ERROR( "[RenderPipelineResource] formatVersion upgrade failed: %#", absPath );
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

		parseAttachments( root.child( "_attachments" ), _desc._listAttachments );

		XmlNode passesNode = root.child( "_passes" );
		if ( passesNode.isValid() )
			parsePipelineGraphPasses( passesNode, _desc._listPasses );

		parseStringList( root, "_renderPassRefs", _desc._listRenderPassRefs );

		if ( _desc._shadingModel.empty() || _desc._shadingModel == "Forward" )
			_desc._shadingModel = guessShadingModel( _desc._name, _desc._listPasses );

		SW_LOG_INFO( "[RenderPipelineResource] Loaded '%#' (model=%#, attachments=%#, passes=%#)",
					 _desc._name, _desc._shadingModel, _desc._listAttachments.size(), _desc._listPasses.size() );
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

		XmlNode nameNode = root.appendChild( "_name" );
		nameNode.setValue( _desc._name.c_str() );
		XmlNode shadingModelNode = root.appendChild( "_shadingModel" );
		shadingModelNode.setValue( _desc._shadingModel.c_str() );

		XmlNode attachsNode = root.appendChild( "_attachments" );
		for ( const RenderPassAttachment& att : _desc._listAttachments )
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

		XmlNode passesNode = root.appendChild( "_passes" );
		for ( const RenderGraphPassDesc& pass : _desc._listPasses )
		{
			XmlNode passNode = passesNode.appendChild( "item" );
			XmlNode pName	 = passNode.appendChild( "_name" );
			pName.setValue( pass._name.c_str() );

			XmlNode pType = passNode.appendChild( "_type" );
			pType.setValue( pass._type.c_str() );

			appendStringList( passNode, "_inputs", pass._listInputs );
			appendStringList( passNode, "_outputs", pass._listOutputs );
			if ( pass._shaderPath.empty() == false )
			{
				XmlNode pNode = passNode.appendChild( "_shaderPath" );
				pNode.setValue( pass._shaderPath.c_str() );
			}
			if ( pass._vertexEntryPoint.empty() == false )
			{
				XmlNode pNode = passNode.appendChild( "_vertexEntryPoint" );
				pNode.setValue( pass._vertexEntryPoint.c_str() );
			}
			if ( pass._pixelEntryPoint.empty() == false )
			{
				XmlNode pNode = passNode.appendChild( "_pixelEntryPoint" );
				pNode.setValue( pass._pixelEntryPoint.c_str() );
			}
			if ( pass._computeEntryPoint.empty() == false && pass._computeEntryPoint != "CSMain" )
			{
				XmlNode pNode = passNode.appendChild( "_computeEntryPoint" );
				pNode.setValue( pass._computeEntryPoint.c_str() );
			}
			if ( pass._cullMode.empty() == false )
			{
				XmlNode pNode = passNode.appendChild( "_cullMode" );
				pNode.setValue( pass._cullMode.c_str() );
			}

			XmlNode pDepthTest = passNode.appendChild( "_bEnableDepthTest" );
			pDepthTest.setValue( pass._bEnableDepthTest ? "1" : "0" );

			XmlNode pDepthWrite = passNode.appendChild( "_bEnableDepthWrite" );
			pDepthWrite.setValue( pass._bEnableDepthWrite ? "1" : "0" );

			XmlNode pBlend = passNode.appendChild( "_bEnableBlend" );
			pBlend.setValue( pass._bEnableBlend ? "1" : "0" );

			if ( pass._listPermutations.empty() == false )
				appendStringList( passNode, "_permutations", pass._listPermutations );
		}

		if ( _desc._listRenderPassRefs.empty() == false )
			appendStringList( root, "_renderPassRefs", _desc._listRenderPassRefs );

		string xmlStr = doc.saveToString();
		if ( FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( xmlStr.data() ), xmlStr.size() ) == false )
		{
			SW_LOG_ERROR( "[RenderPipelineResource] Failed to write: %#", absPath );
			return false;
		}

		SW_LOG_INFO( "[RenderPipelineResource] Saved '%#' -> %#", _desc._name, absPath );
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
