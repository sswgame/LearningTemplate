#include "pch.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	bool EngineData::loadFromResource( string_view assetRelativePath )
	{
		const string path = assetRelativePath.empty() ? string( path::kEngineData ) : string( assetRelativePath );

		XmlDocument doc;
		string		absPath;
		if ( doc.loadResource( path, &absPath ) == false )
		{
			SW_LOG_WARNING( "[EngineData] Using built-in defaults; failed to read %#", path );
			return false;
		}

		XmlNode root = doc.root( "EngineData" );
		if ( root.isValid() == false )
		{
			SW_LOG_WARNING( "[EngineData] Missing <EngineData> in %# — using defaults.", absPath );
			return false;
		}

		root.takeChildText( "defaultMaterial", _defaultMaterial );
		root.takeChildText( "shellInputMap", _shellInputMap );
		root.takeChildText( "defaultForwardPipeline", _defaultForwardPipeline );
		root.takeChildText( "defaultDeferredPipeline", _defaultDeferredPipeline );
		root.takeChildText( "defaultRenderPass", _defaultRenderPass );
		root.takeChildText( "shaderShadowDepth", _shaderShadowDepth );
		root.takeChildText( "shaderForwardLit", _shaderForwardLit );
		root.takeChildText( "shaderGBuffer", _shaderGBuffer );
		root.takeChildText( "shaderGBufferAlbedo", _shaderGBufferAlbedo );
		root.takeChildText( "shaderGBufferNormal", _shaderGBufferNormal );
		root.takeChildText( "shaderDeferredLighting", _shaderDeferredLighting );
		root.takeChildText( "shaderPostBloom", _shaderPostBloom );
		root.takeChildText( "shaderPostOutlineCommon", _shaderPostOutlineCommon );
		root.takeChildText( "shaderPostOutlineEngine", _shaderPostOutlineEngine );
		root.takeChildText( "shaderFullscreenBlit", _shaderFullscreenBlit );
		root.takeChildText( "shaderGpuCull", _shaderGpuCull );
		root.takeChildText( "shaderFullscreenTriangle", _shaderFullscreenTriangle );
		root.takeChildText( "shaderSsao", _shaderSsao );
		root.takeChildText( "shaderTaa", _shaderTaa );
		root.takeChildText( "shaderTonemap", _shaderTonemap );

		SW_LOG_INFO( "[EngineData] Loaded from %#", absPath );
		return true;
	}
} // namespace sw
