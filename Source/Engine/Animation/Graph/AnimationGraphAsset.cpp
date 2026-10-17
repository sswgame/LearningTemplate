#include "pch.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationGraphAsset" );

    namespace
    {
        struct AnimationGraphAssetInternal
        {
            /** @brief 조건 표기 표입니다. 순서는 `AnimationConditionOp` 와 같습니다(None 은 표기가 없습니다). */
            static constexpr const utf8* kArrOpText[] = { "", ">", "<", ">=", "<=", "==", "!=", "trigger" };
            /** @brief 전이 방식 표기 표입니다. 순서는 `AnimationBlendMode` 와 같습니다. */
            static constexpr const utf8* kArrBlendModeText[] = { "crossfade", "inertialization" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AnimationGraphLink::isConditionMet( float32 parameterValue ) const
    {
        switch ( _op )
        {
            case AnimationConditionOp::None:
                return false;
            case AnimationConditionOp::Greater:
                return parameterValue > _threshold;
            case AnimationConditionOp::Less:
                return parameterValue < _threshold;
            case AnimationConditionOp::GreaterEqual:
                return parameterValue >= _threshold;
            case AnimationConditionOp::LessEqual:
                return parameterValue <= _threshold;
            case AnimationConditionOp::Equal:
                return parameterValue == _threshold;
            case AnimationConditionOp::NotEqual:
                return parameterValue != _threshold;
            case AnimationConditionOp::Trigger:
                return parameterValue != 0.0f;
        }
        return false;
    }

    bool AnimationGraphAsset::loadFromFile( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        _listNode.clear();
        _listLink.clear();
        if ( path.empty() )
            return false;

        JSONDocument doc;
        if ( doc.loadPath( path ) == false )
            return false;
        // 파싱한 문서를 그대로 읽는다(문자열로 덤프해 parseJSON 에 다시 넘기지 않는다).
        return parseRoot( doc.getRoot() );
    }

    bool AnimationGraphAsset::saveToFile( string_view path ) const
    {
        if ( path.empty() )
            return false;
        FileUtil::ensureParentDirectoryExists( path );
        return FileUtil::writeTextFile( path, toJSON() );
    }

    bool AnimationGraphAsset::parseJSON( string_view jsonView )
    {
        _listNode.clear();
        _listLink.clear();

        JSONDocument doc;
        if ( doc.parse( jsonView ) == false )
            return false;

        return parseRoot( doc.getRoot() );
    }

    bool AnimationGraphAsset::parseBlendSpace( const JSONValue& blendSpaceJSON, AnimationGraphNode& inoutNode )
    {
        AnimationGraphBlendSpaceDef& def = inoutNode._blendSpace;
        def._parameterX                  = hashed_string( blendSpaceJSON.get( "x" ).asString() );
        if ( blendSpaceJSON.has( "y" ) )
            def._parameterY = hashed_string( blendSpaceJSON.get( "y" ).asString() );
        def._space.clear();
        def._space.setAxisCount( def._parameterY.empty() ? 1u : 2u );
        if ( def._parameterX.empty() )
        {
            SW_LOG_ERROR( "Animation graph node '%#' has a blend space without an 'x' parameter", inoutNode._name.c_str() );
            return false;
        }

        bool bValid = true;
        forEachObjectInArray( blendSpaceJSON, "samples", [&def, &bValid, &inoutNode]( const JSONValue& sampleJSON, size_t sampleIndex )
        {
            const string clipName = sampleJSON.get( "clip" ).asString();
            if ( clipName.empty() )
            {
                SW_LOG_ERROR( "Animation graph node '%#' blend space sample %# has no 'clip'", inoutNode._name.c_str(), sampleIndex );
                bValid = false;
                return;
            }
            const float2 position{ static_cast<float32>( sampleJSON.get( "x" ).asFloat( 0.0 ) ), static_cast<float32>( sampleJSON.get( "y" ).asFloat( 0.0 ) ) };
            def._space.addSample( position, hashed_string( clipName ) );
        } );
        if ( bValid && def._space.getSampleCount() == 0 )
        {
            SW_LOG_ERROR( "Animation graph node '%#' has a blend space without samples", inoutNode._name.c_str() );
            bValid = false;
        }
        return bValid;
    }

    bool AnimationGraphAsset::parseRoot( const JSONValue& root )
    {
        bool bValid = true;
        forEachObjectInArray( root, "nodes", [this, &bValid]( const JSONValue& nodeJSON, size_t /*nodeIndex*/ )
        {
            AnimationGraphNode node{};
            node._id          = static_cast<int32>( nodeJSON.get( "id" ).asInt( 0 ) );
            node._name        = nodeJSON.get( "name" ).asString();
            node._position._x = static_cast<float32>( nodeJSON.get( "x" ).asFloat( 40.0 ) );
            node._position._y = static_cast<float32>( nodeJSON.get( "y" ).asFloat( 40.0 ) );
            if ( nodeJSON.has( "loop" ) )
                node._loopOverride = nodeJSON.get( "loop" ).asBool() ? 1 : 0;
            if ( nodeJSON.has( "sync_group" ) )
                node._syncGroup = hashed_string( nodeJSON.get( "sync_group" ).asString() );
            const JSONValue blendSpaceJSON = nodeJSON.get( "blend_space" );
            if ( blendSpaceJSON.isObject() && parseBlendSpace( blendSpaceJSON, node ) == false )
                bValid = false;
            if ( node._id > 0 )
                _listNode.push_back( std::move( node ) );
        } );

        forEachObjectInArray( root, "links", [this, &bValid]( const JSONValue& linkJSON, size_t /*linkIndex*/ )
        {
            AnimationGraphLink link{};
            link._id       = static_cast<int32>( linkJSON.get( "id" ).asInt( 0 ) );
            link._fromNode = static_cast<int32>( linkJSON.get( "from" ).asInt( 0 ) );
            link._toNode   = static_cast<int32>( linkJSON.get( "to" ).asInt( 0 ) );
            if ( linkJSON.has( "blend" ) )
                link._blendSeconds = static_cast<float32>( linkJSON.get( "blend" ).asFloat( -1.0 ) );
            if ( linkJSON.has( "blend_mode" ) )
            {
                const string modeText = linkJSON.get( "blend_mode" ).asString();
                if ( parseBlendMode( modeText, link._blendMode ) == false )
                {
                    SW_LOG_ERROR( "Animation graph link %# has unknown blend_mode '%#'", link._id, modeText.c_str() );
                    bValid = false;
                }
            }
            if ( linkJSON.has( "half_life" ) )
                link._halfLifeSeconds = static_cast<float32>( linkJSON.get( "half_life" ).asFloat( 0.0 ) );
            if ( link._blendMode == AnimationBlendMode::Inertialization && link._halfLifeSeconds <= 0.0f )
            {
                SW_LOG_ERROR( "Animation graph link %# uses inertialization without a positive 'half_life'", link._id );
                bValid = false;
            }
            const JSONValue condition = linkJSON.get( "condition" );
            if ( condition.isObject() )
            {
                const string opText = condition.get( "op" ).asString();
                if ( parseConditionOp( opText, link._op ) == false || link._op == AnimationConditionOp::None )
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

    string AnimationGraphAsset::toJSON() const
    {
        JSONDocument    doc;
        const JSONValue root = doc.makeObject();

        const JSONValue nodesVal = root.set( "nodes" );
        nodesVal.setArray();
        for ( const AnimationGraphNode& node : _listNode )
        {
            const JSONValue nodeJSON = nodesVal.pushBack();
            nodeJSON.setObject();
            nodeJSON.set( "id" ).setInt( node._id );
            nodeJSON.set( "name" ).setString( node._name );
            nodeJSON.set( "x" ).setFloat( static_cast<float64>( node._position._x ) );
            nodeJSON.set( "y" ).setFloat( static_cast<float64>( node._position._y ) );
            if ( node._loopOverride >= 0 )
                nodeJSON.set( "loop" ).setBool( node._loopOverride != 0 );
            if ( node._syncGroup.empty() == false )
                nodeJSON.set( "sync_group" ).setString( node._syncGroup.c_str() );
            if ( node.hasBlendSpace() )
            {
                const AnimationGraphBlendSpaceDef& def            = node._blendSpace;
                const JSONValue                    blendSpaceJSON = nodeJSON.set( "blend_space" );
                blendSpaceJSON.setObject();
                blendSpaceJSON.set( "x" ).setString( def._parameterX.c_str() );
                if ( def._parameterY.empty() == false )
                    blendSpaceJSON.set( "y" ).setString( def._parameterY.c_str() );
                const JSONValue samplesJSON = blendSpaceJSON.set( "samples" );
                samplesJSON.setArray();
                for ( const BlendSpaceSample& sample : def._space.getSamples() )
                {
                    const JSONValue sampleJSON = samplesJSON.pushBack();
                    sampleJSON.setObject();
                    sampleJSON.set( "clip" ).setString( sample._clipName.c_str() );
                    sampleJSON.set( "x" ).setFloat( static_cast<float64>( sample._position._x ) );
                    if ( def._space.getAxisCount() == 2 )
                        sampleJSON.set( "y" ).setFloat( static_cast<float64>( sample._position._y ) );
                }
            }
        }

        const JSONValue linksVal = root.set( "links" );
        linksVal.setArray();
        for ( const AnimationGraphLink& link : _listLink )
        {
            const JSONValue linkJSON = linksVal.pushBack();
            linkJSON.setObject();
            linkJSON.set( "id" ).setInt( link._id );
            linkJSON.set( "from" ).setInt( link._fromNode );
            linkJSON.set( "to" ).setInt( link._toNode );
            if ( link._blendSeconds >= 0.0f )
                linkJSON.set( "blend" ).setFloat( static_cast<float64>( link._blendSeconds ) );
            // 크로스페이드는 이 키가 생기기 전 파일과 같은 모양으로 쓴다(키가 없으면 크로스페이드다).
            if ( link._blendMode != AnimationBlendMode::Crossfade )
            {
                linkJSON.set( "blend_mode" ).setString( getBlendModeText( link._blendMode ) );
                linkJSON.set( "half_life" ).setFloat( static_cast<float64>( link._halfLifeSeconds ) );
            }
            if ( link._op != AnimationConditionOp::None )
            {
                const JSONValue condition = linkJSON.set( "condition" );
                condition.setObject();
                condition.set( "param" ).setString( link._parameter.c_str() );
                condition.set( "op" ).setString( getConditionOpText( link._op ) );
                condition.set( "value" ).setFloat( static_cast<float64>( link._threshold ) );
            }
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

    const AnimationGraphNode* AnimationGraphAsset::findNode( int32 nodeID ) const
    {
        for ( const AnimationGraphNode& node : _listNode )
        {
            if ( node._id == nodeID )
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

    const AnimationGraphLink* AnimationGraphAsset::findFinishLink( int32 fromNodeID ) const
    {
        for ( const AnimationGraphLink& link : _listLink )
        {
            if ( link._fromNode == fromNodeID && link._op == AnimationConditionOp::None )
                return &link;
        }
        return nullptr;
    }

    bool AnimationGraphAsset::parseConditionOp( string_view text, AnimationConditionOp& outOp )
    {
        for ( uint32 index = 1; index < static_cast<uint32>( std::size( AnimationGraphAssetInternal::kArrOpText ) ); ++index )
        {
            if ( text == AnimationGraphAssetInternal::kArrOpText[index] )
            {
                outOp = static_cast<AnimationConditionOp>( index );
                return true;
            }
        }
        return false;
    }

    const utf8* AnimationGraphAsset::getConditionOpText( AnimationConditionOp op )
    {
        const uint32 index = static_cast<uint32>( op );
        return index < static_cast<uint32>( std::size( AnimationGraphAssetInternal::kArrOpText ) ) ? AnimationGraphAssetInternal::kArrOpText[index] : "";
    }

    bool AnimationGraphAsset::parseBlendMode( string_view text, AnimationBlendMode& outMode )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( std::size( AnimationGraphAssetInternal::kArrBlendModeText ) ); ++index )
        {
            if ( text == AnimationGraphAssetInternal::kArrBlendModeText[index] )
            {
                outMode = static_cast<AnimationBlendMode>( index );
                return true;
            }
        }
        return false;
    }

    const utf8* AnimationGraphAsset::getBlendModeText( AnimationBlendMode mode )
    {
        const uint32 index = static_cast<uint32>( mode );
        return index < static_cast<uint32>( std::size( AnimationGraphAssetInternal::kArrBlendModeText ) ) ? AnimationGraphAssetInternal::kArrBlendModeText[index] : "";
    }

    int32 AnimationGraphAsset::findFirstOutgoingNodeID( int32 fromNodeID ) const
    {
        for ( const AnimationGraphLink& link : _listLink )
        {
            if ( link._fromNode == fromNodeID )
                return link._toNode;
        }
        return 0;
    }
} // namespace sw
