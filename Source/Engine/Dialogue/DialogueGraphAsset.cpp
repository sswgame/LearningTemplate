#include "pch.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetIO.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextGatherer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "DialogueGraph" );

    namespace
    {
        /** @brief 특성 테이블이 타입 값 순서대로인지 확인합니다. `findNodeInfo` 가 값으로 바로 찾습니다. */
        constexpr bool isInfoTableOrdered()
        {
            for ( size_t index = 0; index < SW_COUNT_OF( kArrDialogueNodeInfo ); ++index )
            {
                if ( kArrDialogueNodeInfo[index]._type != static_cast<DialogueAssetNodeType>( index ) )
                    return false;
            }
            return true;
        }

        static_assert( isInfoTableOrdered(), "kArrDialogueNodeInfo must be ordered by DialogueAssetNodeType" );
    } // namespace
} // namespace sw

namespace sw
{
    bool DialogueGraphAsset::loadFromFile( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        SW_MEMORY_SCOPE( Script );
        _listNode.clear();
        _listLink.clear();
        if ( path.empty() )
            return false;

        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_WARNING( "Could not read dialogue graph '%#'", path );
            return false;
        }
        return readText( text, path );
    }

    bool DialogueGraphAsset::loadFromDiskFile( string_view diskPath )
    {
        SW_MEMORY_SCOPE( Script );
        _listNode.clear();
        _listLink.clear();
        if ( diskPath.empty() )
            return false;

        string text;
        if ( AssetIO::readTextFile( diskPath, text ) == false )
        {
            SW_LOG_WARNING( "Could not read dialogue graph file '%#'", diskPath );
            return false;
        }
        return readText( text, diskPath );
    }

    bool DialogueGraphAsset::saveToFile( string_view path ) const
    {
        if ( path.empty() )
            return false;
        return FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFile( path, toXML() );
    }

    bool DialogueGraphAsset::parseXML( string_view xmlView )
    {
        return readText( xmlView, "<dialogue graph text>" );
    }

    bool DialogueGraphAsset::readText( string_view xml, string_view sourceLabel )
    {
        _listNode.clear();
        _listLink.clear();
        const string label{ sourceLabel };
        // 키는 대소문자까지 맞아야 하고, 모르는 속성 · 자식 · 모르는 노드 종류 · 읽지 못한 값 · 빠진 필수 칸은 오류다(`readConfigXMLElement` 가 로그로 알린다).
        if ( ConfigManager::readConfigXMLElement( this, *StaticType(), xml, label.c_str() ) && validateFile( sourceLabel ) )
            return true;
        _listNode.clear();
        _listLink.clear();
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
            SW_LOG_ERROR( "Dialogue graph '%#': node %# needs a unique _id of 1 or more (has %#)", sourceLabel, nodeIndex, nodeID );
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
            SW_LOG_ERROR( "Dialogue graph '%#': link %# needs a unique _id of 1 or more and two pins (id %#, %# -> %#)", sourceLabel, linkIndex, link._id,
                          link._fromPin, link._toPin );
            bValid = false;
        }
        return bValid;
    }

    string DialogueGraphAsset::toXML() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
    }

    const DialogueAssetNode* DialogueGraphAsset::findStartNode() const
    {
        for ( const DialogueAssetNode& node : _listNode )
        {
            if ( node._type == DialogueAssetNodeType::Start )
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

    int32 DialogueGraphAsset::findLinkedNodeID( int32 fromNodeID, int32 pinOffset ) const
    {
        for ( const DialogueAssetLink& link : _listLink )
        {
            if ( decodePinNodeID( link._fromPin ) != fromNodeID )
                continue;
            if ( decodePinOffset( link._fromPin ) != pinOffset )
                continue;
            return decodePinNodeID( link._toPin );
        }
        return 0;
    }

    int32 DialogueGraphAsset::findDefaultNextNodeID( int32 fromNodeID ) const
    {
        return findLinkedNodeID( fromNodeID, kPinOffsetOut );
    }

    int32 DialogueGraphAsset::findChoiceNextNodeID( int32 fromNodeID, int32 choiceIndex ) const
    {
        if ( 0 <= choiceIndex && choiceIndex < getMaxChoiceCount() )
        {
            const int32 nextID = findLinkedNodeID( fromNodeID, kPinOffsetChoiceBase + choiceIndex );
            if ( nextID > 0 )
                return nextID;
        }
        return findDefaultNextNodeID( fromNodeID );
    }

    int32 DialogueGraphAsset::findBranchNextNodeID( int32 fromNodeID, bool bTrue ) const
    {
        return findLinkedNodeID( fromNodeID, bTrue ? kPinOffsetTrue : kPinOffsetFalse );
    }

    const utf8* DialogueGraphAsset::nodeTypeName( DialogueAssetNodeType type )
    {
        const DialogueNodeInfo* pInfo = findNodeInfo( type );
        return pInfo != nullptr ? pInfo->_pName : "Unknown";
    }

    const DialogueNodeInfo* DialogueGraphAsset::findNodeInfo( DialogueAssetNodeType type )
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

    void DialogueGraphAsset::collectLocalizableText( TextGatherer& gatherer, string_view origin ) const
    {
        for ( const DialogueAssetNode& node : _listNode )
        {
            if ( node._speaker.empty() == false )
                gatherer.addTextOrKey( node._speaker, "Dialogue speaker name", origin );
            if ( node._text.empty() == false )
                gatherer.addTextOrKey( node._text, "Dialogue line", origin );
            for ( const string& choice : node._listChoice )
            {
                if ( choice.empty() == false )
                    gatherer.addTextOrKey( choice, "Dialogue choice", origin );
            }
        }
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( DialogueGraphAsset, DialogueGraphAsset::kAssetType );
} // namespace sw
