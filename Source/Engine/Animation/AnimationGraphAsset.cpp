#include "pch.h"

#include "Engine/Animation/AnimationGraphAsset.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
	bool AnimationGraphAsset::loadFromFile( string_view path )
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

	bool AnimationGraphAsset::saveToFile( string_view path ) const
	{
		if ( path.empty() )
			return false;
		const string dir = FileUtil::getDirectoryPart( path );
		if ( dir.empty() == false )
			FileUtil::ensureDirectoryExists( dir );
		return FileUtil::writeTextFile( path, toJson() );
	}

	bool AnimationGraphAsset::parseJson( string_view jsonView )
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

				AnimationGraphNode node{};
				node._id   = static_cast<int32>( nodeJson.get( "id" ).asInt( 0 ) );
				node._name = nodeJson.get( "name" ).asString();
				node._x	   = static_cast<float32>( nodeJson.get( "x" ).asFloat( 40.0 ) );
				node._y	   = static_cast<float32>( nodeJson.get( "y" ).asFloat( 40.0 ) );
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

				AnimationGraphLink link{};
				link._id	   = static_cast<int32>( linkJson.get( "id" ).asInt( 0 ) );
				link._fromNode = static_cast<int32>( linkJson.get( "from" ).asInt( 0 ) );
				link._toNode   = static_cast<int32>( linkJson.get( "to" ).asInt( 0 ) );
				if ( link._id > 0 )
					_listLink.push_back( link );
			}
		}
		return true;
	}

	string AnimationGraphAsset::toJson() const
	{
		JsonDocument	doc;
		const JsonValue root = doc.makeObject();

		const JsonValue nodesVal = root.set( "nodes" );
		nodesVal.setArray();
		for ( const AnimationGraphNode& node : _listNode )
		{
			const JsonValue nodeJson = nodesVal.pushBack();
			nodeJson.setObject();
			nodeJson.set( "id" ).setInt( node._id );
			nodeJson.set( "name" ).setString( node._name );
			nodeJson.set( "x" ).setFloat( static_cast<float64>( node._x ) );
			nodeJson.set( "y" ).setFloat( static_cast<float64>( node._y ) );
		}

		const JsonValue linksVal = root.set( "links" );
		linksVal.setArray();
		for ( const AnimationGraphLink& link : _listLink )
		{
			const JsonValue linkJson = linksVal.pushBack();
			linkJson.setObject();
			linkJson.set( "id" ).setInt( link._id );
			linkJson.set( "from" ).setInt( link._fromNode );
			linkJson.set( "to" ).setInt( link._toNode );
		}

		return doc.dump( 2 );
	}

	void AnimationGraphAsset::collectNodeNames( vector<string>& outListName ) const
	{
		outListName.clear();
		outListName.reserve( _listNode.size() );
		for ( const AnimationGraphNode& node : _listNode )
		{
			if ( node._name.empty() == false )
				outListName.push_back( node._name );
		}
	}

	const AnimationGraphNode* AnimationGraphAsset::findNode( int32 nodeId ) const
	{
		for ( const AnimationGraphNode& node : _listNode )
		{
			if ( node._id == nodeId )
				return &node;
		}
		return nullptr;
	}

	const AnimationGraphNode* AnimationGraphAsset::findNodeByName( string_view name ) const
	{
		if ( name.empty() )
			return nullptr;
		for ( const AnimationGraphNode& node : _listNode )
		{
			if ( node._name == name )
				return &node;
		}
		return nullptr;
	}

	const AnimationGraphNode* AnimationGraphAsset::findEntryNode() const
	{
		for ( const AnimationGraphNode& node : _listNode )
		{
			bool bHasIncoming = false;
			for ( const AnimationGraphLink& link : _listLink )
			{
				if ( link._toNode != node._id )
					continue;
				bHasIncoming = true;
				break;
			}
			if ( bHasIncoming == false )
				return &node;
		}
		if ( _listNode.empty() )
			return nullptr;
		return &_listNode.front();
	}

	int32 AnimationGraphAsset::findFirstOutgoingNodeId( int32 fromNodeId ) const
	{
		for ( const AnimationGraphLink& link : _listLink )
		{
			if ( link._fromNode == fromNodeId )
				return link._toNode;
		}
		return 0;
	}
} // namespace sw
