#include "pch.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Serialization/Format/SimpleJsonWalk.h"

namespace sw
{
	namespace
	{
		const utf8* dialogueAssetTypeName( DialogueAssetNodeType type )
		{
			switch ( type )
			{
				case DialogueAssetNodeType::Start:
					return "Start";
				case DialogueAssetNodeType::Dialogue:
					return "Dialogue";
				case DialogueAssetNodeType::Choice:
					return "Choice";
				case DialogueAssetNodeType::Branch:
					return "Branch";
				case DialogueAssetNodeType::Action:
					return "Action";
				case DialogueAssetNodeType::End:
					return "End";
				default:
					return "Unknown";
			}
		}

		DialogueAssetNodeType parseDialogueAssetType( string_view typeStr )
		{
			if ( typeStr == "Start" )
				return DialogueAssetNodeType::Start;
			if ( typeStr == "Choice" )
				return DialogueAssetNodeType::Choice;
			if ( typeStr == "Branch" )
				return DialogueAssetNodeType::Branch;
			if ( typeStr == "Action" )
				return DialogueAssetNodeType::Action;
			if ( typeStr == "End" )
				return DialogueAssetNodeType::End;
			return DialogueAssetNodeType::Dialogue;
		}
	} // namespace

	bool DialogueGraphAsset::loadFromFile( string_view path )
	{
		_listNode.clear();
		_listLink.clear();
		if ( path.empty() || FileUtil::fileExists( path ) == false )
			return false;

		string json;
		if ( FileUtil::readTextFile( path, json ) == false || json.empty() )
			return false;
		return parseJson( json );
	}

	bool DialogueGraphAsset::saveToFile( string_view path ) const
	{
		if ( path.empty() )
			return false;
		const string dir = FileUtil::getDirectoryPart( path );
		if ( dir.empty() == false )
			FileUtil::ensureDirectoryExists( dir );
		const string json = toJson();
		return FileUtil::writeTextFile( path, json );
	}

	bool DialogueGraphAsset::parseJson( string_view jsonView )
	{
		_listNode.clear();
		_listLink.clear();
		const string json{ jsonView };

		vector<size_t> listNodeObj;
		collectJsonObjectArrayStarts( json, "nodes", listNodeObj );
		const size_t nodesEnd = findJsonArrayEnd( json, "nodes" );
		for ( const size_t obj : listNodeObj )
		{
			DialogueAssetNode node{};
			parseJsonIntField( json, obj, nodesEnd, "id", node._id );
			string typeName;
			if ( parseJsonStringField( json, obj, nodesEnd, "type", typeName ) )
				node._type = parseDialogueAssetType( typeName );
			parseJsonStringField( json, obj, nodesEnd, "speaker", node._speaker );
			parseJsonStringField( json, obj, nodesEnd, "text", node._text );
			parseJsonStringField( json, obj, nodesEnd, "condition", node._condition );
			parseJsonStringField( json, obj, nodesEnd, "action", node._actionCommand );
			parseJsonFloatField( json, obj, nodesEnd, "x", node._x );
			parseJsonFloatField( json, obj, nodesEnd, "y", node._y );
			if ( node._id > 0 )
				_listNode.push_back( std::move( node ) );
		}

		vector<size_t> listLinkObj;
		collectJsonObjectArrayStarts( json, "links", listLinkObj );
		const size_t linksEnd = findJsonArrayEnd( json, "links" );
		for ( const size_t obj : listLinkObj )
		{
			DialogueAssetLink link{};
			parseJsonIntField( json, obj, linksEnd, "id", link._id );
			parseJsonIntField( json, obj, linksEnd, "from", link._fromPin );
			parseJsonIntField( json, obj, linksEnd, "to", link._toPin );
			if ( link._id > 0 )
				_listLink.push_back( link );
		}
		return true;
	}

	string DialogueGraphAsset::toJson() const
	{
		StringBuilder<constant::kMaxBuffer4096> sb;
		sb.append( "{\n  \"nodes\": [\n" );
		for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
		{
			const DialogueAssetNode& node = _listNode[nodeIndex];
			sb.append( "    { \"id\": " ).append( node._id );
			sb.append( ", \"type\": \"" ).append( dialogueAssetTypeName( node._type ) ).append( "\"" );
			sb.append( ", \"speaker\": \"" ).append( node._speaker.c_str() ).append( "\"" );
			sb.append( ", \"text\": \"" ).append( node._text.c_str() ).append( "\"" );
			sb.append( ", \"condition\": \"" ).append( node._condition.c_str() ).append( "\"" );
			sb.append( ", \"action\": \"" ).append( node._actionCommand.c_str() ).append( "\"" );
			sb.append( ", \"x\": " ).append( node._x );
			sb.append( ", \"y\": " ).append( node._y );
			sb.append( " }" );
			if ( nodeIndex + 1 < _listNode.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ],\n  \"links\": [\n" );
		for ( size_t linkIndex = 0; linkIndex < _listLink.size(); ++linkIndex )
		{
			const DialogueAssetLink& link = _listLink[linkIndex];
			sb.append( "    { \"id\": " ).append( link._id );
			sb.append( ", \"from\": " ).append( link._fromPin );
			sb.append( ", \"to\": " ).append( link._toPin );
			sb.append( " }" );
			if ( linkIndex + 1 < _listLink.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n}\n" );
		return string{ sb.c_str() };
	}

	const DialogueAssetNode* DialogueGraphAsset::findStartNode() const
	{
		for ( const DialogueAssetNode& node : _listNode )
		{
			if ( node._type == DialogueAssetNodeType::Start )
				return &node;
		}
		if ( _listNode.empty() )
			return nullptr;
		return &_listNode.front();
	}

	const DialogueAssetNode* DialogueGraphAsset::findNode( int32 nodeId ) const
	{
		for ( const DialogueAssetNode& node : _listNode )
		{
			if ( node._id == nodeId )
				return &node;
		}
		return nullptr;
	}

	string DialogueGraphAsset::resolveLocalizedText( string_view textOrKey )
	{
		if ( textOrKey.empty() )
			return {};
		if ( engine::areEngineServicesBound() == false )
			return string{ textOrKey };

		const string keyStr{ textOrKey };
		const utf8*	 pResolved = engine::getLocalizationManager().getString( hashed_string( keyStr.c_str() ), nullptr );
		if ( pResolved == nullptr || pResolved[0] == '\0' )
			return string{ textOrKey };
		return string{ pResolved };
	}
} // namespace sw
