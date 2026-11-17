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

        struct DialogueGraphAssetInternal
        {
            /** @brief 노드 @p node 가 오프셋 @p pinOffset 의 출력 핀을 가지는지입니다(종류 표의 출력 형태와 선택지 수가 정한다). */
            static bool hasOutputPin( const DialogueAssetNode& node, int32 pinOffset )
            {
                const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( node._type );
                if ( pInfo == nullptr || pInfo->_output == DialogueNodeOutput::None )
                    return false;
                if ( pinOffset == DialogueGraphAsset::kPinOffsetOut )
                    return true;
                if ( pInfo->_output == DialogueNodeOutput::Branch )
                    return pinOffset == DialogueGraphAsset::kPinOffsetTrue || pinOffset == DialogueGraphAsset::kPinOffsetFalse;
                const int32 choiceIndex = pinOffset - DialogueGraphAsset::kPinOffsetChoiceBase;
                return pInfo->_output == DialogueNodeOutput::Choice && choiceIndex >= 0 && choiceIndex < static_cast<int32>( node._listChoice.size() );
            }

            /** @brief 입력을 기다리지 않고 지나가는 노드(시작 · 동작 · 분기)인지입니다. 이런 노드끼리 도는 고리는 러너가 한 프레임 안에서 끝없이 돈다. */
            static bool isImmediateNode( const DialogueAssetNode& node )
            {
                const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( node._type );
                return pInfo != nullptr && ( pInfo->_flow == DialogueNodeFlow::PassThrough || pInfo->_flow == DialogueNodeFlow::Condition );
            }

            /**
             * @brief 기다림 없는 노드만으로 된 고리가 있으면 그 고리의 한 노드 id 를 돌려줍니다. 없으면 0 입니다.
             * @details 깊이 우선 탐색의 세 색(안 봄 · 경로 위 · 끝남)이다. 경로 위 노드로 돌아오는 간선이 고리다.
             */
            static int32 findInfiniteLoop( const DialogueGraphAsset& asset )
            {
                enum class Mark : uint8
                {
                    Unvisited,
                    OnPath,
                    Done,
                };
                struct Frame
                {
                    size_t _nodeIndex;
                    size_t _nextLinkIndex;
                };
                const size_t  nodeCount = asset._listNode.size();
                vector<Mark>  listMark( nodeCount, Mark::Unvisited );
                vector<Frame> listStack;
                for ( size_t rootIndex = 0; rootIndex < nodeCount; ++rootIndex )
                {
                    if ( listMark[rootIndex] != Mark::Unvisited || isImmediateNode( asset._listNode[rootIndex] ) == false )
                        continue;
                    listMark[rootIndex] = Mark::OnPath;
                    listStack.push_back( { rootIndex, 0 } );
                    while ( listStack.empty() == false )
                    {
                        Frame&      top     = listStack.back();
                        const int32 fromID  = asset._listNode[top._nodeIndex]._id;
                        bool        bPushed = false;
                        while ( top._nextLinkIndex < asset._listLink.size() && bPushed == false )
                        {
                            const DialogueAssetLink& link = asset._listLink[top._nextLinkIndex++];
                            if ( DialogueGraphAsset::decodePinNodeID( link._fromPin ) != fromID )
                                continue;
                            const DialogueAssetNode* pTo = asset.findNode( DialogueGraphAsset::decodePinNodeID( link._toPin ) );
                            if ( pTo == nullptr || isImmediateNode( *pTo ) == false )
                                continue;
                            const size_t toIndex = static_cast<size_t>( pTo - asset._listNode.data() );
                            if ( listMark[toIndex] == Mark::OnPath )
                                return pTo->_id;
                            if ( listMark[toIndex] == Mark::Done )
                                continue;
                            listMark[toIndex] = Mark::OnPath;
                            listStack.push_back( { toIndex, 0 } );
                            bPushed = true;
                        }
                        if ( bPushed )
                            continue;
                        listMark[listStack.back()._nodeIndex] = Mark::Done;
                        listStack.pop_back();
                    }
                }
                return 0;
            }
        };
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
        // 시작은 정확히 하나다 — 없으면 첫 노드로 대신하던 폴백이 엉뚱한 줄에서 대화를 열었다(감사 S-12, Yarn Spinner 는 컴파일 오류).
        int32 startCount = 0;
        for ( const DialogueAssetNode& node : _listNode )
        {
            startCount += node._type == DialogueAssetNodeKind::Start ? 1 : 0;
        }
        if ( _listNode.empty() == false && startCount != 1 )
        {
            SW_LOG_ERROR_ID( "Dialogue.StartNodeCount", "Dialogue graph '%#': needs exactly one Start node (has %#)", sourceLabel, startCount );
            bValid = false;
        }
        // 링크 끝점은 있는 노드의 있는 핀이어야 한다 — 끊긴 링크는 러너가 조용히 기본 출력으로 떨어지거나 대화를 끝냈다(감사 S-12, Yarn Spinner 는 컴파일 오류).
        // 한 출력 핀에는 링크 하나다 — 둘이면 `findLinkedNodeID` 가 파일 순서의 첫 링크만 따른다.
        for ( size_t linkIndex = 0; linkIndex < _listLink.size(); ++linkIndex )
        {
            const DialogueAssetLink& link       = _listLink[linkIndex];
            const DialogueAssetNode* pFrom      = findNode( decodePinNodeID( link._fromPin ) );
            const DialogueAssetNode* pTo        = findNode( decodePinNodeID( link._toPin ) );
            const DialogueNodeInfo*  pToInfo    = pTo != nullptr ? findNodeInfo( pTo->_type ) : nullptr;
            const bool               bFromPin   = pFrom != nullptr && DialogueGraphAssetInternal::hasOutputPin( *pFrom, decodePinOffset( link._fromPin ) );
            const bool               bToPin     = pToInfo != nullptr && pToInfo->_bHasInputPin && decodePinOffset( link._toPin ) == kPinOffsetIn;
            bool                     bSharedPin = false;
            for ( size_t previousIndex = 0; previousIndex < linkIndex; ++previousIndex )
            {
                bSharedPin = bSharedPin || _listLink[previousIndex]._fromPin == link._fromPin;
            }
            if ( bFromPin && bToPin && bSharedPin == false )
                continue;
            SW_LOG_ERROR_ID( "Dialogue.LinkDangling", "Dialogue graph '%#': link %# (%# -> %#) needs an existing output pin used once and an existing node's input pin", sourceLabel, link._id,
                             link._fromPin, link._toPin );
            bValid = false;
        }
        // 기다림(대사 · 선택 · 끝) 없는 고리는 러너가 한 프레임 안에서 끝없이 돈다.
        const int32 cycleNodeID = DialogueGraphAssetInternal::findInfiniteLoop( *this );
        if ( cycleNodeID != 0 )
        {
            SW_LOG_ERROR_ID( "Dialogue.InfiniteLoop", "Dialogue graph '%#': node %# is on a loop of Start, Action and Branch nodes only - add a Dialogue or Choice node to the loop", sourceLabel,
                             cycleNodeID );
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
        return nullptr; // 읽기 검증이 Start 하나를 보장한다 — 편집 중인 그래프는 없을 수 있다
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
