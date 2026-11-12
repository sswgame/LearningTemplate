#include "pch.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Graph/AnimationGraphParams.h"
#include "Engine/Animation/Skeletal/AnimationPoseNode.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
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

            /** @brief 원소 하나를 스키마 @p TParams 로 읽고, 자식 원소가 @p arrChild 안의 이름뿐인지 봅니다. 칸 하나라도 틀리면 false 입니다(오류는 로그에). */
            template <typename TParams, size_t ChildCount>
            [[nodiscard]] static bool readElement( const XMLNode& element, TParams& outParams, string_view sourceLabel, const utf8* const ( &arrChild )[ChildCount],
                                                   std::initializer_list<string_view> listHandRead = {} )
            {
                const bool bProperties = XMLFields::readProperties( element, outParams, sourceLabel, listHandRead );
                const bool bChildren   = XMLNameCheck::reportUnknownChildren( element, arrChild, sourceLabel );
                return bProperties && bChildren;
            }

            /** @brief 자식이 없는 원소 하나를 스키마 @p TParams 로 읽습니다. */
            template <typename TParams>
            [[nodiscard]] static bool readLeafElement( const XMLNode& element, TParams& outParams, string_view sourceLabel )
            {
                const bool bProperties = XMLFields::readProperties( element, outParams, sourceLabel );
                const bool bChildren   = XMLNameCheck::reportUnknownChildren( element, nullptr, 0, sourceLabel );
                return bProperties && bChildren;
            }

            /** @brief @p parent 아래의 원소가 모두 @p pTag 인지 봅니다. 아니면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool isTag( const XMLNode& element, const utf8* pTag, const utf8* pParentTag, string_view sourceLabel )
            {
                if ( string_view{ element.getName() } == pTag )
                    return true;
                SW_LOG_ERROR_ID( "AnimationGraphAsset.UnexpectedElement", "Animation graph '%#': <%#> holds only <%#> elements (found <%#>)", sourceLabel, pParentTag, pTag, element.getName() );
                return false;
            }

            /** @brief 내용이 있는 자식 원소입니다. 빈 원소는 차이 파일이 물려받은 것을 비운 것이라 없는 것과 같습니다. */
            [[nodiscard]] static XMLNode findFilledChild( const XMLNode& parent, const utf8* pName )
            {
                const XMLNode child   = parent.findChild( pName );
                const bool    bFilled = child.isValid() && ( child.getFirstAttribute().isValid() || child.findChild().isValid() );
                return bFilled ? child : XMLNode{};
            }

            /** @brief 열쇠 있는 목록의 규칙 하나입니다. */
            [[nodiscard]] static DataInheritanceSchema makeKeyedList( const utf8* pName, const utf8* pKey )
            {
                DataInheritanceSchema list{ pName };
                list._keyAttribute = pKey;
                list._bEmptyClears = SW_FALSE; // 숫자 칸의 빈 글은 읽히지 않는다 — 기본값으로 되돌린 칸은 저장의 왕복 검사가 잡는다(`saveToFile`)
                return list;
            }

            /** @brief 상속 규칙입니다(`AnimationGraphAsset` 설명). */
            [[nodiscard]] static DataInheritanceSchema makeInheritanceSchema()
            {
                DataInheritanceSchema schema{ "AnimationGraphAsset" };
                schema._bEmptyClears = SW_FALSE;
                schema._listField.push_back( makeKeyedList( "nodes", "name" ) );
                schema._listField.push_back( makeKeyedList( "links", "id" ) );
                schema._listField.back()._listIdentityAttribute = { "from", "to" }; // 전이 id 는 숫자라 자식이 새로 더한 전이가 부모 id 와 우연히 겹친다 — 끝점이 다르면 오류다
                schema._listField.push_back( makeKeyedList( "bone_masks", "name" ) );
                schema._listField.push_back( makeKeyedList( "layers", "name" ) );
                schema._listField.push_back( makeKeyedList( "comments", "id" ) );
                return schema;
            }
        };
        SW_DATA_INHERITANCE_SCHEMA( AnimationGraphAsset, &AnimationGraphAssetInternal::makeInheritanceSchema ); // 팩 쿠커가 루트 이름으로 찾는다
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

    bool AnimationGraphAsset::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        SW_MEMORY_SCOPE( Animation );
        clear();
        if ( path.empty() )
            return false;

        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
            return false;
        return parseText( text, path, ResourceContext::forSchema( &AnimationGraphAssetInternal::makeInheritanceSchema ).withAsset( assetID ) );
    }

    bool AnimationGraphAsset::loadFromDiskFile( string_view diskPath )
    {
        SW_MEMORY_SCOPE( Animation );
        clear();
        if ( diskPath.empty() )
            return false;

        string text;
        if ( AssetIO::readTextFile( diskPath, text ) == false )
            return false;
        return parseText( text, diskPath, ResourceContext::forSchema( &AnimationGraphAssetInternal::makeInheritanceSchema ) );
    }

    bool AnimationGraphAsset::saveToFile( const AbsolutePath& absPath ) const
    {
        if ( absPath.str().empty() )
            return false;
        const string fullText = toXML();
        if ( _baseID.empty() )
            return ResourceComposer::saveText( absPath.str(), fullText );
        AnimationGraphAsset parent;
        if ( parent.loadFromAsset( AssetID{ _baseID } ) == false )
        {
            DataInheritance::reportBaseNotFound( _baseID, absPath.str() );
            return false;
        }
        return ResourceComposer::saveDelta( absPath.str(), fullText, parent.toXML(), AnimationGraphAssetInternal::makeInheritanceSchema(), _baseID, [&absPath]( const string& deltaText )
        {
            AnimationGraphAsset reread;
            return reread.parseText( deltaText, absPath.str(), ResourceContext::forSchema( &AnimationGraphAssetInternal::makeInheritanceSchema ) ) ? reread.toXML() : string{};
        } );
    }

    bool AnimationGraphAsset::loadFromXMLText( string_view xmlView )
    {
        clear();
        return parseText( xmlView, "<animation graph text>", ResourceContext::forSchema( &AnimationGraphAssetInternal::makeInheritanceSchema ) );
    }

    bool AnimationGraphAsset::parseText( string_view text, string_view sourceLabel, const ResourceContext& context )
    {
        clear();
        _baseID = DataInheritance::findBasePath( text );
        string flatText;
        bool   bComposed = false; // 펼치지 않으면 flatText 는 빈 채로 남는다
        if ( ResourceComposer::compose( text, sourceLabel, context, flatText, bComposed ) == false )
        {
            clear();
            return false;
        }
        XMLDocument doc;
        if ( doc.parse( bComposed ? string_view{ flatText } : text, sourceLabel ) == false )
        {
            clear();
            return false;
        }
        // 파싱한 문서를 그대로 읽는다(문자열로 덤프해 다시 넘기지 않는다).
        return parseRoot( doc.getRoot( "AnimationGraphAsset" ), sourceLabel );
    }

    bool AnimationGraphAsset::parseBlendSpace( const XMLNode& blendSpaceXML, AnimationGraphNode& inoutNode )
    {
        using Internal = AnimationGraphAssetInternal;
        AnimationGraphBlendSpaceParams params;
        if ( Internal::readElement( blendSpaceXML, params, inoutNode._name, Internal::kArrBlendSpaceChild ) == false )
            return false;
        AnimationGraphBlendSpaceDef& def = inoutNode._blendSpace;
        def._parameterX                  = params._x;
        def._parameterY                  = params._y;
        def._notifyMode                  = params._notify;
        def._space.clear();
        def._space.setAxisCount( def._parameterY.empty() ? 1u : 2u );
        def._space.setAxisRange( float2{ params._minX, params._minY }, float2{ params._maxX, params._maxY } );
        if ( def._parameterX.empty() )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeBlend", "Animation graph node '%#' has a blend space without an 'x' parameter", inoutNode._name.c_str() );
            return false;
        }
        if ( def._space.hasValidAxisRange() == false )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.BlendSpaceAxisRange", "Animation graph node '%#' blend space needs max_x > min_x%#", inoutNode._name.c_str(),
                             def._space.getAxisCount() == 2 ? " and max_y > min_y" : "" );
            return false;
        }

        bool bValid = true;
        for ( XMLNode sampleXML = blendSpaceXML.findChild( "samples" ).findChild(); sampleXML.isValid(); sampleXML = sampleXML.findNextSibling() )
        {
            AnimationGraphSampleParams sample;
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
        // 범위 밖 표본은 가둔 파라미터가 닿지 못하고, 겹친 표본은 늘 가중치 0 이다 — 둘 다 조용히 버리지 않고 로드 오류로 멈춘다.
        if ( bValid && def._space.hasSampleOutsideAxisRange() )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.BlendSpaceSampleOutsideRange", "Animation graph node '%#' blend space has a sample outside its axis range", inoutNode._name.c_str() );
            bValid = false;
        }
        if ( bValid && def._space.hasCoincidentSamples() )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.BlendSpaceCoincidentSamples", "Animation graph node '%#' blend space has two samples at the same position", inoutNode._name.c_str() );
            bValid = false;
        }
        // 2D 가중치는 삼각분할의 무게중심이라 넓이가 없는 표본(둘 · 한 줄)은 섞을 삼각형이 없다 — 대신할 규칙을 두지 않고 로드 오류로 멈춘다.
        if ( bValid && def._space.getAxisCount() == 2 && def._space.getSampleCount() >= 2 && def._space.hasArea() == false )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.BlendSpaceNoArea", "Animation graph node '%#' has a 2D blend space whose samples do not span an area (need three not on one line)", inoutNode._name.c_str() );
            bValid = false;
        }
        return bValid;
    }

    bool AnimationGraphAsset::parsePoseNode( const XMLNode& poseNodeXML, AnimationGraphNode& inoutNode )
    {
        using Internal = AnimationGraphAssetInternal;
        AnimationGraphPoseNodeParams params;
        if ( Internal::readElement( poseNodeXML, params, inoutNode._name, Internal::kArrPoseNodeChild ) == false )
            return false;
        AnimationGraphPoseNodeDef& def     = inoutNode._poseNode;
        def                                = AnimationGraphPoseNodeDef{};
        def._type                          = params._type;
        def._weightParameter               = params._weight;
        const AnimationPoseNodeType* pType = engine::getSubsystem<AnimationPoseNodeRegistry>().findType( def._type );
        if ( pType == nullptr )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeUnknown", "Animation graph node '%#' has an unknown pose_node type '%#'", inoutNode._name.c_str(), def._type.c_str() );
            return false;
        }
        for ( XMLNode input = poseNodeXML.findChild( "inputs" ).findChild(); input.isValid(); input = input.findNextSibling() )
        {
            def._listInput.push_back( hashed_string( string_view{ input.getText() } ) );
        }

        // 종류마다 요구하는 값은 그 종류의 계약이다(입력 수 · weight — 표가 정한다).
        if ( AnimationPoseNodeRegistry::isSatisfiedBy( *pType, def ) == false )
        {
            SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeBlend7", "Animation graph node '%#' pose_node '%#' needs %# named 'inputs'%#", inoutNode._name.c_str(), def._type.c_str(), pType->_inputCount,
                             pType->_bRequiresWeight ? " and a 'weight' parameter" : "" );
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
        bValid      = loadNodesFromXML( root, sourceLabel ) && bValid;
        bValid      = parseLinks( root, sourceLabel ) && bValid;
        bValid      = parseComments( root, sourceLabel ) && bValid;
        if ( parseLayers( root ) == false )
            bValid = false;
        bValid = validateGraphIDs( sourceLabel ) && bValid;
        if ( bValid == false )
            clear();
        return bValid;
    }

    bool AnimationGraphAsset::validateGraphIDs( string_view sourceLabel ) const
    {
        bool bValid = true;
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( findNode( _listNode[nodeIndex]._id ) == &_listNode[nodeIndex] )
                continue;
            SW_LOG_ERROR_ID( "AnimationGraphAsset.NodeIDRepeats", "Animation graph '%#': node '%#' repeats id %#", sourceLabel, _listNode[nodeIndex]._name.c_str(), _listNode[nodeIndex]._id );
            bValid = false;
        }
        for ( const AnimationGraphLink& link : _listLink )
        {
            if ( VectorUtil::findBy<&AnimationGraphLink::_id>( _listLink, link._id ) != &link )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.LinkIDRepeats", "Animation graph '%#': link id %# repeats", sourceLabel, link._id );
                bValid = false;
            }
            if ( findNode( link._fromNode ) == nullptr || findNode( link._toNode ) == nullptr )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.LinkNodeMissing", "Animation graph '%#': link %# joins nodes %# and %#, which the graph does not both have", sourceLabel, link._id, link._fromNode, link._toNode );
                bValid = false;
            }
        }
        return bValid;
    }

    bool AnimationGraphAsset::loadNodesFromXML( const XMLNode& root, string_view sourceLabel )
    {
        using Internal = AnimationGraphAssetInternal;
        bool bValid    = true;
        for ( XMLNode nodeXML = root.findChild( "nodes" ).findChild(); nodeXML.isValid(); nodeXML = nodeXML.findNextSibling() )
        {
            // `loop` 는 적지 않음 · 참 · 거짓 셋이라 스키마 칸이 아니다 — 적었을 때만 클립 설정을 덮는다.
            AnimationGraphNodeParams params;
            optional<bool>           loop;
            if ( Internal::isTag( nodeXML, "node", "nodes", sourceLabel ) == false ||
                 ( Internal::readElement( nodeXML, params, sourceLabel, Internal::kArrNodeChild, { "loop" } ) && ( nodeXML.getAttributeText( "loop" ).empty() || nodeXML.readOptionalAttribute( "loop", loop ) ) ) == false )
            {
                bValid = false;
                continue;
            }
            AnimationGraphNode node{};
            node._id                    = params._id;
            node._name                  = params._name;
            node._position              = float2{ params._x, params._y };
            node._syncGroup             = params._syncGroup;
            node._maxLODGrade           = params._maxLod;
            node._loop                  = loop;
            const XMLNode blendSpaceXML = Internal::findFilledChild( nodeXML, "blend_space" );
            if ( blendSpaceXML.isValid() && parseBlendSpace( blendSpaceXML, node ) == false )
                bValid = false;
            const XMLNode poseNodeXML = Internal::findFilledChild( nodeXML, "pose_node" );
            if ( poseNodeXML.isValid() && parsePoseNode( poseNodeXML, node ) == false )
                bValid = false;
            if ( node.hasBlendSpace() && node.hasPoseNode() )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeBoth", "Animation graph node '%#' has both blend_space and pose_node", node._name.c_str() );
                bValid = false;
            }
            // 등급이 접는 것은 섞는 표본이다 — 클립 하나인 상태에는 접을 것이 없다(조용히 무시하지 않는다).
            if ( node._maxLODGrade >= 0 && node.hasBlendSpace() == false )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphNodeMaxLOD", "Animation graph '%#': node '%#' sets 'max_lod' but is not a blend space state", sourceLabel, node._name.c_str() );
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
            AnimationGraphLinkParams params;
            if ( Internal::isTag( linkXML, "link", "links", sourceLabel ) == false || Internal::readElement( linkXML, params, sourceLabel, Internal::kArrLinkChild ) == false )
            {
                bValid = false;
                continue;
            }
            AnimationGraphLink link{};
            link._id              = params._id;
            link._fromNode        = params._from;
            link._toNode          = params._to;
            link._blendSeconds    = params._blend;
            link._blendMode       = params._blendMode;
            link._halfLifeSeconds = params._halfLife;
            if ( link._blendMode == AnimationBlendMode::Inertialization && link._halfLifeSeconds <= 0.0f )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphLinkUses", "Animation graph link %# uses Inertialization without a positive 'half_life'", link._id );
                bValid = false;
            }
            const XMLNode conditionXML = Internal::findFilledChild( linkXML, "condition" );
            if ( conditionXML.isValid() )
            {
                AnimationGraphConditionParams condition;
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
        _baseID.clear();
    }

    bool AnimationGraphAsset::parseComments( const XMLNode& root, string_view sourceLabel )
    {
        using Internal = AnimationGraphAssetInternal;
        bool bValid    = true;
        for ( XMLNode commentXML = root.findChild( "comments" ).findChild(); commentXML.isValid(); commentXML = commentXML.findNextSibling() )
        {
            AnimationGraphCommentParams params;
            if ( Internal::isTag( commentXML, "comment", "comments", sourceLabel ) == false || Internal::readLeafElement( commentXML, params, sourceLabel ) == false )
            {
                bValid = false;
                continue;
            }
            if ( VectorUtil::containsBy<&GraphCommentBox::_id>( _listComment, params._id ) )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.CommentNeedsID", "Animation graph '%#': comment id %# repeats", sourceLabel, params._id );
                bValid = false;
                continue;
            }
            GraphCommentBox comment{};
            comment._id       = params._id;
            comment._text     = params._text;
            comment._position = float2{ params._x, params._y };
            comment._size     = float2{ params._w, params._h };
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
            AnimationGraphBoneMaskParams params;
            if ( Internal::isTag( maskXML, "bone_mask", "bone_masks", "Animation graph" ) == false ||
                 Internal::readElement( maskXML, params, "Animation graph bone mask", Internal::kArrBoneMaskChild ) == false )
            {
                bValid = false;
                continue;
            }
            if ( params._name.empty() || findBoneMask( params._name ) != nullptr )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphRepeatsBone", "Animation graph bone mask '%#' has an empty or repeated 'name'", params._name.c_str() );
                bValid = false;
                continue;
            }
            // 프리셋 파일("file")이나 바로 적은 본문(<roots>, <bones>) 중 하나다. 둘 다 적으면 어느 쪽이 기준인지 모르므로 오류다.
            const bool bFromFile = params._file.empty() == false;
            const bool bInline   = maskXML.findChild( "roots" ).isValid() || maskXML.findChild( "bones" ).isValid();
            if ( bFromFile && bInline )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphBoneMask2", "Animation graph bone mask '%#' has both 'file' and inline bones", params._name.c_str() );
                bValid = false;
                continue;
            }
            AnimationBoneMaskDef mask{};
            mask._name           = params._name;
            const string context = string( "Animation graph bone mask '" ) + mask._name.c_str() + "'";
            const bool   bLoaded = bFromFile ? mask.loadFromAsset( AssetID{ params._file } ) : mask.parseObject( maskXML, context );
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
            AnimationGraphLayerParams params;
            if ( Internal::isTag( layerXML, "layer", "layers", "Animation graph" ) == false || Internal::readLeafElement( layerXML, params, "Animation graph layer" ) == false )
            {
                bValid = false;
                continue;
            }
            if ( params._name.empty() || params._clip.empty() == params._slot.empty() || findLayer( params._name ) != nullptr )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphRepeatsLayer", "Animation graph layer '%#' needs exactly one of 'clip' and 'slot' (one of the other kind over a base layer needs _op=\"Replace\") and a 'name' no other layer has", params._name.c_str() );
                bValid = false;
                continue;
            }
            if ( params._mask.empty() == false && findBoneMask( params._mask ) == nullptr )
            {
                SW_LOG_ERROR_ID( "AnimationGraphAsset.AnimationGraphLayerNames", "Animation graph layer '%#' names unknown bone mask '%#'", params._name.c_str(), params._mask.c_str() );
                bValid = false;
                continue;
            }
            AnimationGraphLayerDef layer{};
            layer._name            = params._name;
            layer._clipName        = params._clip;
            layer._slotName        = params._slot;
            layer._maskName        = params._mask;
            layer._weightParameter = params._weightParam;
            layer._weight          = params._weight;
            layer._halfLifeSeconds = params._halfLife;
            layer._maxLODGrade     = params._maxLod;
            layer._blend           = params._blend;
            layer._bLoop           = params._bLoop ? SW_TRUE : SW_FALSE;
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
                AnimationGraphBoneMaskParams params;
                params._name          = mask._name;
                params._file          = mask._filePath;
                const XMLNode maskXML = masksXML.appendChild( "bone_mask" );
                XMLFields::writeProperties( maskXML, params );
                if ( mask._filePath.empty() )
                    mask.writeObject( maskXML );
            }
        }
        if ( _listLayer.empty() )
            return;
        const XMLNode layersXML = root.appendChild( "layers" );
        for ( const AnimationGraphLayerDef& layer : _listLayer )
        {
            AnimationGraphLayerParams params;
            params._name        = layer._name;
            params._clip        = layer._clipName;
            params._slot        = layer._slotName;
            params._mask        = layer._maskName;
            params._weightParam = layer._weightParameter;
            params._weight      = layer._weight;
            params._halfLife    = layer._halfLifeSeconds;
            params._maxLod      = layer._maxLODGrade;
            params._blend       = layer._blend;
            params._bLoop       = layer._bLoop == SW_TRUE;
            XMLFields::writeProperties( layersXML.appendChild( "layer" ), params );
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
        AnimationGraphNodeParams params;
        params._id            = node._id;
        params._name          = node._name;
        params._x             = node._position._x;
        params._y             = node._position._y;
        params._syncGroup     = node._syncGroup;
        params._maxLod        = node._maxLODGrade;
        const XMLNode nodeXML = nodesXML.appendChild( "node" );
        XMLFields::writeProperties( nodeXML, params );
        if ( node._loop.has_value() )
            nodeXML.appendAttribute( "loop", *node._loop );
        if ( node.hasBlendSpace() )
        {
            const AnimationGraphBlendSpaceDef& def = node._blendSpace;
            AnimationGraphBlendSpaceParams     blendSpace;
            blendSpace._x               = def._parameterX;
            blendSpace._y               = def._parameterY;
            blendSpace._notify          = def._notifyMode;
            blendSpace._minX            = def._space.getAxisMinimum()._x;
            blendSpace._maxX            = def._space.getAxisMaximum()._x;
            blendSpace._minY            = def._space.getAxisMinimum()._y;
            blendSpace._maxY            = def._space.getAxisMaximum()._y;
            const XMLNode blendSpaceXML = nodeXML.appendChild( "blend_space" );
            XMLFields::writeProperties( blendSpaceXML, blendSpace );
            const XMLNode samplesXML = blendSpaceXML.appendChild( "samples" );
            for ( const BlendSpaceSample& sample : def._space.getSamples() )
            {
                AnimationGraphSampleParams sampleProperties;
                sampleProperties._clip = sample._clipName;
                sampleProperties._x    = sample._position._x;
                sampleProperties._y    = sample._position._y;
                XMLFields::writeProperties( samplesXML.appendChild( "sample" ), sampleProperties );
            }
        }
        if ( node.hasPoseNode() )
        {
            const AnimationGraphPoseNodeDef& def = node._poseNode;
            AnimationGraphPoseNodeParams     poseNode;
            poseNode._type            = def._type;
            poseNode._weight          = def._weightParameter;
            const XMLNode poseNodeXML = nodeXML.appendChild( "pose_node" );
            XMLFields::writeProperties( poseNodeXML, poseNode );
            const XMLNode inputsXML = poseNodeXML.appendChild( "inputs" );
            for ( const hashed_string& input : def._listInput )
            {
                (void)inputsXML.appendChild( "item", string_view{ input.c_str() } );
            }
        }
    }

    void AnimationGraphAsset::writeLink( const XMLNode& linksXML, const AnimationGraphLink& link )
    {
        AnimationGraphLinkParams params;
        params._id            = link._id;
        params._from          = link._fromNode;
        params._to            = link._toNode;
        params._blend         = link._blendSeconds;
        params._blendMode     = link._blendMode;
        params._halfLife      = link._halfLifeSeconds;
        const XMLNode linkXML = linksXML.appendChild( "link" );
        XMLFields::writeProperties( linkXML, params );
        if ( link._op == AnimationConditionOp::None )
            return;
        AnimationGraphConditionParams condition;
        condition._param = link._parameter;
        condition._op    = link._op;
        condition._value = link._threshold;
        XMLFields::writeProperties( linkXML.appendChild( "condition" ), condition );
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
                AnimationGraphCommentParams params;
                params._id   = comment._id;
                params._text = comment._text;
                params._x    = comment._position._x;
                params._y    = comment._position._y;
                params._w    = comment._size._x;
                params._h    = comment._size._y;
                XMLFields::writeProperties( commentsXML.appendChild( "comment" ), params );
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
