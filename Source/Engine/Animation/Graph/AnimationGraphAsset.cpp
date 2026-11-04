#include "pch.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Graph/AnimationGraphFields.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct AnimationGraphAssetInternal
        {
            static constexpr const utf8* kArrNodeChild[]       = { "blend_space", "pose_node" };
            static constexpr const utf8* kArrBlendSpaceChild[] = { "samples" };
            static constexpr const utf8* kArrPoseNodeChild[]   = { "inputs" };
            static constexpr const utf8* kArrLinkChild[]       = { "condition" };
            static constexpr const utf8* kArrBoneMaskChild[]   = { "roots", "bones" };

            /** @brief 원소 하나를 스키마 @p TFields 로 읽고, 자식 원소가 @p arrChild 안의 이름뿐인지 봅니다. 칸 하나라도 틀리면 false 입니다(오류는 로그에). */
            template <typename TFields, size_t ChildCount>
            [[nodiscard]] static bool readElement( const XMLNode& element, TFields& outFields, string_view sourceLabel, const utf8* const ( &arrChild )[ChildCount],
                                                   std::initializer_list<string_view> listHandRead = {} )
            {
                const bool bFields   = XMLFields::readFields( element, outFields, sourceLabel, listHandRead );
                const bool bChildren = XMLNameCheck::reportUnknownChildren( element, arrChild, sourceLabel );
                return bFields && bChildren;
            }

            /** @brief 자식이 없는 원소 하나를 스키마 @p TFields 로 읽습니다. */
            template <typename TFields>
            [[nodiscard]] static bool readLeafElement( const XMLNode& element, TFields& outFields, string_view sourceLabel )
            {
                const bool bFields   = XMLFields::readFields( element, outFields, sourceLabel );
                const bool bChildren = XMLNameCheck::reportUnknownChildren( element, nullptr, 0, sourceLabel );
                return bFields && bChildren;
            }

            /** @brief @p parent 아래의 원소가 모두 @p pTag 인지 봅니다. 아니면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool isTag( const XMLNode& element, const utf8* pTag, const utf8* pParentTag, string_view sourceLabel )
            {
                if ( string_view{ element.getName() } == pTag )
                    return true;
                SW_LOG_ERROR_ID( "AnimationGraphAsset.UnexpectedElement", "Animation graph '%#': <%#> holds only <%#> elements (found <%#>)", sourceLabel, pParentTag, pTag, element.getName() );
                return false;
            }
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

    bool AnimationGraphAsset::loadFromFile( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        SW_MEMORY_SCOPE( Animation );
        clear();
        if ( path.empty() )
            return false;

        XMLDocument doc;
        if ( doc.loadResource( assetID ) == false )
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

    bool AnimationGraphAsset::saveToFile( const AbsolutePath& absPath ) const
    {
        if ( absPath.str().empty() )
            return false;
        return FileUtil::ensureParentDirectoryExists( absPath.str() ) && FileUtil::writeTextFile( absPath.str(), toXML() );
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
        using Internal = AnimationGraphAssetInternal;
        AnimationGraphBlendSpaceFields fields;
        if ( Internal::readElement( blendSpaceXML, fields, inoutNode._name, Internal::kArrBlendSpaceChild ) == false )
            return false;
        AnimationGraphBlendSpaceDef& def = inoutNode._blendSpace;
        def._parameterX                  = fields._x;
        def._parameterY                  = fields._y;
        def._notifyMode                  = fields._notify;
        def._space.clear();
        def._space.setAxisCount( def._parameterY.empty() ? 1u : 2u );
        if ( def._parameterX.empty() )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeBlend", "Animation graph node '%#' has a blend space without an 'x' parameter", inoutNode._name.c_str() );
            return false;
        }

        bool bValid = true;
        for ( XMLNode sampleXML = blendSpaceXML.findChild( "samples" ).findChild(); sampleXML.isValid(); sampleXML = sampleXML.findNextSibling() )
        {
            AnimationGraphSampleFields sample;
            if ( Internal::isTag( sampleXML, "sample", "samples", inoutNode._name ) == false || Internal::readLeafElement( sampleXML, sample, inoutNode._name ) == false )
            {
                bValid = false;
                continue;
            }
            if ( sample._clip.empty() )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeBlend4", "Animation graph node '%#' blend space has a sample with an empty 'clip'", inoutNode._name.c_str() );
                bValid = false;
                continue;
            }
            def._space.addSample( float2{ sample._x, sample._y }, sample._clip );
        }
        if ( bValid && def._space.getSampleCount() == 0 )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeBlend6", "Animation graph node '%#' has a blend space without samples", inoutNode._name.c_str() );
            bValid = false;
        }
        return bValid;
    }

    bool AnimationGraphAsset::parsePoseNode( const XMLNode& poseNodeXML, AnimationGraphNode& inoutNode )
    {
        using Internal = AnimationGraphAssetInternal;
        AnimationGraphPoseNodeFields fields;
        if ( Internal::readElement( poseNodeXML, fields, inoutNode._name, Internal::kArrPoseNodeChild ) == false )
            return false;
        AnimationGraphPoseNodeDef& def = inoutNode._poseNode;
        def                            = AnimationGraphPoseNodeDef{};
        def._type                      = fields._type;
        def._weightParameter           = fields._weight;
        if ( def._type == AnimationPoseNodeType::None )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeUnknown", "Animation graph node '%#' has a pose_node of type None", inoutNode._name.c_str() );
            return false;
        }
        for ( XMLNode input = poseNodeXML.findChild( "inputs" ).findChild(); input.isValid(); input = input.findNextSibling() )
        {
            def._listInput.push_back( hashed_string( string_view{ input.getText() } ) );
        }

        // 종류마다 요구하는 값 — 지금은 BlendPoses 하나다.
        const bool bHasEmptyInput     = VectorUtil::contains( def._listInput, hashed_string{} );
        const bool bHasRequiredValues = def._listInput.size() == 2 && bHasEmptyInput == false && def._weightParameter.empty() == false;
        if ( bHasRequiredValues == false )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeBlend7", "Animation graph node '%#' BlendPoses needs two named 'inputs' and a 'weight' parameter", inoutNode._name.c_str() );
            return false;
        }
        return true;
    }

    bool AnimationGraphAsset::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphExpectedAnimationGraphAsset", "Animation graph '%#': expected an <AnimationGraphAsset> root element", sourceLabel );
            return false;
        }
        bool bValid = XMLReadUtil::hasOnlyKnownNames( root, {}, { "nodes", "links", "bone_masks", "layers", "comments" }, sourceLabel );
        bValid      = parseNodes( root, sourceLabel ) && bValid;
        bValid      = parseLinks( root, sourceLabel ) && bValid;
        bValid      = parseComments( root, sourceLabel ) && bValid;
        if ( parseLayers( root ) == false )
            bValid = false;
        if ( bValid == false )
            clear();
        return bValid;
    }

    bool AnimationGraphAsset::parseNodes( const XMLNode& root, string_view sourceLabel )
    {
        using Internal = AnimationGraphAssetInternal;
        bool bValid    = true;
        for ( XMLNode nodeXML = root.findChild( "nodes" ).findChild(); nodeXML.isValid(); nodeXML = nodeXML.findNextSibling() )
        {
            // `loop` 는 적지 않음 · 참 · 거짓 셋이라 스키마 칸이 아니다 — 적었을 때만 클립 설정을 덮는다.
            AnimationGraphNodeFields fields;
            std::optional<bool>      loop;
            if ( Internal::isTag( nodeXML, "node", "nodes", sourceLabel ) == false ||
                 ( Internal::readElement( nodeXML, fields, sourceLabel, Internal::kArrNodeChild, { "loop" } ) && nodeXML.readOptionalAttribute( "loop", loop ) ) == false )
            {
                bValid = false;
                continue;
            }
            AnimationGraphNode node{};
            node._id        = fields._id;
            node._name      = fields._name;
            node._position  = float2{ fields._x, fields._y };
            node._syncGroup = fields._syncGroup;
            if ( loop.has_value() )
                node._loopOverride = *loop ? 1 : 0;
            const XMLNode blendSpaceXML = nodeXML.findChild( "blend_space" );
            if ( blendSpaceXML.isValid() && parseBlendSpace( blendSpaceXML, node ) == false )
                bValid = false;
            const XMLNode poseNodeXML = nodeXML.findChild( "pose_node" );
            if ( poseNodeXML.isValid() && parsePoseNode( poseNodeXML, node ) == false )
                bValid = false;
            if ( node.hasBlendSpace() && node.hasPoseNode() )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeBoth", "Animation graph node '%#' has both blend_space and pose_node", node._name.c_str() );
                bValid = false;
            }
            _listNode.push_back( std::move( node ) );
        }
        return bValid;
    }

    bool AnimationGraphAsset::parseLinks( const XMLNode& root, string_view sourceLabel )
    {
        using Internal = AnimationGraphAssetInternal;
        bool bValid    = true;
        for ( XMLNode linkXML = root.findChild( "links" ).findChild(); linkXML.isValid(); linkXML = linkXML.findNextSibling() )
        {
            AnimationGraphLinkFields fields;
            if ( Internal::isTag( linkXML, "link", "links", sourceLabel ) == false || Internal::readElement( linkXML, fields, sourceLabel, Internal::kArrLinkChild ) == false )
            {
                bValid = false;
                continue;
            }
            AnimationGraphLink link{};
            link._id              = fields._id;
            link._fromNode        = fields._from;
            link._toNode          = fields._to;
            link._blendSeconds    = fields._blend;
            link._blendMode       = fields._blendMode;
            link._halfLifeSeconds = fields._halfLife;
            if ( link._blendMode == AnimationBlendMode::Inertialization && link._halfLifeSeconds <= 0.0f )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphLinkUses", "Animation graph link %# uses Inertialization without a positive 'half_life'", link._id );
                bValid = false;
            }
            const XMLNode conditionXML = linkXML.findChild( "condition" );
            if ( conditionXML.isValid() )
            {
                AnimationGraphConditionFields condition;
                if ( Internal::readLeafElement( conditionXML, condition, sourceLabel ) == false )
                    bValid = false;
                if ( condition._op == AnimationConditionOp::None )
                {
                    SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphLinkUnknown2", "Animation graph link %# has a condition with op None (leave the condition out for 'when finished')", link._id );
                    bValid = false;
                }
                link._parameter = condition._param;
                link._op        = condition._op;
                link._threshold = condition._value;
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
        _listComment.clear();
    }

    bool AnimationGraphAsset::parseComments( const XMLNode& root, string_view sourceLabel )
    {
        using Internal = AnimationGraphAssetInternal;
        bool bValid    = true;
        for ( XMLNode commentXML = root.findChild( "comments" ).findChild(); commentXML.isValid(); commentXML = commentXML.findNextSibling() )
        {
            AnimationGraphCommentFields fields;
            if ( Internal::isTag( commentXML, "comment", "comments", sourceLabel ) == false || Internal::readLeafElement( commentXML, fields, sourceLabel ) == false )
            {
                bValid = false;
                continue;
            }
            if ( VectorUtil::containsBy<&GraphCommentBox::_id>( _listComment, fields._id ) )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.CommentNeedsID", "Animation graph '%#': comment id %# repeats", sourceLabel, fields._id );
                bValid = false;
                continue;
            }
            GraphCommentBox comment{};
            comment._id       = fields._id;
            comment._text     = fields._text;
            comment._position = float2{ fields._x, fields._y };
            comment._size     = float2{ fields._w, fields._h };
            _listComment.push_back( std::move( comment ) );
        }
        return bValid;
    }

    bool AnimationGraphAsset::parseLayers( const XMLNode& root )
    {
        const bool bMasksValid = parseBoneMasks( root );
        return parseLayerDefs( root ) && bMasksValid;
    }

    bool AnimationGraphAsset::parseBoneMasks( const XMLNode& root )
    {
        using Internal = AnimationGraphAssetInternal;
        bool bValid    = true;
        for ( XMLNode maskXML = root.findChild( "bone_masks" ).findChild(); maskXML.isValid(); maskXML = maskXML.findNextSibling() )
        {
            AnimationGraphBoneMaskFields fields;
            if ( Internal::isTag( maskXML, "bone_mask", "bone_masks", "Animation graph" ) == false ||
                 Internal::readElement( maskXML, fields, "Animation graph bone mask", Internal::kArrBoneMaskChild ) == false )
            {
                bValid = false;
                continue;
            }
            if ( fields._name.empty() || findBoneMask( fields._name ) != nullptr )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphRepeatsBone", "Animation graph bone mask '%#' has an empty or repeated 'name'", fields._name.c_str() );
                bValid = false;
                continue;
            }
            // 프리셋 파일("file")이나 바로 적은 본문(<roots>, <bones>) 중 하나다. 둘 다 적으면 어느 쪽이 기준인지 모르므로 오류다.
            const bool bFromFile = fields._file.empty() == false;
            const bool bInline   = maskXML.findChild( "roots" ).isValid() || maskXML.findChild( "bones" ).isValid();
            if ( bFromFile && bInline )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphBoneMask2", "Animation graph bone mask '%#' has both 'file' and inline bones", fields._name.c_str() );
                bValid = false;
                continue;
            }
            AnimationBoneMaskDef mask{};
            mask._name           = fields._name;
            const string context = string( "Animation graph bone mask '" ) + mask._name.c_str() + "'";
            const bool   bLoaded = bFromFile ? mask.loadFromFile( AssetID{ fields._file } ) : mask.parseObject( maskXML, context );
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
        using Internal = AnimationGraphAssetInternal;
        bool bValid    = true;
        for ( XMLNode layerXML = root.findChild( "layers" ).findChild(); layerXML.isValid(); layerXML = layerXML.findNextSibling() )
        {
            AnimationGraphLayerFields fields;
            if ( Internal::isTag( layerXML, "layer", "layers", "Animation graph" ) == false || Internal::readLeafElement( layerXML, fields, "Animation graph layer" ) == false )
            {
                bValid = false;
                continue;
            }
            if ( fields._name.empty() || fields._clip.empty() || findLayer( fields._name ) != nullptr )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphRepeatsLayer", "Animation graph layer '%#' needs a 'clip' and a 'name' no other layer has", fields._name.c_str() );
                bValid = false;
                continue;
            }
            if ( fields._mask.empty() == false && findBoneMask( fields._mask ) == nullptr )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphLayerNames", "Animation graph layer '%#' names unknown bone mask '%#'", fields._name.c_str(), fields._mask.c_str() );
                bValid = false;
                continue;
            }
            AnimationGraphLayerDef layer{};
            layer._name            = fields._name;
            layer._clipName        = fields._clip;
            layer._maskName        = fields._mask;
            layer._weightParameter = fields._weightParam;
            layer._weight          = fields._weight;
            layer._halfLifeSeconds = fields._halfLife;
            layer._blend           = fields._blend;
            layer._bLoop           = fields._bLoop ? SW_TRUE : SW_FALSE;
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
                AnimationGraphBoneMaskFields fields;
                fields._name          = mask._name;
                fields._file          = mask._filePath;
                const XMLNode maskXML = masksXML.appendChild( "bone_mask" );
                XMLFields::writeFields( maskXML, fields );
                if ( mask._filePath.empty() )
                    mask.writeObject( maskXML );
            }
        }
        if ( _listLayer.empty() )
            return;
        const XMLNode layersXML = root.appendChild( "layers" );
        for ( const AnimationGraphLayerDef& layer : _listLayer )
        {
            AnimationGraphLayerFields fields;
            fields._name        = layer._name;
            fields._clip        = layer._clipName;
            fields._mask        = layer._maskName;
            fields._weightParam = layer._weightParameter;
            fields._weight      = layer._weight;
            fields._halfLife    = layer._halfLifeSeconds;
            fields._blend       = layer._blend;
            fields._bLoop       = layer._bLoop == SW_TRUE;
            XMLFields::writeFields( layersXML.appendChild( "layer" ), fields );
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

    void AnimationGraphAsset::writeNode( const XMLNode& nodesXML, const AnimationGraphNode& node )
    {
        AnimationGraphNodeFields fields;
        fields._id            = node._id;
        fields._name          = node._name;
        fields._x             = node._position._x;
        fields._y             = node._position._y;
        fields._syncGroup     = node._syncGroup;
        const XMLNode nodeXML = nodesXML.appendChild( "node" );
        XMLFields::writeFields( nodeXML, fields );
        if ( node._loopOverride >= 0 )
            nodeXML.appendAttribute( "loop", node._loopOverride != 0 );
        if ( node.hasBlendSpace() )
        {
            const AnimationGraphBlendSpaceDef& def = node._blendSpace;
            AnimationGraphBlendSpaceFields     blendSpace;
            blendSpace._x               = def._parameterX;
            blendSpace._y               = def._parameterY;
            blendSpace._notify          = def._notifyMode;
            const XMLNode blendSpaceXML = nodeXML.appendChild( "blend_space" );
            XMLFields::writeFields( blendSpaceXML, blendSpace );
            const XMLNode samplesXML = blendSpaceXML.appendChild( "samples" );
            for ( const BlendSpaceSample& sample : def._space.getSamples() )
            {
                AnimationGraphSampleFields sampleFields;
                sampleFields._clip = sample._clipName;
                sampleFields._x    = sample._position._x;
                sampleFields._y    = sample._position._y;
                XMLFields::writeFields( samplesXML.appendChild( "sample" ), sampleFields );
            }
        }
        if ( node.hasPoseNode() )
        {
            const AnimationGraphPoseNodeDef& def = node._poseNode;
            AnimationGraphPoseNodeFields     poseNode;
            poseNode._type            = def._type;
            poseNode._weight          = def._weightParameter;
            const XMLNode poseNodeXML = nodeXML.appendChild( "pose_node" );
            XMLFields::writeFields( poseNodeXML, poseNode );
            const XMLNode inputsXML = poseNodeXML.appendChild( "inputs" );
            for ( const hashed_string& input : def._listInput )
            {
                (void)inputsXML.appendChild( "item", string_view{ input.c_str() } );
            }
        }
    }

    void AnimationGraphAsset::writeLink( const XMLNode& linksXML, const AnimationGraphLink& link )
    {
        AnimationGraphLinkFields fields;
        fields._id            = link._id;
        fields._from          = link._fromNode;
        fields._to            = link._toNode;
        fields._blend         = link._blendSeconds;
        fields._blendMode     = link._blendMode;
        fields._halfLife      = link._halfLifeSeconds;
        const XMLNode linkXML = linksXML.appendChild( "link" );
        XMLFields::writeFields( linkXML, fields );
        if ( link._op == AnimationConditionOp::None )
            return;
        AnimationGraphConditionFields condition;
        condition._param = link._parameter;
        condition._op    = link._op;
        condition._value = link._threshold;
        XMLFields::writeFields( linkXML.appendChild( "condition" ), condition );
    }

    string AnimationGraphAsset::toXML() const
    {
        XMLDocument   doc;
        const XMLNode root = doc.appendRoot( "AnimationGraphAsset" );

        const XMLNode nodesXML = root.appendChild( "nodes" );
        for ( const AnimationGraphNode& node : _listNode )
        {
            writeNode( nodesXML, node );
        }
        const XMLNode linksXML = root.appendChild( "links" );
        for ( const AnimationGraphLink& link : _listLink )
        {
            writeLink( linksXML, link );
        }
        writeLayers( root );

        if ( _listComment.empty() == false )
        {
            const XMLNode commentsXML = root.appendChild( "comments" );
            for ( const GraphCommentBox& comment : _listComment )
            {
                AnimationGraphCommentFields fields;
                fields._id   = comment._id;
                fields._text = comment._text;
                fields._x    = comment._position._x;
                fields._y    = comment._position._y;
                fields._w    = comment._size._x;
                fields._h    = comment._size._y;
                XMLFields::writeFields( commentsXML.appendChild( "comment" ), fields );
            }
        }

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
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( AnimationGraphAsset, AnimationGraphAsset::kAssetType );
} // namespace sw
