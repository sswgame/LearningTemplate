#include "pch.h"

#include "Editor/Common/Config/EditorData.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

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
			const utf8* pCursor = pText;
			float32		arrParsed[4]{};
			uint32		parsedCount{ 0 };
			while ( parsedCount < 4 )
			{
				utf8*		pEnd	   = nullptr;
				const utf8* pBefore	   = pCursor;
				arrParsed[parsedCount] = StringUtil::strtof( pCursor, &pEnd );
				if ( pEnd == nullptr || pEnd == pBefore )
					break;
				++parsedCount;
				pCursor = pEnd;
			}
			if ( parsedCount != 4 )
				return;
			outColor[0] = arrParsed[0];
			outColor[1] = arrParsed[1];
			outColor[2] = arrParsed[2];
			outColor[3] = arrParsed[3];
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

		_fontSize	 = root.childFloat( "fontSize", _fontSize );
		_playerSpeed = root.childFloat( "playerSpeed", _playerSpeed );
		takeClearColor( root, _arrClearColor );
		takeFontList( root, "baseFonts", _listBaseFonts );
		takeFontList( root, "koreanFonts", _listKoreanFonts );

		SW_LOG_INFO( "Loaded from %#", absPath );
		return true;
	}
} // namespace sw::editor
