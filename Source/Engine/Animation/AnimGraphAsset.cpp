#include "pch.h"

#include "Engine/Animation/AnimGraphAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    bool AnimGraphAsset::loadFromFile( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        _listNode.clear();
        _listLink.clear();
        if ( path.empty() )
            return false;

        JsonDocument doc;
        if ( doc.loadPath( path ) == false )
            return false;
        // 파싱한 문서를 그대로 읽는다(문자열로 덤프해 parseJson 에 다시 넘기지 않는다).
        parseRoot( doc.getRoot() );
        return true;
    }

    bool AnimGraphAsset::saveToFile( string_view path ) const
    {
        if ( path.empty() )
            return false;
        FileUtil::ensureParentDirectoryExists( path );
        return FileUtil::writeTextFile( path, toJson() );
    }

    bool AnimGraphAsset::parseJson( string_view jsonView )
    {
        _listNode.clear();
        _listLink.clear();

        JsonDocument doc;
        if ( doc.parse( jsonView ) == false )
            return false;

        parseRoot( doc.getRoot() );
        return true;
    }

    void AnimGraphAsset::parseRoot( const JsonValue& root )
    {
        forEachObjectInArray( root, "nodes", [this]( const JsonValue& nodeJson, size_t /*nodeIndex*/ )
        {
            AnimGraphNode node{};
            node._id          = static_cast<int32>( nodeJson.get( "id" ).asInt( 0 ) );
            node._name        = nodeJson.get( "name" ).asString();
            node._position._x = static_cast<float32>( nodeJson.get( "x" ).asFloat( 40.0 ) );
            node._position._y = static_cast<float32>( nodeJson.get( "y" ).asFloat( 40.0 ) );
            if ( node._id > 0 )
                _listNode.push_back( std::move( node ) );
        } );

        forEachObjectInArray( root, "links", [this]( const JsonValue& linkJson, size_t /*linkIndex*/ )
        {
            AnimGraphLink link{};
            link._id       = static_cast<int32>( linkJson.get( "id" ).asInt( 0 ) );
            link._fromNode = static_cast<int32>( linkJson.get( "from" ).asInt( 0 ) );
            link._toNode   = static_cast<int32>( linkJson.get( "to" ).asInt( 0 ) );
            if ( link._id > 0 )
                _listLink.push_back( link );
        } );
    }

    string AnimGraphAsset::toJson() const
    {
        JsonDocument    doc;
        const JsonValue root = doc.makeObject();

        const JsonValue nodesVal = root.set( "nodes" );
        nodesVal.setArray();
        for ( const AnimGraphNode& node : _listNode )
        {
            const JsonValue nodeJson = nodesVal.pushBack();
            nodeJson.setObject();
            nodeJson.set( "id" ).setInt( node._id );
            nodeJson.set( "name" ).setString( node._name );
            nodeJson.set( "x" ).setFloat( static_cast<float64>( node._position._x ) );
            nodeJson.set( "y" ).setFloat( static_cast<float64>( node._position._y ) );
        }

        const JsonValue linksVal = root.set( "links" );
        linksVal.setArray();
        for ( const AnimGraphLink& link : _listLink )
        {
            const JsonValue linkJson = linksVal.pushBack();
            linkJson.setObject();
            linkJson.set( "id" ).setInt( link._id );
            linkJson.set( "from" ).setInt( link._fromNode );
            linkJson.set( "to" ).setInt( link._toNode );
        }

        return doc.dump( 2 );
    }

    void AnimGraphAsset::collectNodeNames( vector<string>& outListName ) const
    {
        outListName.clear();
        outListName.reserve( _listNode.size() );
        for ( const AnimGraphNode& node : _listNode )
        {
            if ( node._name.empty() == false )
                outListName.push_back( node._name );
        }
    }

    const AnimGraphNode* AnimGraphAsset::findNode( int32 nodeId ) const
    {
        for ( const AnimGraphNode& node : _listNode )
        {
            if ( node._id == nodeId )
                return &node;
        }
        return nullptr;
    }

    const AnimGraphNode* AnimGraphAsset::findNodeByName( string_view name ) const
    {
        if ( name.empty() )
            return nullptr;
        for ( const AnimGraphNode& node : _listNode )
        {
            if ( node._name == name )
                return &node;
        }
        return nullptr;
    }

    const AnimGraphNode* AnimGraphAsset::findEntryNode() const
    {
        for ( const AnimGraphNode& node : _listNode )
        {
            bool bHasIncoming = false;
            for ( const AnimGraphLink& link : _listLink )
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

    int32 AnimGraphAsset::findFirstOutgoingNodeId( int32 fromNodeId ) const
    {
        for ( const AnimGraphLink& link : _listLink )
        {
            if ( link._fromNode == fromNodeId )
                return link._toNode;
        }
        return 0;
    }
} // namespace sw
