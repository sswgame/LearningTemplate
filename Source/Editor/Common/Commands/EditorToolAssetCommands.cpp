#include "pch.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorUtil.h"

#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Utility/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw::editor
{
	SW_LOG_CALLER( "EditorToolAssetCommands" );

	namespace
	{
		constexpr utf8 kDialogueGraphPath[] = "Saved/Dialogue/default_dialogue.json";

		void resizeTileMapData( EditorTileMapData& data, int32 width, int32 height )
		{
			if ( width <= 0 || height <= 0 )
				return;
			data._width		   = width;
			data._height	   = height;
			const size_t count = static_cast<size_t>( width * height );
			data._listWalkable.assign( count, 1 );
			data._listEncounter.assign( count, 0 );
			data._listPassThrough.assign( count, 0 );
			data._listVisual.assign( count, EditorTileVisual{} );
			data._listWarp.clear();
		}

		bool parseJsonIntInRange( const string& json, size_t from, size_t end, const utf8* pKey, int32& outValue )
		{
			if ( pKey == nullptr )
				return false;
			const string key = string( "\"" ) + pKey + "\"";
			const size_t pos = json.find( key, from );
			if ( pos == string::npos || pos > end )
				return false;
			const size_t colon = json.find( ':', pos );
			if ( colon == string::npos || colon > end )
				return false;
			utf8* pEndPtr{ nullptr };
			outValue = static_cast<int32>( StringUtil::strtoll( json.c_str() + colon + 1, &pEndPtr, 10 ) );
			return true;
		}

		bool parseJsonFloatInRange( const string& json, size_t from, size_t end, const utf8* pKey, float32& outValue )
		{
			if ( pKey == nullptr )
				return false;
			const string key = string( "\"" ) + pKey + "\"";
			const size_t pos = json.find( key, from );
			if ( pos == string::npos || pos > end )
				return false;
			const size_t colon = json.find( ':', pos );
			if ( colon == string::npos || colon > end )
				return false;
			outValue = static_cast<float32>( StringUtil::atof( json.c_str() + colon + 1 ) );
			return true;
		}

		bool parseJsonStringInRange( const string& json, size_t from, size_t end, const utf8* pKey, string& outValue )
		{
			if ( pKey == nullptr )
				return false;
			const string key = string( "\"" ) + pKey + "\"";
			const size_t pos = json.find( key, from );
			if ( pos == string::npos || pos > end )
				return false;
			const size_t colon = json.find( ':', pos );
			if ( colon == string::npos || colon > end )
				return false;
			const size_t q0 = json.find( '"', colon + 1 );
			const size_t q1 = json.find( '"', q0 + 1 );
			if ( q0 == string::npos || q1 == string::npos || q0 > end || q1 > end )
				return false;
			outValue.assign( json, q0 + 1, q1 - q0 - 1 );
			return true;
		}

		bool parseFloatAfter( string_view src, size_t from, const utf8* pKey, float32& outValue )
		{
			const size_t klen = StringUtil::strlen( pKey );
			if ( klen == 0 || from >= src.size() )
				return false;

			for ( size_t sliceIndex = from; sliceIndex + klen + 2 <= src.size(); ++sliceIndex )
			{
				if ( src[sliceIndex] == '"' && src[sliceIndex + klen + 1] == '"' &&
					 src.substr( sliceIndex + 1, klen ) == string_view{ pKey, klen } )
				{
					const size_t colon = src.find( ':', sliceIndex + klen + 2 );
					if ( colon == string_view::npos )
						return false;
					outValue = static_cast<float32>( StringUtil::atof( src.data() + colon + 1 ) );
					return true;
				}
			}
			return false;
		}

		bool parseIntAfter( string_view src, size_t from, const utf8* pKey, int32& outValue )
		{
			const size_t klen = StringUtil::strlen( pKey );
			if ( klen == 0 || from >= src.size() )
				return false;

			for ( size_t sliceIndex = from; sliceIndex + klen + 2 <= src.size(); ++sliceIndex )
			{
				if ( src[sliceIndex] == '"' && src[sliceIndex + klen + 1] == '"' &&
					 src.substr( sliceIndex + 1, klen ) == string_view{ pKey, klen } )
				{
					const size_t colon = src.find( ':', sliceIndex + klen + 2 );
					if ( colon == string_view::npos )
						return false;
					outValue = StringUtil::atoi( src.data() + colon + 1 );
					return true;
				}
			}
			return false;
		}

		void parseJsonObjectArray( const string& json, const utf8* pArrayKey, vector<size_t>& outObjStartList )
		{
			outObjStartList.clear();
			const string key	= string( "\"" ) + pArrayKey + "\"";
			const size_t keyPos = json.find( key );
			if ( keyPos == string::npos )
				return;
			const size_t arr = json.find( '[', keyPos );
			const size_t end = json.find( ']', arr );
			if ( arr == string::npos || end == string::npos )
				return;

			size_t cursor = arr;
			while ( true )
			{
				const size_t obj = json.find( '{', cursor );
				if ( obj == string::npos || obj > end )
					break;
				outObjStartList.push_back( obj );
				cursor = json.find( '}', obj );
				if ( cursor == string::npos )
					break;
				++cursor;
			}
		}
	} // namespace

	bool EditorToolAssetCommands::loadAnimationGraph( EditorAnimGraphData& outData )
	{
		outData._listNode.clear();
		outData._listLink.clear();

		const string path = EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._animationGraphDataFile.c_str() );
		if ( path.empty() || FileUtil::fileExists( path ) == false )
			return false;

		vector<uint8> listData;
		if ( FileUtil::readFile( path, listData ) == false || listData.empty() )
			return false;

		const string   json( listData.begin(), listData.end() );
		vector<size_t> listNodeObj;
		parseJsonObjectArray( json, "nodes", listNodeObj );
		const size_t nodesKey = json.find( "\"nodes\"" );
		const size_t nodesEnd = ( nodesKey == string::npos ) ? string::npos : json.find( ']', json.find( '[', nodesKey ) );
		for ( const size_t obj : listNodeObj )
		{
			EditorAnimGraphNode node{};
			parseJsonIntInRange( json, obj, nodesEnd, "id", node._id );
			parseJsonStringInRange( json, obj, nodesEnd, "name", node._name );
			parseJsonFloatInRange( json, obj, nodesEnd, "x", node._x );
			parseJsonFloatInRange( json, obj, nodesEnd, "y", node._y );
			if ( node._id > 0 )
				outData._listNode.push_back( std::move( node ) );
		}

		vector<size_t> listLinkObj;
		parseJsonObjectArray( json, "links", listLinkObj );
		const size_t linksKey = json.find( "\"links\"" );
		const size_t linksEnd = ( linksKey == string::npos ) ? string::npos : json.find( ']', json.find( '[', linksKey ) );
		for ( const size_t obj : listLinkObj )
		{
			EditorAnimGraphLink link{};
			parseJsonIntInRange( json, obj, linksEnd, "id", link._id );
			parseJsonIntInRange( json, obj, linksEnd, "from", link._fromNode );
			parseJsonIntInRange( json, obj, linksEnd, "to", link._toNode );
			if ( link._id > 0 )
				outData._listLink.push_back( link );
		}
		return true;
	}

	bool EditorToolAssetCommands::saveAnimationGraph( const EditorAnimGraphData& data )
	{
		const string path = EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._animationGraphDataFile.c_str() );
		if ( path.empty() )
			return false;

		StringBuilder<2048> sb;
		sb.append( "{\n  \"nodes\": [\n" );
		for ( size_t nodeIndex = 0; nodeIndex < data._listNode.size(); ++nodeIndex )
		{
			const EditorAnimGraphNode& node = data._listNode[nodeIndex];
			sb.append( "    { \"id\": " ).append( node._id ).append( ", \"name\": \"" ).append( JsonSerializer::escapeString( node._name ).c_str() ).append( "\", \"x\": " ).append( node._x ).append( ", \"y\": " ).append( node._y ).append( " }" );
			if ( nodeIndex + 1 < data._listNode.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ],\n  \"links\": [\n" );
		for ( size_t linkIndex = 0; linkIndex < data._listLink.size(); ++linkIndex )
		{
			const EditorAnimGraphLink& link = data._listLink[linkIndex];
			sb.append( "    { \"id\": " ).append( link._id ).append( ", \"from\": " ).append( link._fromNode ).append( ", \"to\": " ).append( link._toNode ).append( " }" );
			if ( linkIndex + 1 < data._listLink.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n}\n" );

		const string text( sb.c_str() );
		if ( FileUtil::writeFile( path, reinterpret_cast<const uint8*>( text.data() ), text.size() ) == false )
			return false;
		SW_LOG_INFO( "Saved %#", path.c_str() );
		return true;
	}

	const utf8* EditorToolAssetCommands::dialogueNodeTypeName( DialogueNodeType type )
	{
		switch ( type )
		{
			case DialogueNodeType::Start:
				return "Start";
			case DialogueNodeType::Dialogue:
				return "Dialogue";
			case DialogueNodeType::Choice:
				return "Choice";
			case DialogueNodeType::Branch:
				return "Branch";
			case DialogueNodeType::Action:
				return "Action";
			case DialogueNodeType::End:
				return "End";
			default:
				return "Unknown";
		}
	}

	DialogueNodeType EditorToolAssetCommands::parseDialogueNodeType( string_view typeStr )
	{
		if ( typeStr == "Start" )
			return DialogueNodeType::Start;
		if ( typeStr == "Choice" )
			return DialogueNodeType::Choice;
		if ( typeStr == "Branch" )
			return DialogueNodeType::Branch;
		if ( typeStr == "Action" )
			return DialogueNodeType::Action;
		if ( typeStr == "End" )
			return DialogueNodeType::End;
		return DialogueNodeType::Dialogue;
	}

	bool EditorToolAssetCommands::loadDialogueGraph( EditorDialogueGraphData& outData )
	{
		outData._listNode.clear();
		outData._listLink.clear();

		if ( FileUtil::fileExists( kDialogueGraphPath ) == false )
			return false;

		vector<uint8> listData;
		if ( FileUtil::readFile( kDialogueGraphPath, listData ) == false || listData.empty() )
			return false;

		const string   json( listData.begin(), listData.end() );
		vector<size_t> listNodeObj;
		parseJsonObjectArray( json, "nodes", listNodeObj );
		const size_t nodesKey = json.find( "\"nodes\"" );
		const size_t nodesEnd = ( nodesKey == string::npos ) ? string::npos : json.find( ']', json.find( '[', nodesKey ) );
		for ( const size_t obj : listNodeObj )
		{
			EditorDialogueNode node{};
			parseJsonIntInRange( json, obj, nodesEnd, "id", node._id );
			string typeName;
			if ( parseJsonStringInRange( json, obj, nodesEnd, "type", typeName ) )
				node._type = parseDialogueNodeType( typeName );
			parseJsonStringInRange( json, obj, nodesEnd, "speaker", node._speaker );
			parseJsonStringInRange( json, obj, nodesEnd, "text", node._text );
			parseJsonStringInRange( json, obj, nodesEnd, "condition", node._condition );
			parseJsonStringInRange( json, obj, nodesEnd, "action", node._actionCommand );
			parseJsonFloatInRange( json, obj, nodesEnd, "x", node._x );
			parseJsonFloatInRange( json, obj, nodesEnd, "y", node._y );
			if ( node._id > 0 )
				outData._listNode.push_back( std::move( node ) );
		}

		vector<size_t> listLinkObj;
		parseJsonObjectArray( json, "links", listLinkObj );
		const size_t linksKey = json.find( "\"links\"" );
		const size_t linksEnd = ( linksKey == string::npos ) ? string::npos : json.find( ']', json.find( '[', linksKey ) );
		for ( const size_t obj : listLinkObj )
		{
			EditorDialogueLink link{};
			parseJsonIntInRange( json, obj, linksEnd, "id", link._id );
			parseJsonIntInRange( json, obj, linksEnd, "from", link._fromPin );
			parseJsonIntInRange( json, obj, linksEnd, "to", link._toPin );
			if ( link._id > 0 )
				outData._listLink.push_back( link );
		}
		return true;
	}

	bool EditorToolAssetCommands::saveDialogueGraph( const EditorDialogueGraphData& data )
	{
		FileUtil::ensureDirectoryExists( kDialogueGraphPath );

		StringBuilder<constant::kMaxBuffer4096> sb;
		sb.append( "{\n  \"nodes\": [\n" );
		for ( size_t nodeIndex = 0; nodeIndex < data._listNode.size(); ++nodeIndex )
		{
			const EditorDialogueNode& node = data._listNode[nodeIndex];
			sb.append( "    { \"id\": " ).append( node._id );
			sb.append( ", \"type\": \"" ).append( dialogueNodeTypeName( node._type ) ).append( "\"" );
			sb.append( ", \"speaker\": \"" ).append( node._speaker.c_str() ).append( "\"" );
			sb.append( ", \"text\": \"" ).append( node._text.c_str() ).append( "\"" );
			sb.append( ", \"condition\": \"" ).append( node._condition.c_str() ).append( "\"" );
			sb.append( ", \"action\": \"" ).append( node._actionCommand.c_str() ).append( "\"" );
			sb.append( ", \"x\": " ).append( node._x );
			sb.append( ", \"y\": " ).append( node._y );
			sb.append( " }" );
			if ( nodeIndex + 1 < data._listNode.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ],\n  \"links\": [\n" );
		for ( size_t linkIndex = 0; linkIndex < data._listLink.size(); ++linkIndex )
		{
			const EditorDialogueLink& link = data._listLink[linkIndex];
			sb.append( "    { \"id\": " ).append( link._id );
			sb.append( ", \"from\": " ).append( link._fromPin );
			sb.append( ", \"to\": " ).append( link._toPin );
			sb.append( " }" );
			if ( linkIndex + 1 < data._listLink.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n}\n" );

		if ( FileUtil::writeTextFile( kDialogueGraphPath, sb.view() ) == false )
			return false;
		SW_LOG_INFO( "Saved %zu nodes, %zu links -> %#", data._listNode.size(), data._listLink.size(), kDialogueGraphPath );
		return true;
	}

	bool EditorToolAssetCommands::loadTileMap( string_view assetRelativePath, EditorTileMapData& outData, string& outStatus )
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::fileExists( absPath ) == false )
		{
			outStatus = "Not found: " + absPath;
			return false;
		}

		vector<uint8> listFileData;
		if ( FileUtil::readFile( absPath, listFileData ) == false || listFileData.empty() )
		{
			outStatus = "Failed to read file";
			return false;
		}

		string		xmlStr( reinterpret_cast<const utf8*>( listFileData.data() ), listFileData.size() );
		XmlDocument doc;
		if ( doc.parse( xmlStr ) == false )
		{
			outStatus = "XML parse error";
			return false;
		}

		XmlNode root = doc.root( "TileMap" );
		if ( root.isValid() == false )
		{
			outStatus = "Missing <TileMap>";
			return false;
		}

		const utf8* pName = root.childText( "name" );
		if ( pName != nullptr )
			outData._name = pName;

		int32		width	   = 8;
		int32		height	   = 8;
		const utf8* pWidthText = root.childText( "width" );
		if ( pWidthText != nullptr )
			width = StringUtil::atoi( pWidthText );
		const utf8* pHeightText = root.childText( "height" );
		if ( pHeightText != nullptr )
			height = StringUtil::atoi( pHeightText );
		if ( width <= 0 )
			width = 8;
		if ( height <= 0 )
			height = 8;
		resizeTileMapData( outData, width, height );

		const size_t count = static_cast<size_t>( outData._width * outData._height );
		XmlNode		 tiles = root.child( "tiles" );
		if ( tiles.isValid() )
		{
			int32 index{ 0 };
			for ( XmlNode tileNode = tiles.child( "t" ); tileNode && index < static_cast<int32>( count );
				  tileNode		   = tileNode.next( "t" ), ++index )
			{
				const utf8*	 pV					 = tileNode.text();
				const size_t tileIndex			 = static_cast<size_t>( index );
				outData._listWalkable[tileIndex] = ( pV == nullptr || pV[0] != '0' ) ? 1 : 0;
				const utf8* pEnc				 = tileNode.attr( "enc" );
				if ( pEnc != nullptr )
					outData._listEncounter[tileIndex] = ( StringUtil::atoi( pEnc ) != 0 ) ? 1 : 0;
				const utf8* pPt = tileNode.attr( "pt" );
				if ( pPt != nullptr )
					outData._listPassThrough[tileIndex] = ( StringUtil::atoi( pPt ) != 0 ) ? 1 : 0;

				EditorTileVisual vis{};
				const utf8*		 pHa = tileNode.attr( "h" );
				if ( pHa != nullptr )
					vis._height = static_cast<uint8>( StringUtil::atoi( pHa ) );
				const utf8* pAtlas = tileNode.attr( "atlas" );
				if ( pAtlas != nullptr )
					vis._atlasId = static_cast<uint8>( StringUtil::atoi( pAtlas ) );
				const utf8* pTr = tileNode.attr( "tr" );
				if ( pTr != nullptr )
					vis._tintR = static_cast<uint8>( StringUtil::atoi( pTr ) );
				const utf8* pTg = tileNode.attr( "tg" );
				if ( pTg != nullptr )
					vis._tintG = static_cast<uint8>( StringUtil::atoi( pTg ) );
				const utf8* pTb = tileNode.attr( "tb" );
				if ( pTb != nullptr )
					vis._tintB = static_cast<uint8>( StringUtil::atoi( pTb ) );
				outData._listVisual[tileIndex] = vis;
			}
		}

		outData._listWarp.clear();
		XmlNode warps = root.child( "warps" );
		if ( warps.isValid() )
		{
			for ( XmlNode warpNode = warps.child( "warp" ); warpNode; warpNode = warpNode.next( "warp" ) )
			{
				EditorTileWarp warp{};
				const utf8*	   pX = warpNode.attr( "x" );
				if ( pX != nullptr )
					warp._tileX = StringUtil::atoi( pX );
				const utf8* pY = warpNode.attr( "y" );
				if ( pY != nullptr )
					warp._tileY = StringUtil::atoi( pY );
				const utf8* pMap = warpNode.attr( "map" );
				if ( pMap != nullptr )
					warp._targetMap = pMap;
				const utf8* pTx = warpNode.attr( "tx" );
				if ( pTx != nullptr )
					warp._targetTileX = StringUtil::atoi( pTx );
				const utf8* pTy = warpNode.attr( "ty" );
				if ( pTy != nullptr )
					warp._targetTileY = StringUtil::atoi( pTy );
				const utf8* pPair = warpNode.attr( "pair" );
				if ( pPair != nullptr )
					warp._pairId = pPair;
				outData._listWarp.push_back( std::move( warp ) );
			}
		}

		outStatus = string( "Loaded " ) + string( assetRelativePath );
		return true;
	}

	bool EditorToolAssetCommands::saveTileMap( string_view assetRelativePath, const EditorTileMapData& data )
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		StringBuilder<4096> sb;
		sb.appendFormat( "<TileMap>\n" );
		sb.appendFormat( "  <name>%s</name>\n", data._name.c_str() );
		sb.appendFormat( "  <width>%d</width>\n", data._width );
		sb.appendFormat( "  <height>%d</height>\n", data._height );
		sb.appendFormat( "  <tiles>\n" );

		const size_t count = static_cast<size_t>( data._width * data._height );
		for ( size_t tileIndex = 0; tileIndex < count; ++tileIndex )
		{
			const uint8			   walk = data._listWalkable[tileIndex];
			const uint8			   enc	= data._listEncounter[tileIndex];
			const uint8			   pt	= data._listPassThrough[tileIndex];
			const EditorTileVisual vis	= data._listVisual[tileIndex];
			sb.appendFormat( "    <t enc=\"%u\" pt=\"%u\" h=\"%u\" atlas=\"%u\" tr=\"%u\" tg=\"%u\" tb=\"%u\">%u</t>\n",
							 static_cast<uint32>( enc ),
							 static_cast<uint32>( pt ),
							 static_cast<uint32>( vis._height ),
							 static_cast<uint32>( vis._atlasId ),
							 static_cast<uint32>( vis._tintR ),
							 static_cast<uint32>( vis._tintG ),
							 static_cast<uint32>( vis._tintB ),
							 static_cast<uint32>( walk ) );
		}

		sb.appendFormat( "  </tiles>\n" );
		sb.appendFormat( "  <warps>\n" );
		for ( const EditorTileWarp& warp : data._listWarp )
		{
			sb.appendFormat( "    <warp x=\"%d\" y=\"%d\" map=\"%s\" tx=\"%d\" ty=\"%d\"",
							 warp._tileX,
							 warp._tileY,
							 warp._targetMap.c_str(),
							 warp._targetTileX,
							 warp._targetTileY );
			if ( warp._pairId.empty() == false )
				sb.appendFormat( " pair=\"%s\"", warp._pairId.c_str() );
			sb.appendFormat( "/>\n" );
		}
		sb.appendFormat( "  </warps>\n" );
		sb.appendFormat( "</TileMap>\n" );
		return FileUtil::writeTextFile( absPath, sb.c_str() );
	}

	bool EditorToolAssetCommands::loadSpriteClip( EditorSpriteClipData& outData, string& outStatus )
	{
		outData._listFrame.clear();
		outData._listKey.clear();
		outData._atlasPath.clear();

		const string path = EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._spriteClipFile.c_str() );
		if ( path.empty() || FileUtil::fileExists( path ) == false )
		{
			outStatus = "No SpriteClip.json yet";
			return false;
		}

		string json;
		if ( FileUtil::readTextFile( path, json ) == false || json.empty() )
		{
			outStatus = "Failed to read SpriteClip.json";
			return false;
		}

		const string atlas = JsonSerializer::extractStringField( json, "atlas" );
		if ( atlas.empty() == false )
			outData._atlasPath = atlas;

		vector<size_t> listFrameObj;
		parseJsonObjectArray( json, "frames", listFrameObj );
		for ( const size_t obj : listFrameObj )
		{
			EditorSpriteClipFrame frame{};
			parseFloatAfter( json, obj, "u", frame._u );
			parseFloatAfter( json, obj, "v", frame._v );
			parseFloatAfter( json, obj, "w", frame._w );
			parseFloatAfter( json, obj, "h", frame._h );
			parseIntAfter( json, obj, "durationMs", frame._durationMs );
			outData._listFrame.push_back( frame );
		}

		vector<size_t> listKeyObj;
		parseJsonObjectArray( json, "transformKeys", listKeyObj );
		for ( const size_t obj : listKeyObj )
		{
			EditorSpriteClipKey key{};
			parseFloatAfter( json, obj, "time", key._time );
			parseFloatAfter( json, obj, "x", key._x );
			parseFloatAfter( json, obj, "y", key._y );
			parseFloatAfter( json, obj, "angleDeg", key._angleDeg );
			outData._listKey.push_back( key );
		}

		outStatus = "Loaded SpriteClip.json";
		return true;
	}

	bool EditorToolAssetCommands::saveSpriteClip( const EditorSpriteClipData& data )
	{
		const string path = EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._spriteClipFile.c_str() );
		if ( path.empty() )
			return false;

		StringBuilder<2048> sb;
		sb.append( "{\n" );
		sb.append( "  \"atlas\": \"" ).append( JsonSerializer::escapeString( data._atlasPath ).c_str() ).append( "\",\n" );
		sb.append( "  \"frames\": [\n" );
		for ( size_t frameIndex = 0; frameIndex < data._listFrame.size(); ++frameIndex )
		{
			const EditorSpriteClipFrame& frame = data._listFrame[frameIndex];
			sb.append( "    { \"u\": " ).append( frame._u ).append( ", \"v\": " ).append( frame._v ).append( ", \"w\": " ).append( frame._w ).append( ", \"h\": " ).append( frame._h ).append( ", \"durationMs\": " ).append( frame._durationMs ).append( " }" );
			if ( frameIndex + 1 < data._listFrame.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ],\n" );
		sb.append( "  \"transformKeys\": [\n" );
		for ( size_t keyIndex = 0; keyIndex < data._listKey.size(); ++keyIndex )
		{
			const EditorSpriteClipKey& key = data._listKey[keyIndex];
			sb.append( "    { \"time\": " ).append( key._time ).append( ", \"x\": " ).append( key._x ).append( ", \"y\": " ).append( key._y ).append( ", \"angleDeg\": " ).append( key._angleDeg ).append( " }" );
			if ( keyIndex + 1 < data._listKey.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n" );
		sb.append( "}\n" );

		const string text( sb.c_str() );
		if ( FileUtil::writeFile( path, reinterpret_cast<const uint8*>( text.data() ), text.size() ) == false )
			return false;
		SW_LOG_INFO( "Saved %#", path.c_str() );
		return true;
	}

	void EditorToolAssetCommands::collectPrefabOverrides( const utf8* pPrefabPath, string& outPrefabPath,
														  vector<PrefabOverrideItem>& outOverride, vector<string>& outNestedPrefab )
	{
		outOverride.clear();
		outNestedPrefab.clear();
		outPrefabPath = pPrefabPath != nullptr ? pPrefabPath : "Prefabs/Characters/Orc_Warrior.prefab";

		outNestedPrefab.push_back( "Prefabs/Weapons/BattleAxe_Heavy.prefab" );
		outNestedPrefab.push_back( "Prefabs/VFX/RageAura_Fire.prefab" );
		outNestedPrefab.push_back( "Prefabs/UI/WorldHealthBar.prefab" );

		outOverride.push_back( PrefabOverrideItem{ "UnitStatsComponent", "maxHp", "250", "350", true } );
		outOverride.push_back( PrefabOverrideItem{ "UnitStatsComponent", "attack", "35", "50", true } );
		outOverride.push_back( PrefabOverrideItem{ "UnitStatsComponent", "defense", "15", "15", false } );
		outOverride.push_back( PrefabOverrideItem{ "UnitStatsComponent", "moveSpeed", "4.5", "4.5", false } );
		outOverride.push_back( PrefabOverrideItem{ "TransformComponent", "scale", "(1.0, 1.0, 1.0)", "(1.25, 1.25, 1.25)", true } );
		outOverride.push_back( PrefabOverrideItem{ "MaterialComponent", "tintColor", "(1.0, 1.0, 1.0, 1.0)", "(1.0, 0.8, 0.8, 1.0)", true } );
	}

	void EditorToolAssetCommands::revertPrefabOverride( PrefabOverrideItem& item )
	{
		item._overriddenValue = item._defaultValue;
		item._bModified		  = false;
	}

	void EditorToolAssetCommands::applyPrefabOverridesToTemplate( vector<PrefabOverrideItem>& listOverride )
	{
		for ( PrefabOverrideItem& item : listOverride )
		{
			if ( item._bModified )
			{
				item._defaultValue = item._overriddenValue;
				item._bModified	   = false;
			}
		}
	}

	void EditorToolAssetCommands::revertAllPrefabOverrides( vector<PrefabOverrideItem>& listOverride )
	{
		for ( PrefabOverrideItem& item : listOverride )
			revertPrefabOverride( item );
	}
} // namespace sw::editor
