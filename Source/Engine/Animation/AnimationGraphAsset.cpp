#include "pch.h"

#include "Engine/Animation/AnimationGraphAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/SimpleJsonWalk.h"

namespace sw
{
	bool AnimationGraphAsset::loadFromFile( string_view path )
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

	bool AnimationGraphAsset::saveToFile( string_view path ) const
	{
		if ( path.empty() )
			return false;
		const string dir = FileUtil::getDirectoryPart( path );
		if ( dir.empty() == false )
			FileUtil::ensureDirectoryExists( dir );
		const string json = toJson();
		return FileUtil::writeFile( path, reinterpret_cast<const uint8*>( json.data() ), json.size() );
	}

	bool AnimationGraphAsset::parseJson( string_view jsonView )
	{
		_listNode.clear();
		_listLink.clear();
		const string json{ jsonView };

		vector<size_t> listNodeObj;
		collectJsonObjectArrayStarts( json, "nodes", listNodeObj );
		const size_t nodesEnd = findJsonArrayEnd( json, "nodes" );
		for ( const size_t obj : listNodeObj )
		{
			AnimationGraphNode node{};
			parseJsonIntField( json, obj, nodesEnd, "id", node._id );
			parseJsonStringField( json, obj, nodesEnd, "name", node._name );
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
			AnimationGraphLink link{};
			parseJsonIntField( json, obj, linksEnd, "id", link._id );
			parseJsonIntField( json, obj, linksEnd, "from", link._fromNode );
			parseJsonIntField( json, obj, linksEnd, "to", link._toNode );
			if ( link._id > 0 )
				_listLink.push_back( link );
		}
		return true;
	}

	string AnimationGraphAsset::toJson() const
	{
		StringBuilder<2048> sb;
		sb.append( "{\n  \"nodes\": [\n" );
		for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
		{
			const AnimationGraphNode& node = _listNode[nodeIndex];
			sb.append( "    { \"id\": " )
				.append( node._id )
				.append( ", \"name\": \"" )
				.append( JsonSerializer::escapeString( node._name ).c_str() )
				.append( "\", \"x\": " )
				.append( node._x )
				.append( ", \"y\": " )
				.append( node._y )
				.append( " }" );
			if ( nodeIndex + 1 < _listNode.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ],\n  \"links\": [\n" );
		for ( size_t linkIndex = 0; linkIndex < _listLink.size(); ++linkIndex )
		{
			const AnimationGraphLink& link = _listLink[linkIndex];
			sb.append( "    { \"id\": " )
				.append( link._id )
				.append( ", \"from\": " )
				.append( link._fromNode )
				.append( ", \"to\": " )
				.append( link._toNode )
				.append( " }" );
			if ( linkIndex + 1 < _listLink.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n}\n" );
		return string{ sb.c_str() };
	}

	void AnimationGraphAsset::collectNodeNames( vector<string>& outNameList ) const
	{
		outNameList.clear();
		outNameList.reserve( _listNode.size() );
		for ( const AnimationGraphNode& node : _listNode )
		{
			if ( node._name.empty() == false )
				outNameList.push_back( node._name );
		}
	}
} // namespace sw
