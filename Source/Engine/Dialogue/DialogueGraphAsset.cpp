#include "pch.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    namespace
    {
        /** @brief 특성 테이블이 타입 값 순서대로인지 확인합니다. `findNodeInfo` 가 값으로 바로 찾습니다. */
        constexpr bool isInfoTableOrdered()
        {
            for ( size_t index = 0; index < SW_COUNT_OF( kArrDialogueNodeInfo ); ++index )
            {
                if ( kArrDialogueNodeInfo[index]._type != static_cast<DialogueAssetNodeKind>( index ) )
                    return false;
            }
            return true;
        }

        static_assert( isInfoTableOrdered(), "kArrDialogueNodeInfo must be ordered by DialogueAssetNodeKind" );
    } // namespace
} // namespace sw

namespace sw
{
    bool DialogueGraphAsset::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        SW_MEMORY_SCOPE( Script );
        _listSpeaker.clear();
        _listNode.clear();
        _listLink.clear();
        if ( path.empty() )
            return false;

        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_WARNING_ID( "DialogueGraph.CouldNotReadDialogue", "Could not read dialogue graph '%#'", path );
            return false;
        }
        return readText( text, path, ResourceContext::forType( *StaticType() ).withAsset( assetID ) );
    }

    bool DialogueGraphAsset::loadFromDiskFile( string_view diskPath )
    {
        SW_MEMORY_SCOPE( Script );
        _listSpeaker.clear();
        _listNode.clear();
        _listLink.clear();
        if ( diskPath.empty() )
            return false;

        string text;
        if ( AssetIO::readTextFile( diskPath, text ) == false )
        {
            SW_LOG_WARNING_ID( "DialogueGraph.CouldNotReadDialogue2", "Could not read dialogue graph file '%#'", diskPath );
            return false;
        }
        return readText( text, diskPath, ResourceContext::forType( *StaticType() ) );
    }

    bool DialogueGraphAsset::saveToFile( const AbsolutePath& absPath ) const
    {
        if ( absPath.str().empty() )
            return false;
        return ResourceComposer::saveText( absPath.str(), toXML() );
    }

    bool DialogueGraphAsset::loadFromXMLText( string_view xmlView )
    {
        return readText( xmlView, "<dialogue graph text>", ResourceContext::forType( *StaticType() ) );
    }

    bool DialogueGraphAsset::readText( string_view xml, string_view sourceLabel, const ResourceContext& context )
    {
        _listSpeaker.clear();
        _listNode.clear();
        _listLink.clear();
        _listComment.clear();
        // 키는 대소문자까지 맞아야 하고, 모르는 속성 · 자식 · 모르는 노드 종류 · 읽지 못한 값 · 빠진 필수 칸은 오류다(`readComposedXMLDocument` 가 로그로 알린다).
        if ( ConfigManager::readComposedXMLDocument( this, *StaticType(), xml, sourceLabel, context ) && validateFile( sourceLabel ) )
            return true;
        _listSpeaker.clear();
        _listNode.clear();
        _listLink.clear();
        _listComment.clear();
        return false;
    }

    bool DialogueGraphAsset::validateFile( string_view sourceLabel ) const
    {
        bool bValid = true;
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            const int32 nodeID     = _listNode[nodeIndex]._id;
            bool        bDuplicate = false;
            for ( size_t previousIndex = 0; previousIndex < nodeIndex; ++previousIndex )
            {
                bDuplicate = bDuplicate || _listNode[previousIndex]._id == nodeID;
            }
            if ( nodeID > 0 && bDuplicate == false )
                continue;
            SW_LOG_ERROR_ID( "Dialogue.NodeIDInvalid", "Dialogue graph '%#': node %# needs a unique _id of 1 or more (has %#)", sourceLabel, nodeIndex, nodeID );
            bValid = false;
        }
        for ( size_t linkIndex = 0; linkIndex < _listLink.size(); ++linkIndex )
        {
            const DialogueAssetLink& link       = _listLink[linkIndex];
            bool                     bDuplicate = false;
            for ( size_t previousIndex = 0; previousIndex < linkIndex; ++previousIndex )
            {
                bDuplicate = bDuplicate || _listLink[previousIndex]._id == link._id;
            }
            const bool bValidLink = link._id > 0 && bDuplicate == false && link._fromPin > 0 && link._toPin > 0;
            if ( bValidLink )
                continue;
            SW_LOG_ERROR_ID( "Dialogue.LinkInvalid", "Dialogue graph '%#': link %# needs a unique _id of 1 or more and two pins (id %#, %# -> %#)", sourceLabel, linkIndex, link._id,
                             link._fromPin, link._toPin );
            bValid = false;
        }
        for ( size_t speakerIndex = 0; speakerIndex < _listSpeaker.size(); ++speakerIndex )
        {
            const uint32 speakerID  = _listSpeaker[speakerIndex]._id;
            bool         bDuplicate = false;
            for ( size_t previousIndex = 0; previousIndex < speakerIndex; ++previousIndex )
            {
                bDuplicate = bDuplicate || _listSpeaker[previousIndex]._id == speakerID;
            }
            if ( speakerID != kDialogueNoSpeaker && bDuplicate == false )
                continue;
            SW_LOG_ERROR_ID( "Dialogue.SpeakerIDInvalid", "Dialogue graph '%#': speaker %# needs a unique _id of 1 or more (has %#)", sourceLabel, speakerIndex, speakerID );
            bValid = false;
        }
        // 화자는 id 로만 가리킨다 — 목록에 없는 id 는 이름으로 찾아 메우지 않고 읽기 오류다.
        for ( const DialogueAssetNode& node : _listNode )
        {
            if ( node._speaker == kDialogueNoSpeaker || findSpeaker( node._speaker ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "Dialogue.SpeakerMissing", "Dialogue graph '%#': node %# refers to speaker %# which is not in _listSpeaker", sourceLabel, node._id, node._speaker );
            bValid = false;
        }
        return bValid;
    }

    const DialogueSpeaker* DialogueGraphAsset::findSpeaker( uint32 speakerID ) const
    {
        if ( speakerID == kDialogueNoSpeaker )
            return nullptr;
        return VectorUtil::findBy<&DialogueSpeaker::_id>( _listSpeaker, speakerID );
    }

    string DialogueGraphAsset::resolveSpeakerName( uint32 speakerID ) const
    {
        const DialogueSpeaker* pSpeaker = findSpeaker( speakerID );
        return pSpeaker != nullptr ? resolveLocalizedText( pSpeaker->_name ) : string{};
    }

    uint32 DialogueGraphAsset::makeNextSpeakerID() const
    {
        uint32 largestID = kDialogueNoSpeaker;
        for ( const DialogueSpeaker& speaker : _listSpeaker )
        {
            largestID = speaker._id > largestID ? speaker._id : largestID;
        }
        return largestID + 1;
    }

    string DialogueGraphAsset::toXML() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
    }

    const DialogueAssetNode* DialogueGraphAsset::findStartNode() const
    {
        for ( const DialogueAssetNode& node : _listNode )
        {
            if ( node._type == DialogueAssetNodeKind::Start )
                return &node;
        }
        if ( _listNode.empty() )
            return nullptr;
        return &_listNode.front();
    }

    const DialogueAssetNode* DialogueGraphAsset::findNode( int32 nodeID ) const
    {
        return VectorUtil::findBy<&DialogueAssetNode::_id>( _listNode, nodeID );
    }

    int32 DialogueGraphAsset::encodePin( int32 nodeID, int32 pinOffset )
    {
        // 오프셋이 자릿수를 넘으면 노드 id 를 오염시켜, 링크가 **다른 노드**를 가리키게 된다.
        // 그런 값을 조용히 만들지 않고 0(없는 핀)을 반환한다.
        const bool bInvalidPinID = nodeID <= 0 || pinOffset <= 0 || pinOffset >= kPinScale;
        if ( bInvalidPinID )
            return 0;
        return nodeID * kPinScale + pinOffset;
    }

    int32 DialogueGraphAsset::encodeChoicePin( int32 nodeID, int32 choiceIndex )
    {
        if ( choiceIndex < 0 || choiceIndex >= getMaxChoiceCount() )
            return 0;
        return encodePin( nodeID, kPinOffsetChoiceBase + choiceIndex );
    }

    int32 DialogueGraphAsset::decodePinNodeID( int32 pin )
    {
        if ( pin < kPinScale )
            return 0; // 노드 id 는 1 부터라 핀 값은 kPinScale 이상이다
        return pin / kPinScale;
    }

    int32 DialogueGraphAsset::decodePinOffset( int32 pin )
    {
        if ( pin < kPinScale )
            return 0;
        return pin % kPinScale;
    }

    optional<int32> DialogueGraphAsset::findLinkedNodeID( int32 fromNodeID, int32 pinOffset ) const
    {
        for ( const DialogueAssetLink& link : _listLink )
        {
            if ( decodePinNodeID( link._fromPin ) != fromNodeID )
                continue;
            if ( decodePinOffset( link._fromPin ) != pinOffset )
                continue;
            const int32 toNodeID = decodePinNodeID( link._toPin );
            return toNodeID > 0 ? optional<int32>( toNodeID ) : nullopt;
        }
        return nullopt;
    }

    optional<int32> DialogueGraphAsset::findDefaultNextNodeID( int32 fromNodeID ) const
    {
        return findLinkedNodeID( fromNodeID, kPinOffsetOut );
    }

    optional<int32> DialogueGraphAsset::findChoiceNextNodeID( int32 fromNodeID, int32 choiceIndex ) const
    {
        if ( 0 <= choiceIndex && choiceIndex < getMaxChoiceCount() )
        {
            const optional<int32> nextID = findLinkedNodeID( fromNodeID, kPinOffsetChoiceBase + choiceIndex );
            if ( nextID.has_value() )
                return nextID;
        }
        return findDefaultNextNodeID( fromNodeID );
    }

    optional<int32> DialogueGraphAsset::findBranchNextNodeID( int32 fromNodeID, bool bTrue ) const
    {
        return findLinkedNodeID( fromNodeID, bTrue ? kPinOffsetTrue : kPinOffsetFalse );
    }

    const utf8* DialogueGraphAsset::nodeTypeName( DialogueAssetNodeKind type )
    {
        const DialogueNodeInfo* pInfo = findNodeInfo( type );
        return pInfo != nullptr ? pInfo->_pName : "Unknown";
    }

    const DialogueNodeInfo* DialogueGraphAsset::findNodeInfo( DialogueAssetNodeKind type )
    {
        const size_t index = static_cast<size_t>( type );
        if ( index >= SW_COUNT_OF( kArrDialogueNodeInfo ) )
            return nullptr;
        return &kArrDialogueNodeInfo[index];
    }

    string DialogueGraphAsset::resolveLocalizedText( string_view textOrKey )
    {
        if ( textOrKey.empty() )
            return {};
        if ( engine::areEngineServicesBound() == false )
            return string{ textOrKey };

        // **키가 아닐 수도 있는 텍스트로 조회한다.** `hashed_string` 을 만들어 조회하면 그 대사 원문이
        // intern 아레나에 영구히 남아, 대화가 늘수록 메모리가 누수된다. 테이블은 해시로만
        // 조회되므로 intern 없이 조회할 수 있다.
        const utf8* pResolved = engine::getSubsystem<LocalizationManager>().findStringByText( textOrKey );
        if ( StringUtil::isNullOrEmpty( pResolved ) )
            return string{ textOrKey };
        return string{ pResolved };
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( DialogueGraphAsset, DialogueGraphAsset::kAssetType );
} // namespace sw
