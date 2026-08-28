#include "pch.h"

#include "Editor/Common/Config/EditorData.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
	SW_LOG_CALLER( "EditorData" );

	namespace
	{

		void takeFontList( XmlNode root, const utf8* pListName, vector<string>& listOut )
		{
			XmlNode list = root.child( pListName );
			if ( list.isValid() == false )
				return;

			vector<string> listLoaded;
			for ( XmlNode fontNode = list.child( "font" ); fontNode; fontNode = fontNode.next( "font" ) )
			{
				const utf8* pText = fontNode.text();
				if ( pText != nullptr && pText[0] != '\0' )
					listLoaded.push_back( pText );
			}
			if ( listLoaded.empty() == false )
				listOut = std::move( listLoaded );
		}

		void takeClearColor( XmlNode root, float32 outColor[4] )
		{
			const utf8* pText = root.childText( "clearColor" );
			if ( pText == nullptr || pText[0] == '\0' )
				return;
			float32 r = outColor[0];
			float32 g = outColor[1];
			float32 b = outColor[2];
			float32 a = outColor[3];
			if ( std::sscanf( pText, "%f %f %f %f", &r, &g, &b, &a ) == 4 )
			{
				outColor[0] = r;
				outColor[1] = g;
				outColor[2] = b;
				outColor[3] = a;
			}
		}

	} // namespace

	bool EditorData::loadFromHostPath( string_view hostRelativePath )
	{
		string rel = hostRelativePath.empty() == false ? string( hostRelativePath ) : EditorConfig::getActive()._editorData;
		if ( rel.empty() )
			rel = string( config::kFileRuntimeEditorData );

		const string projectRoot = EditorUtil::getProjectRootPath();
		string		 absPath	 = FileUtil::normalizeSeparators( rel );
		const bool	 bAbsolute =
			( absPath.size() >= 2 && absPath[1] == ':' ) || ( absPath.empty() == false && ( absPath[0] == '/' || absPath[0] == '\\' ) );
		if ( projectRoot.empty() == false && bAbsolute == false )
			absPath = FileUtil::joinPath( projectRoot, absPath );

		XmlDocument doc;
		if ( doc.loadFile( absPath ) == false )
		{
			SW_LOG_WARNING( "Using built-in defaults; failed to read %#", absPath );
			return false;
		}

		XmlNode root = doc.root( "EditorData" );
		if ( root.isValid() == false )
		{
			SW_LOG_WARNING( "Missing <EditorData> in %# — using defaults.", absPath );
			return false;
		}

		root.takeChildText( "defaultMap", _defaultMap );
		root.takeChildText( "warpMap", _warpMap );
		root.takeChildText( "spriteAtlas", _spriteAtlas );
		root.takeChildText( "defaultMaterial", _defaultMaterial );
		root.takeChildText( "editorFolder", _editorFolder );
		root.takeChildText( "fontsFolder", _fontsFolder );

		const utf8* pFontSizeText = root.childText( "fontSize" );
		if ( pFontSizeText != nullptr )
			_fontSize = static_cast<float32>( StringUtil::atof( pFontSizeText ) );
		const utf8* pPlayerSpeedText = root.childText( "playerSpeed" );
		if ( pPlayerSpeedText != nullptr )
			_playerSpeed = static_cast<float32>( StringUtil::atof( pPlayerSpeedText ) );
		takeClearColor( root, _arrClearColor );
		takeFontList( root, "baseFonts", _listBaseFonts );
		takeFontList( root, "koreanFonts", _listKoreanFonts );

		SW_LOG_INFO( "Loaded from %#", absPath );
		return true;
	}
} // namespace sw::editor
