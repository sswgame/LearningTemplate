#include "pch.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

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

    bool AnimationGraphAsset::loadFromFile( const ResourceID& resourceID )
    {
        const string_view path = resourceID.view();
        SW_MEMORY_SCOPE( Animation );
        clear();
        if ( path.empty() )
            return false;

        XMLDocument doc;
        if ( doc.loadResource( resourceID ) == false )
            return false;
        // 파싱한 문서를 그대로 읽는다(문자열로 덤프해 parseXML 에 다시 넘기지 않는다).
        return parseRoot( doc.getRoot( "AnimationGraphAsset" ), path );
    }

    bool AnimationGraphAsset::loadFromDiskFile( string_view diskPath )
    {
        SW_MEMORY_SCOPE( Animation );
        clear();
        if ( diskPath.empty() )
            return false;

        XMLDocument doc;
        if ( doc.loadFile( diskPath ) == false )
            return false;
        return parseRoot( doc.getRoot( "AnimationGraphAsset" ), diskPath );
    }

    bool AnimationGraphAsset::saveToFile( string_view path ) const
    {
        if ( path.empty() )
            return false;
        return FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFile( path, toXML() );
    }

    bool AnimationGraphAsset::parseXML( string_view xmlView )
    {
        clear();

        XMLDocument doc;
        if ( doc.parse( xmlView, "<animation graph text>" ) == false )
            return false;

        return parseRoot( doc.getRoot( "AnimationGraphAsset" ), "<animation graph text>" );
    }

    bool AnimationGraphAsset::parseBlendSpace( const XMLNode& blendSpaceXML, AnimationGraphNode& inoutNode )
    {
        if ( XMLReadUtil::hasOnlyKnownNames( blendSpaceXML, { "x", "y", "notify" }, { "samples" }, inoutNode._name ) == false )
            return false;
        AnimationGraphBlendSpaceDef& def = inoutNode._blendSpace;
        def._parameterX                  = hashed_string( string_view{ blendSpaceXML.getAttributeText( "x" ) } );
        if ( blendSpaceXML.findAttribute( "y" ) != nullptr )
            def._parameterY = hashed_string( string_view{ blendSpaceXML.getAttributeText( "y" ) } );
        def._space.clear();
        def._space.setAxisCount( def._parameterY.empty() ? 1u : 2u );
        if ( def._parameterX.empty() )
        {
            SW_LOG_ERROR( "Animation graph node '%#' has a blend space without an 'x' parameter", inoutNode._name.c_str() );
            return false;
        }
        def._notifyMode = AnimationBlendSpaceNotifyMode::All;
        if ( blendSpaceXML.findAttribute( "notify" ) != nullptr )
        {
            const string notifyText{ blendSpaceXML.getAttributeText( "notify" ) };
            bool         bKnown = false;
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

        bool   bValid = true;
        size_t sampleIndex{ 0 };
        for ( XMLNode sampleXML = blendSpaceXML.findChild( "samples" ).findChild(); sampleXML.isValid(); sampleXML = sampleXML.findNextSibling(), ++sampleIndex )
        {
            if ( string_view{ sampleXML.getName() } != "sample" || XMLReadUtil::hasOnlyKnownNames( sampleXML, { "clip", "x", "y" }, {}, inoutNode._name ) == false )
            {
                SW_LOG_ERROR( "Animation graph node '%#' blend space <samples> holds only <sample clip x y> elements", inoutNode._name.c_str() );
                bValid = false;
                continue;
            }
            const string clipName{ sampleXML.getAttributeText( "clip" ) };
            if ( clipName.empty() )
            {
                SW_LOG_ERROR( "Animation graph node '%#' blend space sample %# has no 'clip'", inoutNode._name.c_str(), sampleIndex );
                bValid = false;
                continue;
            }
            // 적지 않은 축은 0 이다("y" 가 없으면 1D).
            std::optional<float32> positionX;
            std::optional<float32> positionY;
            if ( sampleXML.readOptionalAttribute( "x", positionX ) == false || sampleXML.readOptionalAttribute( "y", positionY ) == false )
            {
                SW_LOG_ERROR( "Animation graph node '%#' blend space sample %# has a non-numeric position", inoutNode._name.c_str(), sampleIndex );
                bValid = false;
                continue;
            }
            def._space.addSample( float2{ positionX.value_or( 0.0f ), positionY.value_or( 0.0f ) }, hashed_string( clipName ) );
        }
        if ( bValid && def._space.getSampleCount() == 0 )
        {
            SW_LOG_ERROR( "Animation graph node '%#' has a blend space without samples", inoutNode._name.c_str() );
            bValid = false;
        }
        return bValid;
    }

    bool AnimationGraphAsset::parsePoseNode( const XMLNode& poseNodeXML, AnimationGraphNode& inoutNode )
    {
        if ( XMLReadUtil::hasOnlyKnownNames( poseNodeXML, { "type", "weight" }, { "inputs" }, inoutNode._name ) == false )
            return false;
        AnimationGraphPoseNodeDef& def = inoutNode._poseNode;
        const string               typeText{ poseNodeXML.getAttributeText( "type" ) };
        def = AnimationGraphPoseNodeDef{};
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
        for ( XMLNode input = poseNodeXML.findChild( "inputs" ).findChild(); input.isValid(); input = input.findNextSibling() )
        {
            def._listInput.push_back( hashed_string( string_view{ input.getText() } ) );
        }
        if ( poseNodeXML.findAttribute( "weight" ) != nullptr )
            def._weightParameter = hashed_string( string_view{ poseNodeXML.getAttributeText( "weight" ) } );

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

    bool AnimationGraphAsset::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Animation graph '%#': expected an <AnimationGraphAsset> root element", sourceLabel );
            return false;
        }
        bool bValid = XMLReadUtil::hasOnlyKnownNames( root, {}, { "nodes", "links", "bone_masks", "layers" }, sourceLabel );
        bValid      = parseNodes( root, sourceLabel ) && bValid;
        bValid      = parseLinks( root, sourceLabel ) && bValid;
        if ( parseLayers( root ) == false )
            bValid = false;
        if ( bValid == false )
            clear();
        return bValid;
    }

    bool AnimationGraphAsset::parseNodes( const XMLNode& root, string_view sourceLabel )
    {
        bool bValid = true;
        for ( XMLNode nodeXML = root.findChild( "nodes" ).findChild(); nodeXML.isValid(); nodeXML = nodeXML.findNextSibling() )
        {
            // id 는 필수이고 1 이상이다. 위치는 없으면 노드의 초기 자리다. 반복은 적었을 때만 클립 설정을 덮는다.
            AnimationGraphNode node{};
            node._name = string{ nodeXML.getAttributeText( "name" ) };
            if ( string_view{ nodeXML.getName() } != "node" ||
                 XMLReadUtil::hasOnlyKnownNames( nodeXML, { "id", "name", "x", "y", "loop", "sync_group" }, { "blend_space", "pose_node" }, node._name ) == false )
            {
                SW_LOG_ERROR( "Animation graph '%#': <nodes> holds only <node> elements", sourceLabel );
                bValid = false;
                continue;
            }
            const std::optional<int32> nodeID = nodeXML.readRequiredAttribute<int32>( "id" );
            std::optional<float32>     positionX;
            std::optional<float32>     positionY;
            std::optional<bool>        loop;
            const bool                 bNodeFieldsRead = nodeID.has_value() && *nodeID > 0 && nodeXML.readOptionalAttribute( "x", positionX ) && nodeXML.readOptionalAttribute( "y", positionY ) &&
                                         nodeXML.readOptionalAttribute( "loop", loop );
            if ( bNodeFieldsRead == false )
            {
                SW_LOG_ERROR( "Animation graph node '%#' needs an 'id' of 1 or more and numeric 'x', 'y' and boolean 'loop'", node._name.c_str() );
                bValid = false;
                continue;
            }
            node._id = *nodeID;
            if ( positionX.has_value() )
                node._position._x = *positionX;
            if ( positionY.has_value() )
                node._position._y = *positionY;
            if ( loop.has_value() )
                node._loopOverride = *loop ? 1 : 0;
            if ( nodeXML.findAttribute( "sync_group" ) != nullptr )
                node._syncGroup = hashed_string( string_view{ nodeXML.getAttributeText( "sync_group" ) } );
            const XMLNode blendSpaceXML = nodeXML.findChild( "blend_space" );
            if ( blendSpaceXML.isValid() && parseBlendSpace( blendSpaceXML, node ) == false )
                bValid = false;
            const XMLNode poseNodeXML = nodeXML.findChild( "pose_node" );
            if ( poseNodeXML.isValid() && parsePoseNode( poseNodeXML, node ) == false )
                bValid = false;
            if ( node.hasBlendSpace() && node.hasPoseNode() )
            {
                SW_LOG_ERROR( "Animation graph node '%#' has both blend_space and pose_node", node._name.c_str() );
                bValid = false;
            }
            _listNode.push_back( std::move( node ) );
        }
        return bValid;
    }

    bool AnimationGraphAsset::parseLinks( const XMLNode& root, string_view sourceLabel )
    {
        bool bValid = true;
        for ( XMLNode linkXML = root.findChild( "links" ).findChild(); linkXML.isValid(); linkXML = linkXML.findNextSibling() )
        {
            // id · from · to 는 필수다. 크로스페이드 길이 · 반감기는 없으면 링크의 초기값(플레이어 기본 · 즉시)이다.
            if ( string_view{ linkXML.getName() } != "link" ||
                 XMLReadUtil::hasOnlyKnownNames( linkXML, { "id", "from", "to", "blend", "blend_mode", "half_life" }, { "condition" }, sourceLabel ) == false )
            {
                SW_LOG_ERROR( "Animation graph '%#': <links> holds only <link> elements", sourceLabel );
                bValid = false;
                continue;
            }
            AnimationGraphLink         link{};
            const std::optional<int32> linkID   = linkXML.readRequiredAttribute<int32>( "id" );
            const std::optional<int32> fromNode = linkXML.readRequiredAttribute<int32>( "from" );
            const std::optional<int32> toNode   = linkXML.readRequiredAttribute<int32>( "to" );
            std::optional<float32>     blendSeconds;
            std::optional<float32>     halfLifeSeconds;
            const bool                 bLinkFieldsRead = linkID.has_value() && *linkID > 0 && fromNode.has_value() && toNode.has_value() && linkXML.readOptionalAttribute( "blend", blendSeconds ) &&
                                         linkXML.readOptionalAttribute( "half_life", halfLifeSeconds );
            if ( bLinkFieldsRead == false )
            {
                SW_LOG_ERROR( "Animation graph link needs an 'id' of 1 or more, 'from' and 'to' node ids and numeric 'blend' and 'half_life'" );
                bValid = false;
                continue;
            }
            link._id       = *linkID;
            link._fromNode = *fromNode;
            link._toNode   = *toNode;
            if ( blendSeconds.has_value() )
                link._blendSeconds = *blendSeconds;
            if ( halfLifeSeconds.has_value() )
                link._halfLifeSeconds = *halfLifeSeconds;
            if ( linkXML.findAttribute( "blend_mode" ) != nullptr )
            {
                const string modeText{ linkXML.getAttributeText( "blend_mode" ) };
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
            const XMLNode condition = linkXML.findChild( "condition" );
            if ( condition.isValid() )
            {
                if ( XMLReadUtil::hasOnlyKnownNames( condition, { "param", "op", "value" }, {}, sourceLabel ) == false )
                    bValid = false;
                const string opText{ condition.getAttributeText( "op" ) };
                if ( parseConditionOp( opText, link._op ) == false || link._op == AnimationConditionOp::None )
                {
                    SW_LOG_ERROR( "Animation graph link %# has unknown condition op '%#'", link._id, opText.c_str() );
                    bValid = false;
                }
                link._parameter = hashed_string( string_view{ condition.getAttributeText( "param" ) } );
                std::optional<float32> threshold;
                if ( condition.readOptionalAttribute( "value", threshold ) == false )
                {
                    SW_LOG_ERROR( "Animation graph link %# has a non-numeric condition value", link._id );
                    bValid = false;
                }
                if ( threshold.has_value() )
                    link._threshold = *threshold;
            }
            _listLink.push_back( link );
        }
        return bValid;
    }

    void AnimationGraphAsset::clear()
    {
        _listNode.clear();
        _listLink.clear();
        _listBoneMask.clear();
        _listLayer.clear();
    }

    bool AnimationGraphAsset::parseLayers( const XMLNode& root )
    {
        const bool bMasksValid = parseBoneMasks( root );
        return parseLayerDefs( root ) && bMasksValid;
    }

    bool AnimationGraphAsset::parseBoneMasks( const XMLNode& root )
    {
        bool   bValid = true;
        size_t maskIndex{ 0 };
        for ( XMLNode maskXML = root.findChild( "bone_masks" ).findChild(); maskXML.isValid(); maskXML = maskXML.findNextSibling(), ++maskIndex )
        {
            AnimationBoneMaskDef mask{};
            mask._name = hashed_string( string_view{ maskXML.getAttributeText( "name" ) } );
            if ( string_view{ maskXML.getName() } != "bone_mask" )
            {
                SW_LOG_ERROR( "Animation graph <bone_masks> holds only <bone_mask> elements" );
                bValid = false;
                continue;
            }
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
            // 프리셋 파일("file")이나 바로 적은 본문(<roots>, <bones>) 중 하나다. 둘 다 적으면 어느 쪽이 기준인지 모르므로 오류다.
            if ( XMLReadUtil::hasOnlyKnownNames( maskXML, { "name", "file" }, { "roots", "bones" }, mask._name.c_str() ) == false )
            {
                bValid = false;
                continue;
            }
            const bool bFromFile = maskXML.findAttribute( "file" ) != nullptr;
            const bool bInline   = maskXML.findChild( "roots" ).isValid() || maskXML.findChild( "bones" ).isValid();
            if ( bFromFile && bInline )
            {
                SW_LOG_ERROR( "Animation graph bone mask '%#' has both 'file' and inline bones", mask._name.c_str() );
                bValid = false;
                continue;
            }
            const string context = string( "Animation graph bone mask '" ) + mask._name.c_str() + "'";
            const bool   bLoaded = bFromFile ? mask.loadFromFile( ResourceID{ maskXML.getAttributeText( "file" ) } ) : mask.parseObject( maskXML, context );
            if ( bLoaded == false )
            {
                bValid = false;
                continue;
            }
            _listBoneMask.push_back( std::move( mask ) );
        }
        return bValid;
    }

    bool AnimationGraphAsset::parseLayerDefs( const XMLNode& root )
    {
        bool   bValid = true;
        size_t layerIndex{ 0 };
        for ( XMLNode layerXML = root.findChild( "layers" ).findChild(); layerXML.isValid(); layerXML = layerXML.findNextSibling(), ++layerIndex )
        {
            AnimationGraphLayerDef layer{};
            layer._name     = hashed_string( string_view{ layerXML.getAttributeText( "name" ) } );
            layer._clipName = hashed_string( string_view{ layerXML.getAttributeText( "clip" ) } );
            if ( string_view{ layerXML.getName() } != "layer" || XMLReadUtil::hasOnlyKnownNames( layerXML, { "name", "clip", "mask", "weight", "blend", "loop", "weight_param", "half_life" }, {}, "Animation graph layer" ) == false )
            {
                SW_LOG_ERROR( "Animation graph <layers> holds only <layer> elements" );
                bValid = false;
                continue;
            }
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
            const std::optional<float32> weight       = layerXML.readRequiredAttribute<float32>( "weight" );
            const bool                   bWeightValid = weight.has_value() && 0.0f <= *weight && *weight <= 1.0f;
            if ( bWeightValid )
                layer._weight = *weight;
            const string blendText{ layerXML.getAttributeText( "blend" ) };
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
            std::optional<bool> layerLoop;
            const bool          bLoopValid = layerXML.readOptionalAttribute( "loop", layerLoop );
            if ( layerLoop.has_value() )
                layer._bLoop = *layerLoop ? SW_TRUE : SW_FALSE;
            if ( layerXML.findAttribute( "weight_param" ) != nullptr )
                layer._weightParameter = hashed_string( string_view{ layerXML.getAttributeText( "weight_param" ) } );
            if ( layerXML.findAttribute( "mask" ) != nullptr )
                layer._maskName = hashed_string( string_view{ layerXML.getAttributeText( "mask" ) } );
            const bool bMaskValid = layer._maskName.empty() || findBoneMask( layer._maskName ) != nullptr;
            if ( bMaskValid == false )
                SW_LOG_ERROR( "Animation graph layer '%#' names unknown bone mask '%#'", layer._name.c_str(), layer._maskName.c_str() );
            bool bHalfLifeValid = true;
            if ( layerXML.findAttribute( "half_life" ) != nullptr )
            {
                const std::optional<float32> halfLife = layerXML.readRequiredAttribute<float32>( "half_life" );
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

    void AnimationGraphAsset::writeLayers( const XMLNode& root ) const
    {
        if ( _listBoneMask.empty() == false )
        {
            const XMLNode masksXML = root.appendChild( "bone_masks" );
            for ( const AnimationBoneMaskDef& mask : _listBoneMask )
            {
                const XMLNode maskXML = masksXML.appendChild( "bone_mask" );
                maskXML.appendAttribute( "name", mask._name.c_str() );
                if ( mask._filePath.empty() == false )
                    maskXML.appendAttribute( "file", string_view{ mask._filePath } );
                else
                    mask.writeObject( maskXML );
            }
        }
        if ( _listLayer.empty() )
            return;
        const XMLNode layersXML = root.appendChild( "layers" );
        for ( const AnimationGraphLayerDef& layer : _listLayer )
        {
            const XMLNode layerXML = layersXML.appendChild( "layer" );
            layerXML.appendAttribute( "name", layer._name.c_str() );
            layerXML.appendAttribute( "clip", layer._clipName.c_str() );
            if ( layer._maskName.empty() == false )
                layerXML.appendAttribute( "mask", layer._maskName.c_str() );
            layerXML.appendAttribute( "weight", layer._weight );
            layerXML.appendAttribute( "blend", getLayerBlendText( layer._blend ) );
            layerXML.appendAttribute( "loop", string_view{ layer._bLoop == SW_TRUE ? "true" : "false" } );
            if ( layer._weightParameter.empty() == false )
                layerXML.appendAttribute( "weight_param", layer._weightParameter.c_str() );
            if ( layer._halfLifeSeconds > 0.0f )
                layerXML.appendAttribute( "half_life", layer._halfLifeSeconds );
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

    string AnimationGraphAsset::toXML() const
    {
        XMLDocument   doc;
        const XMLNode root = doc.appendRoot( "AnimationGraphAsset" );

        const XMLNode nodesXML = root.appendChild( "nodes" );
        for ( const AnimationGraphNode& node : _listNode )
        {
            const XMLNode nodeXML = nodesXML.appendChild( "node" );
            nodeXML.appendAttribute( "id", node._id );
            nodeXML.appendAttribute( "name", string_view{ node._name } );
            nodeXML.appendAttribute( "x", node._position._x );
            nodeXML.appendAttribute( "y", node._position._y );
            if ( node._loopOverride >= 0 )
                nodeXML.appendAttribute( "loop", string_view{ node._loopOverride != 0 ? "true" : "false" } );
            if ( node._syncGroup.empty() == false )
                nodeXML.appendAttribute( "sync_group", node._syncGroup.c_str() );
            if ( node.hasBlendSpace() )
            {
                const AnimationGraphBlendSpaceDef& def           = node._blendSpace;
                const XMLNode                      blendSpaceXML = nodeXML.appendChild( "blend_space" );
                blendSpaceXML.appendAttribute( "x", def._parameterX.c_str() );
                if ( def._parameterY.empty() == false )
                    blendSpaceXML.appendAttribute( "y", def._parameterY.c_str() );
                // 기본값(all)은 적지 않는다(속성이 없으면 all 이다).
                if ( def._notifyMode != AnimationBlendSpaceNotifyMode::All )
                    blendSpaceXML.appendAttribute( "notify", AnimationGraphAssetInternal::kArrBlendSpaceNotifyText[static_cast<uint32>( def._notifyMode )] );
                const XMLNode samplesXML = blendSpaceXML.appendChild( "samples" );
                for ( const BlendSpaceSample& sample : def._space.getSamples() )
                {
                    const XMLNode sampleXML = samplesXML.appendChild( "sample" );
                    sampleXML.appendAttribute( "clip", sample._clipName.c_str() );
                    sampleXML.appendAttribute( "x", sample._position._x );
                    if ( def._space.getAxisCount() == 2 )
                        sampleXML.appendAttribute( "y", sample._position._y );
                }
            }
            if ( node.hasPoseNode() )
            {
                const AnimationGraphPoseNodeDef& def         = node._poseNode;
                const XMLNode                    poseNodeXML = nodeXML.appendChild( "pose_node" );
                poseNodeXML.appendAttribute( "type", AnimationGraphAssetInternal::kArrPoseNodeTypeText[static_cast<uint32>( def._type )] );
                if ( def._weightParameter.empty() == false )
                    poseNodeXML.appendAttribute( "weight", def._weightParameter.c_str() );
                const XMLNode inputsXML = poseNodeXML.appendChild( "inputs" );
                for ( const hashed_string& input : def._listInput )
                {
                    (void)inputsXML.appendChild( "item", string_view{ input.c_str() } );
                }
            }
        }

        const XMLNode linksXML = root.appendChild( "links" );
        for ( const AnimationGraphLink& link : _listLink )
        {
            const XMLNode linkXML = linksXML.appendChild( "link" );
            linkXML.appendAttribute( "id", link._id );
            linkXML.appendAttribute( "from", link._fromNode );
            linkXML.appendAttribute( "to", link._toNode );
            if ( link._blendSeconds >= 0.0f )
                linkXML.appendAttribute( "blend", link._blendSeconds );
            // 크로스페이드는 적지 않는다(속성이 없으면 크로스페이드다).
            if ( link._blendMode != AnimationBlendMode::Crossfade )
            {
                linkXML.appendAttribute( "blend_mode", getBlendModeText( link._blendMode ) );
                linkXML.appendAttribute( "half_life", link._halfLifeSeconds );
            }
            if ( link._op != AnimationConditionOp::None )
            {
                const XMLNode condition = linkXML.appendChild( "condition" );
                condition.appendAttribute( "param", link._parameter.c_str() );
                condition.appendAttribute( "op", getConditionOpText( link._op ) );
                condition.appendAttribute( "value", link._threshold );
            }
        }
        writeLayers( root );

        return doc.saveToString();
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

namespace sw
{
    SW_ASSET_TYPE( AnimationGraphAsset, AnimationGraphAsset::kAssetType );
} // namespace sw
