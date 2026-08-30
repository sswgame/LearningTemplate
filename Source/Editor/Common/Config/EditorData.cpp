#include "pch.h"

#include "Editor/Common/Config/EditorData.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
	namespace
	{
		struct EditorDataInternal
		{
			static void takeFontList( XmlNode root, const utf8* pListName, vector<string>& outList )
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
					outList = std::move( listLoaded );
			}

			static void takeClearColor( XmlNode root, float4& outColor )
			{
				const utf8* pText = root.childText( "clearColor" );
				if ( pText == nullptr || pText[0] == '\0' )
					return;
				string_splitter tokens( pText, { ",", " " } );
				const auto&		listToken = tokens.getSplitList();
				if ( listToken.size() < 4 )
					return;
				float32 arrVal[4] = { outColor._x, outColor._y, outColor._z, outColor._w };
				for ( size_t index = 0; index < 4; ++index )
					StringUtil::parseFloat( listToken[index], arrVal[index] );
				outColor = float4( arrVal[0], arrVal[1], arrVal[2], arrVal[3] );
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "EditorData" );

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
		EditorDataInternal::takeClearColor( root, _clearColor );
		EditorDataInternal::takeFontList( root, "baseFonts", _listBaseFont );
		EditorDataInternal::takeFontList( root, "koreanFonts", _listKoreanFont );

		SW_LOG_INFO( "Loaded from %#", absPath );
		return true;
	}
} // namespace sw::editor
