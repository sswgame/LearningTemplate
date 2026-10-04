#include "pch.h"

#include "Engine/Animation/AnimGraphAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "AnimGraphAsset" );

    namespace
    {
        struct AnimGraphAssetInternal
        {
            /** @brief 조건 표기 표입니다. 순서는 `AnimConditionOp` 와 같습니다(None 은 표기가 없습니다). */
            static constexpr const utf8* kArrOpText[] = { "", ">", "<", ">=", "<=", "==", "!=", "trigger" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AnimGraphLink::isConditionMet( float32 parameterValue ) const
    {
        switch ( _op )
        {
            case AnimConditionOp::None:
                return false;
            case AnimConditionOp::Greater:
                return parameterValue > _threshold;
            case AnimConditionOp::Less:
                return parameterValue < _threshold;
            case AnimConditionOp::GreaterEqual:
                return parameterValue >= _threshold;
            case AnimConditionOp::LessEqual:
                return parameterValue <= _threshold;
            case AnimConditionOp::Equal:
                return parameterValue == _threshold;
            case AnimConditionOp::NotEqual:
                return parameterValue != _threshold;
            case AnimConditionOp::Trigger:
                return parameterValue != 0.0f;
        }
        return false;
    }

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
        return parseRoot( doc.getRoot() );
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

        return parseRoot( doc.getRoot() );
    }

    bool AnimGraphAsset::parseRoot( const JsonValue& root )
    {
        forEachObjectInArray( root, "nodes", [this]( const JsonValue& nodeJson, size_t /*nodeIndex*/ )
        {
            AnimGraphNode node{};
            node._id          = static_cast<int32>( nodeJson.get( "id" ).asInt( 0 ) );
            node._name        = nodeJson.get( "name" ).asString();
            node._position._x = static_cast<float32>( nodeJson.get( "x" ).asFloat( 40.0 ) );
            node._position._y = static_cast<float32>( nodeJson.get( "y" ).asFloat( 40.0 ) );
            if ( nodeJson.has( "loop" ) )
                node._loopOverride = nodeJson.get( "loop" ).asBool() ? 1 : 0;
            if ( node._id > 0 )
                _listNode.push_back( std::move( node ) );
        } );

        bool bValid = true;
        forEachObjectInArray( root, "links", [this, &bValid]( const JsonValue& linkJson, size_t /*linkIndex*/ )
        {
            AnimGraphLink link{};
            link._id       = static_cast<int32>( linkJson.get( "id" ).asInt( 0 ) );
            link._fromNode = static_cast<int32>( linkJson.get( "from" ).asInt( 0 ) );
            link._toNode   = static_cast<int32>( linkJson.get( "to" ).asInt( 0 ) );
            if ( linkJson.has( "blend" ) )
                link._blendSeconds = static_cast<float32>( linkJson.get( "blend" ).asFloat( -1.0 ) );
            const JsonValue condition = linkJson.get( "condition" );
            if ( condition.isObject() )
            {
                const string opText = condition.get( "op" ).asString();
                if ( parseConditionOp( opText, link._op ) == false || link._op == AnimConditionOp::None )
                {
                    SW_LOG_ERROR( "Animation graph link %# has unknown condition op '%#'", link._id, opText.c_str() );
                    bValid = false;
                }
                link._parameter = hashed_string( condition.get( "param" ).asString() );
                link._threshold = static_cast<float32>( condition.get( "value" ).asFloat( 0.0 ) );
            }
            if ( link._id > 0 )
                _listLink.push_back( link );
        } );
        if ( bValid == false )
        {
            _listNode.clear();
            _listLink.clear();
        }
        return bValid;
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
            if ( node._loopOverride >= 0 )
                nodeJson.set( "loop" ).setBool( node._loopOverride != 0 );
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
            if ( link._blendSeconds >= 0.0f )
                linkJson.set( "blend" ).setFloat( static_cast<float64>( link._blendSeconds ) );
            if ( link._op != AnimConditionOp::None )
            {
                const JsonValue condition = linkJson.set( "condition" );
                condition.setObject();
                condition.set( "param" ).setString( link._parameter.c_str() );
                condition.set( "op" ).setString( getConditionOpText( link._op ) );
                condition.set( "value" ).setFloat( static_cast<float64>( link._threshold ) );
            }
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

    const AnimGraphLink* AnimGraphAsset::findFinishLink( int32 fromNodeId ) const
    {
        for ( const AnimGraphLink& link : _listLink )
        {
            if ( link._fromNode == fromNodeId && link._op == AnimConditionOp::None )
                return &link;
        }
        return nullptr;
    }

    bool AnimGraphAsset::parseConditionOp( string_view text, AnimConditionOp& outOp )
    {
        for ( uint32 index = 1; index < static_cast<uint32>( std::size( AnimGraphAssetInternal::kArrOpText ) ); ++index )
        {
            if ( text == AnimGraphAssetInternal::kArrOpText[index] )
            {
                outOp = static_cast<AnimConditionOp>( index );
                return true;
            }
        }
        return false;
    }

    const utf8* AnimGraphAsset::getConditionOpText( AnimConditionOp op )
    {
        const uint32 index = static_cast<uint32>( op );
        return index < static_cast<uint32>( std::size( AnimGraphAssetInternal::kArrOpText ) ) ? AnimGraphAssetInternal::kArrOpText[index] : "";
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
