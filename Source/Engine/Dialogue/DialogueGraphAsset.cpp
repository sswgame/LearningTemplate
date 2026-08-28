#include "pch.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Resource/ResourceUtil.h"

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
		if ( path.empty() )
			return false;

		JsonDocument doc;
		if ( doc.loadPath( path ) == false )
			return false;
		return parseJson( doc.dump( -1 ) );
	}

	bool DialogueGraphAsset::saveToFile( string_view path ) const
	{
		if ( path.empty() )
			return false;
		const string dir = FileUtil::getDirectoryPart( path );
		if ( dir.empty() == false )
			FileUtil::ensureDirectoryExists( dir );
		return FileUtil::writeTextFile( path, toJson() );
	}

	bool DialogueGraphAsset::parseJson( string_view jsonView )
	{
		_listNode.clear();
		_listLink.clear();

		JsonDocument doc;
		if ( doc.parse( jsonView ) == false )
			return false;

		const JsonValue root	 = doc.root();
		const JsonValue nodesVal = root.get( "nodes" );
		if ( nodesVal.isArray() )
		{
			const size_t nodeCount = nodesVal.size();
			for ( size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
			{
				const JsonValue nodeJson = nodesVal.at( nodeIndex );
				if ( nodeJson.isObject() == false )
					continue;

				DialogueAssetNode node{};
				node._id			= static_cast<int32>( nodeJson.get( "id" ).asInt( 0 ) );
				node._type			= parseDialogueAssetType( nodeJson.get( "type" ).asString() );
				node._speaker		= nodeJson.get( "speaker" ).asString();
				node._text			= nodeJson.get( "text" ).asString();
				node._condition		= nodeJson.get( "condition" ).asString();
				node._actionCommand = nodeJson.get( "action" ).asString();
				node._x				= static_cast<float32>( nodeJson.get( "x" ).asFloat( 40.0 ) );
				node._y				= static_cast<float32>( nodeJson.get( "y" ).asFloat( 40.0 ) );

				const JsonValue choicesVal = nodeJson.get( "choices" );
				if ( choicesVal.isArray() )
				{
					const size_t choiceCount = choicesVal.size();
					node._listChoice.reserve( choiceCount );
					for ( size_t choiceIndex = 0; choiceIndex < choiceCount; ++choiceIndex )
						node._listChoice.push_back( choicesVal.at( choiceIndex ).asString() );
				}

				if ( node._id > 0 )
					_listNode.push_back( std::move( node ) );
			}
		}

		const JsonValue linksVal = root.get( "links" );
		if ( linksVal.isArray() )
		{
			const size_t linkCount = linksVal.size();
			for ( size_t linkIndex = 0; linkIndex < linkCount; ++linkIndex )
			{
				const JsonValue linkJson = linksVal.at( linkIndex );
				if ( linkJson.isObject() == false )
					continue;

				DialogueAssetLink link{};
				link._id	  = static_cast<int32>( linkJson.get( "id" ).asInt( 0 ) );
				link._fromPin = static_cast<int32>( linkJson.get( "from" ).asInt( 0 ) );
				link._toPin	  = static_cast<int32>( linkJson.get( "to" ).asInt( 0 ) );
				if ( link._id > 0 )
					_listLink.push_back( link );
			}
		}
		return true;
	}

	string DialogueGraphAsset::toJson() const
	{
		JsonDocument	doc;
		const JsonValue root = doc.makeObject();

		const JsonValue nodesVal = root.set( "nodes" );
		nodesVal.setArray();
		for ( const DialogueAssetNode& node : _listNode )
		{
			const JsonValue nodeJson = nodesVal.pushBack();
			nodeJson.setObject();
			nodeJson.set( "id" ).setInt( node._id );
			nodeJson.set( "type" ).setString( dialogueAssetTypeName( node._type ) );
			nodeJson.set( "speaker" ).setString( node._speaker );
			nodeJson.set( "text" ).setString( node._text );
			nodeJson.set( "condition" ).setString( node._condition );
			nodeJson.set( "action" ).setString( node._actionCommand );

			const JsonValue choicesVal = nodeJson.set( "choices" );
			choicesVal.setArray();
			for ( const string& choice : node._listChoice )
				choicesVal.pushBack().setString( choice );

			nodeJson.set( "x" ).setFloat( static_cast<float64>( node._x ) );
			nodeJson.set( "y" ).setFloat( static_cast<float64>( node._y ) );
		}

		const JsonValue linksVal = root.set( "links" );
		linksVal.setArray();
		for ( const DialogueAssetLink& link : _listLink )
		{
			const JsonValue linkJson = linksVal.pushBack();
			linkJson.setObject();
			linkJson.set( "id" ).setInt( link._id );
			linkJson.set( "from" ).setInt( link._fromPin );
			linkJson.set( "to" ).setInt( link._toPin );
		}

		return doc.dump( 2 );
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

	int32 DialogueGraphAsset::decodePinNodeId( int32 pin )
	{
		if ( pin <= 0 )
			return 0;
		const int32 scale = ( pin >= 100 ) ? 100 : 10;
		return pin / scale;
	}

	int32 DialogueGraphAsset::decodePinOffset( int32 pin )
	{
		if ( pin <= 0 )
			return 0;
		const int32 scale = ( pin >= 100 ) ? 100 : 10;
		return pin % scale;
	}

	int32 DialogueGraphAsset::findLinkedNodeId( int32 fromNodeId, int32 pinOffset ) const
	{
		for ( const DialogueAssetLink& link : _listLink )
		{
			if ( decodePinNodeId( link._fromPin ) != fromNodeId )
				continue;
			if ( decodePinOffset( link._fromPin ) != pinOffset )
				continue;
			return decodePinNodeId( link._toPin );
		}
		return 0;
	}

	int32 DialogueGraphAsset::findDefaultNextNodeId( int32 fromNodeId ) const
	{
		constexpr int32 kPinOut = 2;
		return findLinkedNodeId( fromNodeId, kPinOut );
	}

	int32 DialogueGraphAsset::findChoiceNextNodeId( int32 fromNodeId, int32 choiceIndex ) const
	{
		constexpr int32 kPinChoiceBase = 10;
		const int32		nextId		   = findLinkedNodeId( fromNodeId, kPinChoiceBase + choiceIndex );
		if ( nextId > 0 )
			return nextId;
		return findDefaultNextNodeId( fromNodeId );
	}

	int32 DialogueGraphAsset::findBranchNextNodeId( int32 fromNodeId, bool bTrue ) const
	{
		constexpr int32 kPinTrue  = 3;
		constexpr int32 kPinFalse = 4;
		return findLinkedNodeId( fromNodeId, bTrue ? kPinTrue : kPinFalse );
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
