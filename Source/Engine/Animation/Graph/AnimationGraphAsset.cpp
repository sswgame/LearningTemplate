#include "pch.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"

#include "Core/Container/VectorUtil.h"
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
            /** @brief 포즈 노드 종류 표기 표입니다. 순서는 `AnimationPoseNodeType` 과 같습니다(None 은 표기가 없습니다). */
            static constexpr const utf8* kArrPoseNodeTypeText[] = { "", "blend_poses" };
            /** @brief 레이어 합치기 표기 표입니다. 순서는 `AnimationLayerBlend` 와 같습니다. */
            static constexpr const utf8* kArrLayerBlendText[] = { "Override", "Additive" };
            /** @brief 블렌드 스페이스 알림 방법 표기 표입니다. 순서는 `AnimationBlendSpaceNotifyMode` 와 같습니다. */
            static constexpr const utf8* kArrBlendSpaceNotifyText[] = { "all", "highest_weight", "none" };
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
        if ( doc.loadResource( path ) == false )
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
        def._notifyMode = AnimationBlendSpaceNotifyMode::All;
        if ( blendSpaceJSON.has( "notify" ) )
        {
            const string notifyText = blendSpaceJSON.get( "notify" ).asString();
            bool         bKnown     = false;
            for ( uint32 index = 0; index < static_cast<uint32>( std::size( AnimationGraphAssetInternal::kArrBlendSpaceNotifyText ) ); ++index )
            {
                if ( notifyText == AnimationGraphAssetInternal::kArrBlendSpaceNotifyText[index] )
                {
                    def._notifyMode = static_cast<AnimationBlendSpaceNotifyMode>( index );
                    bKnown          = true;
                }
            }
            if ( bKnown == false )
            {
                SW_LOG_ERROR( "Animation graph node '%#' blend space has unknown 'notify' '%#' (all, highest_weight, none)", inoutNode._name.c_str(), notifyText.c_str() );
                return false;
            }
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
            // 적지 않은 축은 0 이다("y" 가 없으면 1D).
            float2 position{};
            if ( sampleJSON.readOptionalInto( "x", position._x ) == false || sampleJSON.readOptionalInto( "y", position._y ) == false )
            {
                SW_LOG_ERROR( "Animation graph node '%#' blend space sample %# has a non-numeric position", inoutNode._name.c_str(), sampleIndex );
                bValid = false;
                return;
            }
            def._space.addSample( position, hashed_string( clipName ) );
        } );
        if ( bValid && def._space.getSampleCount() == 0 )
        {
            SW_LOG_ERROR( "Animation graph node '%#' has a blend space without samples", inoutNode._name.c_str() );
            bValid = false;
        }
        return bValid;
    }

    bool AnimationGraphAsset::parsePoseNode( const JSONValue& poseNodeJSON, AnimationGraphNode& inoutNode )
    {
        AnimationGraphPoseNodeDef& def      = inoutNode._poseNode;
        const string               typeText = poseNodeJSON.get( "type" ).asString();
        def                                 = AnimationGraphPoseNodeDef{};
        for ( uint32 index = 1; index < static_cast<uint32>( std::size( AnimationGraphAssetInternal::kArrPoseNodeTypeText ) ); ++index )
        {
            if ( typeText == AnimationGraphAssetInternal::kArrPoseNodeTypeText[index] )
                def._type = static_cast<AnimationPoseNodeType>( index );
        }
        if ( def._type == AnimationPoseNodeType::None )
        {
            SW_LOG_ERROR( "Animation graph node '%#' has unknown pose_node type '%#'", inoutNode._name.c_str(), typeText.c_str() );
            return false;
        }
        const JSONValue inputsJSON = poseNodeJSON.get( "inputs" );
        for ( size_t inputIndex = 0; inputsJSON.isArray() && inputIndex < inputsJSON.size(); ++inputIndex )
        {
            def._listInput.push_back( hashed_string( inputsJSON.at( inputIndex ).asString() ) );
        }
        if ( poseNodeJSON.has( "weight" ) )
            def._weightParameter = hashed_string( poseNodeJSON.get( "weight" ).asString() );

        // 종류마다 요구하는 값 — 지금은 blend_poses 하나다.
        const bool bHasEmptyInput     = VectorUtil::contains( def._listInput, hashed_string{} );
        const bool bHasRequiredValues = def._listInput.size() == 2 && bHasEmptyInput == false && def._weightParameter.empty() == false;
        if ( bHasRequiredValues == false )
        {
            SW_LOG_ERROR( "Animation graph node '%#' blend_poses needs two named 'inputs' and a 'weight' parameter", inoutNode._name.c_str() );
            return false;
        }
        return true;
    }

    bool AnimationGraphAsset::parseRoot( const JSONValue& root )
    {
        bool bValid = true;
        forEachObjectInArray( root, "nodes", [this, &bValid]( const JSONValue& nodeJSON, size_t /*nodeIndex*/ )
        {
            // id 는 필수이고 1 이상이다. 위치는 없으면 노드의 초기 자리다. 반복은 적었을 때만 클립 설정을 덮는다.
            AnimationGraphNode node{};
            node._name                                 = nodeJSON.get( "name" ).asString();
            const std::optional<int32> nodeID          = nodeJSON.readRequired<int32>( "id" );
            bool                       bLoop           = false;
            const bool                 bHasLoop        = nodeJSON.has( "loop" );
            const bool                 bNodeFieldsRead = nodeID.has_value() && *nodeID > 0 && nodeJSON.readOptionalInto( "x", node._position._x ) &&
                                         nodeJSON.readOptionalInto( "y", node._position._y ) && nodeJSON.readOptionalInto( "loop", bLoop );
            if ( bNodeFieldsRead == false )
            {
                SW_LOG_ERROR( "Animation graph node '%#' needs an 'id' of 1 or more and numeric 'x', 'y' and boolean 'loop'", node._name.c_str() );
                bValid = false;
                return;
            }
            node._id = *nodeID;
            if ( bHasLoop )
                node._loopOverride = bLoop ? 1 : 0;
            if ( nodeJSON.has( "sync_group" ) )
                node._syncGroup = hashed_string( nodeJSON.get( "sync_group" ).asString() );
            const JSONValue blendSpaceJSON = nodeJSON.get( "blend_space" );
            if ( blendSpaceJSON.isObject() && parseBlendSpace( blendSpaceJSON, node ) == false )
                bValid = false;
            const JSONValue poseNodeJSON = nodeJSON.get( "pose_node" );
            if ( poseNodeJSON.isObject() && parsePoseNode( poseNodeJSON, node ) == false )
                bValid = false;
            if ( node.hasBlendSpace() && node.hasPoseNode() )
            {
                SW_LOG_ERROR( "Animation graph node '%#' has both blend_space and pose_node", node._name.c_str() );
                bValid = false;
            }
            _listNode.push_back( std::move( node ) );
        } );

        forEachObjectInArray( root, "links", [this, &bValid]( const JSONValue& linkJSON, size_t /*linkIndex*/ )
        {
            // id · from · to 는 필수다. 크로스페이드 길이 · 반감기는 없으면 링크의 초기값(플레이어 기본 · 즉시)이다.
            AnimationGraphLink         link{};
            const std::optional<int32> linkID          = linkJSON.readRequired<int32>( "id" );
            const std::optional<int32> fromNode        = linkJSON.readRequired<int32>( "from" );
            const std::optional<int32> toNode          = linkJSON.readRequired<int32>( "to" );
            const bool                 bLinkFieldsRead = linkID.has_value() && *linkID > 0 && fromNode.has_value() && toNode.has_value() && linkJSON.readOptionalInto( "blend", link._blendSeconds ) &&
                                         linkJSON.readOptionalInto( "half_life", link._halfLifeSeconds );
            if ( bLinkFieldsRead == false )
            {
                SW_LOG_ERROR( "Animation graph link needs an 'id' of 1 or more, 'from' and 'to' node ids and numeric 'blend' and 'half_life'" );
                bValid = false;
                return;
            }
            link._id       = *linkID;
            link._fromNode = *fromNode;
            link._toNode   = *toNode;
            if ( linkJSON.has( "blend_mode" ) )
            {
                const string modeText = linkJSON.get( "blend_mode" ).asString();
                if ( parseBlendMode( modeText, link._blendMode ) == false )
                {
                    SW_LOG_ERROR( "Animation graph link %# has unknown blend_mode '%#'", link._id, modeText.c_str() );
                    bValid = false;
                }
            }
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
                if ( condition.readOptionalInto( "value", link._threshold ) == false )
                {
                    SW_LOG_ERROR( "Animation graph link %# has a non-numeric condition value", link._id );
                    bValid = false;
                }
            }
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
            const std::optional<float32> weight       = layerJSON.get( "weight" ).isNumber() ? layerJSON.readRequired<float32>( "weight" ) : std::nullopt;
            const bool                   bWeightValid = weight.has_value() && 0.0f <= *weight && *weight <= 1.0f;
            if ( bWeightValid )
                layer._weight = *weight;
            const string blendText   = layerJSON.get( "blend" ).asString();
            bool         bBlendValid = false;
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
            bool       bLayerLoop = layer._bLoop != SW_FALSE;
            const bool bLoopValid = layerJSON.readOptionalInto( "loop", bLayerLoop );
            layer._bLoop          = bLayerLoop ? SW_TRUE : SW_FALSE;
            if ( layerJSON.has( "weight_param" ) )
                layer._weightParameter = hashed_string( layerJSON.get( "weight_param" ).asString() );
            if ( layerJSON.has( "mask" ) )
                layer._maskName = hashed_string( layerJSON.get( "mask" ).asString() );
            const bool bMaskValid = layer._maskName.empty() || findBoneMask( layer._maskName ) != nullptr;
            if ( bMaskValid == false )
                SW_LOG_ERROR( "Animation graph layer '%#' names unknown bone mask '%#'", layer._name.c_str(), layer._maskName.c_str() );
            bool bHalfLifeValid = true;
            if ( layerJSON.has( "half_life" ) )
            {
                const std::optional<float32> halfLife = layerJSON.get( "half_life" ).isNumber() ? layerJSON.readRequired<float32>( "half_life" ) : std::nullopt;
                bHalfLifeValid                        = halfLife.has_value() && *halfLife > 0.0f;
                if ( bHalfLifeValid )
                    layer._halfLifeSeconds = *halfLife;
                if ( bHalfLifeValid == false )
                    SW_LOG_ERROR( "Animation graph layer '%#' needs a positive 'half_life'", layer._name.c_str() );
            }
            const bool bLayerValid = bWeightValid && bBlendValid && bMaskValid && bHalfLifeValid && bLoopValid;
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
            layerJSON.set( "blend" ).setString( getLayerBlendText( layer._blend ) );
            layerJSON.set( "loop" ).setBool( layer._bLoop == SW_TRUE );
            if ( layer._weightParameter.empty() == false )
                layerJSON.set( "weight_param" ).setString( layer._weightParameter.c_str() );
            if ( layer._halfLifeSeconds > 0.0f )
                layerJSON.set( "half_life" ).setFloat( static_cast<float64>( layer._halfLifeSeconds ) );
        }
    }

    const AnimationBoneMaskDef* AnimationGraphAsset::findBoneMask( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AnimationBoneMaskDef::_name>( _listBoneMask, name );
    }

    const AnimationGraphLayerDef* AnimationGraphAsset::findLayer( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AnimationGraphLayerDef::_name>( _listLayer, name );
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
                // 기본값(all)은 이 키가 생기기 전 파일과 같은 모양으로 쓴다(키가 없으면 all 이다).
                if ( def._notifyMode != AnimationBlendSpaceNotifyMode::All )
                    blendSpaceJSON.set( "notify" ).setString( AnimationGraphAssetInternal::kArrBlendSpaceNotifyText[static_cast<uint32>( def._notifyMode )] );
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
            if ( node.hasPoseNode() )
            {
                const AnimationGraphPoseNodeDef& def          = node._poseNode;
                const JSONValue                  poseNodeJSON = nodeJSON.set( "pose_node" );
                poseNodeJSON.setObject();
                poseNodeJSON.set( "type" ).setString( AnimationGraphAssetInternal::kArrPoseNodeTypeText[static_cast<uint32>( def._type )] );
                const JSONValue inputsJSON = poseNodeJSON.set( "inputs" );
                inputsJSON.setArray();
                for ( const hashed_string& input : def._listInput )
                {
                    inputsJSON.pushBack().setString( input.c_str() );
                }
                if ( def._weightParameter.empty() == false )
                    poseNodeJSON.set( "weight" ).setString( def._weightParameter.c_str() );
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
        return VectorUtil::findBy<&AnimationGraphNode::_id>( _listNode, nodeID );
    }

    const AnimationGraphNode* AnimationGraphAsset::findNodeByName( string_view name ) const
    {
        if ( name.empty() )
            return nullptr;
        return VectorUtil::findBy<&AnimationGraphNode::_name>( _listNode, name );
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
        return VectorUtil::findIf( _listLink, [&]( const AnimationGraphLink& link )
        { return link._fromNode == fromNodeID && link._op == AnimationConditionOp::None; } );
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

    const utf8* AnimationGraphAsset::getLayerBlendText( AnimationLayerBlend blend )
    {
        const uint32 index = static_cast<uint32>( blend );
        return index < static_cast<uint32>( std::size( AnimationGraphAssetInternal::kArrLayerBlendText ) ) ? AnimationGraphAssetInternal::kArrLayerBlendText[index] : "";
    }
} // namespace sw
