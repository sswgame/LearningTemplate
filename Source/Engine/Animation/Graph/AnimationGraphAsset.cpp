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
            /** @brief 레이어 합치기 표기 표입니다. 순서는 `AnimationLayerBlend` 와 같습니다. */
            static constexpr const utf8* kArrLayerBlendText[] = { "Override", "Additive" };
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
        clear();
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
        clear();

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
        if ( parseLayers( root ) == false )
            bValid = false;
        if ( bValid == false )
            clear();
        return bValid;
    }

    void AnimationGraphAsset::clear()
    {
        _listNode.clear();
        _listLink.clear();
        _listBoneMask.clear();
        _listLayer.clear();
    }

    bool AnimationGraphAsset::parseLayers( const JSONValue& root )
    {
        bool            bValid    = true;
        const JSONValue masksJSON = root.get( "bone_masks" );
        if ( masksJSON.isValid() && masksJSON.isArray() == false )
        {
            SW_LOG_ERROR( "Animation graph 'bone_masks' must be an array" );
            bValid = false;
        }
        for ( size_t maskIndex = 0; masksJSON.isArray() && maskIndex < masksJSON.size(); ++maskIndex )
        {
            const JSONValue      maskJSON = masksJSON.at( maskIndex );
            AnimationBoneMaskDef mask{};
            mask._name = hashed_string( maskJSON.get( "name" ).asString() );
            if ( mask._name.empty() )
            {
                SW_LOG_ERROR( "Animation graph bone mask %# has no 'name'", maskIndex );
                bValid = false;
                continue;
            }
            if ( findBoneMask( mask._name ) != nullptr )
            {
                SW_LOG_ERROR( "Animation graph repeats bone mask '%#'", mask._name.c_str() );
                bValid = false;
                continue;
            }
            // 프리셋 파일("file")이나 바로 적은 본문("roots", "bones") 중 하나다. 둘 다 적으면 어느 쪽이 정본인지 모르므로 오류다.
            const bool bFromFile = maskJSON.has( "file" );
            const bool bInline   = maskJSON.has( "roots" ) || maskJSON.has( "bones" );
            if ( bFromFile && bInline )
            {
                SW_LOG_ERROR( "Animation graph bone mask '%#' has both 'file' and inline bones", mask._name.c_str() );
                bValid = false;
                continue;
            }
            const string context = string( "Animation graph bone mask '" ) + mask._name.c_str() + "'";
            const bool   bLoaded = bFromFile ? mask.loadFromFile( maskJSON.get( "file" ).asString() ) : mask.parseObject( maskJSON, context );
            if ( bLoaded == false )
            {
                bValid = false;
                continue;
            }
            _listBoneMask.push_back( std::move( mask ) );
        }

        const JSONValue layersJSON = root.get( "layers" );
        if ( layersJSON.isValid() && layersJSON.isArray() == false )
        {
            SW_LOG_ERROR( "Animation graph 'layers' must be an array" );
            bValid = false;
        }
        for ( size_t layerIndex = 0; layersJSON.isArray() && layerIndex < layersJSON.size(); ++layerIndex )
        {
            const JSONValue        layerJSON = layersJSON.at( layerIndex );
            AnimationGraphLayerDef layer{};
            layer._name     = hashed_string( layerJSON.get( "name" ).asString() );
            layer._clipName = hashed_string( layerJSON.get( "clip" ).asString() );
            if ( layer._name.empty() || layer._clipName.empty() )
            {
                SW_LOG_ERROR( "Animation graph layer %# needs 'name' and 'clip'", layerIndex );
                bValid = false;
                continue;
            }
            if ( findLayer( layer._name ) != nullptr )
            {
                SW_LOG_ERROR( "Animation graph repeats layer '%#'", layer._name.c_str() );
                bValid = false;
                continue;
            }
            const JSONValue weightJSON = layerJSON.get( "weight" );
            layer._weight              = static_cast<float32>( weightJSON.asFloat( -1.0 ) );
            const bool   bWeightValid  = weightJSON.isNumber() && 0.0f <= layer._weight && layer._weight <= 1.0f;
            const string blendText     = layerJSON.get( "blend" ).asString();
            bool         bBlendValid   = false;
            for ( uint32 blendIndex = 0; blendIndex < static_cast<uint32>( std::size( AnimationGraphAssetInternal::kArrLayerBlendText ) ); ++blendIndex )
            {
                if ( blendText == AnimationGraphAssetInternal::kArrLayerBlendText[blendIndex] )
                {
                    layer._blend = static_cast<AnimationLayerBlend>( blendIndex );
                    bBlendValid  = true;
                }
            }
            if ( bWeightValid == false )
                SW_LOG_ERROR( "Animation graph layer '%#' needs a 'weight' in [0, 1]", layer._name.c_str() );
            if ( bBlendValid == false )
                SW_LOG_ERROR( "Animation graph layer '%#' has unknown 'blend' '%#' (Override, Additive)", layer._name.c_str(), blendText.c_str() );
            if ( layerJSON.has( "loop" ) )
                layer._bLoop = layerJSON.get( "loop" ).asBool() ? SW_TRUE : SW_FALSE;
            if ( layerJSON.has( "weight_param" ) )
                layer._weightParameter = hashed_string( layerJSON.get( "weight_param" ).asString() );
            if ( layerJSON.has( "mask" ) )
                layer._maskName = hashed_string( layerJSON.get( "mask" ).asString() );
            const bool bMaskValid = layer._maskName.empty() || findBoneMask( layer._maskName ) != nullptr;
            if ( bMaskValid == false )
                SW_LOG_ERROR( "Animation graph layer '%#' names unknown bone mask '%#'", layer._name.c_str(), layer._maskName.c_str() );
            const bool bLayerValid = bWeightValid && bBlendValid && bMaskValid;
            if ( bLayerValid == false )
            {
                bValid = false;
                continue;
            }
            _listLayer.push_back( layer );
        }
        return bValid;
    }

    void AnimationGraphAsset::writeLayers( const JSONValue& root ) const
    {
        if ( _listBoneMask.empty() == false )
        {
            const JSONValue masksJSON = root.set( "bone_masks" );
            masksJSON.setArray();
            for ( const AnimationBoneMaskDef& mask : _listBoneMask )
            {
                const JSONValue maskJSON = masksJSON.pushBack();
                maskJSON.setObject();
                maskJSON.set( "name" ).setString( mask._name.c_str() );
                if ( mask._filePath.empty() == false )
                    maskJSON.set( "file" ).setString( mask._filePath );
                else
                    mask.writeObject( maskJSON );
            }
        }
        if ( _listLayer.empty() )
            return;
        const JSONValue layersJSON = root.set( "layers" );
        layersJSON.setArray();
        for ( const AnimationGraphLayerDef& layer : _listLayer )
        {
            const JSONValue layerJSON = layersJSON.pushBack();
            layerJSON.setObject();
            layerJSON.set( "name" ).setString( layer._name.c_str() );
            layerJSON.set( "clip" ).setString( layer._clipName.c_str() );
            if ( layer._maskName.empty() == false )
                layerJSON.set( "mask" ).setString( layer._maskName.c_str() );
            layerJSON.set( "weight" ).setFloat( static_cast<float64>( layer._weight ) );
            layerJSON.set( "blend" ).setString( AnimationGraphAssetInternal::kArrLayerBlendText[static_cast<uint32>( layer._blend )] );
            layerJSON.set( "loop" ).setBool( layer._bLoop == SW_TRUE );
            if ( layer._weightParameter.empty() == false )
                layerJSON.set( "weight_param" ).setString( layer._weightParameter.c_str() );
        }
    }

    const AnimationBoneMaskDef* AnimationGraphAsset::findBoneMask( const hashed_string& name ) const
    {
        for ( const AnimationBoneMaskDef& mask : _listBoneMask )
        {
            if ( mask._name == name )
                return &mask;
        }
        return nullptr;
    }

    const AnimationGraphLayerDef* AnimationGraphAsset::findLayer( const hashed_string& name ) const
    {
        for ( const AnimationGraphLayerDef& layer : _listLayer )
        {
            if ( layer._name == name )
                return &layer;
        }
        return nullptr;
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
            if ( link._op != AnimationConditionOp::None )
            {
                const JSONValue condition = linkJSON.set( "condition" );
                condition.setObject();
                condition.set( "param" ).setString( link._parameter.c_str() );
                condition.set( "op" ).setString( getConditionOpText( link._op ) );
                condition.set( "value" ).setFloat( static_cast<float64>( link._threshold ) );
            }
        }
        writeLayers( root );

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
