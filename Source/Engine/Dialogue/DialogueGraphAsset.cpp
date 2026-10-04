#include "pch.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextGatherer.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    namespace
    {
        struct DialogueGraphAssetInternal
        {
            /** @brief 특성 표가 타입 값 순서대로인지 봅니다. `findNodeInfo` 가 값으로 바로 찾습니다. */
            static constexpr bool isInfoTableOrdered()
            {
                for ( size_t index = 0; index < SW_COUNT_OF( kArrDialogueNodeInfo ); ++index )
                {
                    if ( kArrDialogueNodeInfo[index]._type != static_cast<DialogueAssetNodeType>( index ) )
                        return false;
                }
                return true;
            }
        };

        static_assert( DialogueGraphAssetInternal::isInfoTableOrdered(), "kArrDialogueNodeInfo must be ordered by DialogueAssetNodeType" );
    } // namespace
} // namespace sw

namespace sw
{
    bool DialogueGraphAsset::loadFromFile( string_view path )
    {
        SW_MEMORY_SCOPE( Script );
        _listNode.clear();
        _listLink.clear();
        if ( path.empty() )
            return false;

        JsonDocument doc;
        if ( doc.loadPath( path ) == false )
            return false;
        // 파싱한 문서를 그대로 읽는다(문자열로 덤프해 parseJson 에 다시 넘기지 않는다).
        parseRoot( doc.getRoot() );
        return true;
    }

    bool DialogueGraphAsset::saveToFile( string_view path ) const
    {
        if ( path.empty() )
            return false;
        FileUtil::ensureParentDirectoryExists( path );
        return FileUtil::writeTextFile( path, toJson() );
    }

    bool DialogueGraphAsset::parseJson( string_view jsonView )
    {
        _listNode.clear();
        _listLink.clear();

        JsonDocument doc;
        if ( doc.parse( jsonView ) == false )
            return false;

        parseRoot( doc.getRoot() );
        return true;
    }

    void DialogueGraphAsset::parseRoot( const JsonValue& root )
    {
        forEachObjectInArray( root, "nodes", [this]( const JsonValue& nodeJson, size_t /*nodeIndex*/ )
        {
            DialogueAssetNode node{};
            node._id            = static_cast<int32>( nodeJson.get( "id" ).asInt( 0 ) );
            node._type          = parseNodeType( nodeJson.get( "type" ).asString() );
            node._speaker       = nodeJson.get( "speaker" ).asString();
            node._text          = nodeJson.get( "text" ).asString();
            node._condition     = nodeJson.get( "condition" ).asString();
            node._actionCommand = nodeJson.get( "action" ).asString();
            node._position._x   = static_cast<float32>( nodeJson.get( "x" ).asFloat( 40.0 ) );
            node._position._y   = static_cast<float32>( nodeJson.get( "y" ).asFloat( 40.0 ) );

            // 선택지는 **문자열 배열**이라 객체만 거르는 위 도우미가 맞지 않는다. 여기서 그대로 읽는다.
            const JsonValue choicesVal = nodeJson.get( "choices" );
            if ( choicesVal.isArray() )
            {
                const size_t choiceCount = choicesVal.size();
                node._listChoice.reserve( choiceCount );
                for ( size_t choiceIndex = 0; choiceIndex < choiceCount; ++choiceIndex )
                    node._listChoice.push_back( choicesVal.at( choiceIndex ).asString() );
            }

            if ( node._id > 0 )
                _listNode.push_back( std::move( node ) );
        } );

        forEachObjectInArray( root, "links", [this]( const JsonValue& linkJson, size_t linkIndex )
        {
            DialogueAssetLink link{};
            // id 가 없으면 순번을 쓴다. 손으로 적은 파일이 id 를 빼먹는 일이 있다.
            link._id      = static_cast<int32>( linkJson.get( "id" ).asInt( static_cast<int32>( linkIndex + 1 ) ) );
            link._fromPin = static_cast<int32>( linkJson.get( "from" ).asInt( 0 ) );
            link._toPin   = static_cast<int32>( linkJson.get( "to" ).asInt( 0 ) );
            if ( link._fromPin > 0 && link._toPin > 0 )
                _listLink.push_back( link );
        } );
    }

    string DialogueGraphAsset::toJson() const
    {
        JsonDocument    doc;
        const JsonValue root = doc.makeObject();

        const JsonValue nodesVal = root.set( "nodes" );
        nodesVal.setArray();
        for ( const DialogueAssetNode& node : _listNode )
        {
            const JsonValue nodeJson = nodesVal.pushBack();
            nodeJson.setObject();
            nodeJson.set( "id" ).setInt( node._id );
            nodeJson.set( "type" ).setString( nodeTypeName( node._type ) );
            nodeJson.set( "speaker" ).setString( node._speaker );
            nodeJson.set( "text" ).setString( node._text );
            nodeJson.set( "condition" ).setString( node._condition );
            nodeJson.set( "action" ).setString( node._actionCommand );

            const JsonValue choicesVal = nodeJson.set( "choices" );
            choicesVal.setArray();
            for ( const string& choice : node._listChoice )
                choicesVal.pushBack().setString( choice );

            nodeJson.set( "x" ).setFloat( static_cast<float64>( node._position._x ) );
            nodeJson.set( "y" ).setFloat( static_cast<float64>( node._position._y ) );
        }

        const JsonValue linksVal = root.set( "links" );
        linksVal.setArray();
        for ( const DialogueAssetLink& link : _listLink )
        {
            const JsonValue linkJson = linksVal.pushBack();
            linkJson.setObject();
            linkJson.set( "id" ).setInt( link._id );
            linkJson.set( "from" ).setInt( link._fromPin );
            linkJson.set( "to" ).setInt( link._toPin );
        }

        return doc.dump( 2 );
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

    const DialogueAssetNode* DialogueGraphAsset::findNode( int32 nodeId ) const
    {
        for ( const DialogueAssetNode& node : _listNode )
        {
            if ( node._id == nodeId )
                return &node;
        }
        return nullptr;
    }

    int32 DialogueGraphAsset::encodePin( int32 nodeId, int32 pinOffset )
    {
        // 오프셋이 자릿수를 넘으면 노드 id 를 오염시킨다. 링크가 **다른 노드**를 가리키게 된다.
        // 조용히 그런 값을 만들지 않고 0(없는 핀)을 반환한다.
        if ( nodeId <= 0 || pinOffset <= 0 || pinOffset >= kPinScale )
            return 0;
        return nodeId * kPinScale + pinOffset;
    }

    int32 DialogueGraphAsset::encodeChoicePin( int32 nodeId, int32 choiceIndex )
    {
        if ( choiceIndex < 0 || choiceIndex >= getMaxChoiceCount() )
            return 0;
        return encodePin( nodeId, kPinOffsetChoiceBase + choiceIndex );
    }

    int32 DialogueGraphAsset::decodePinNodeId( int32 pin )
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

    int32 DialogueGraphAsset::findLinkedNodeId( int32 fromNodeId, int32 pinOffset ) const
    {
        for ( const DialogueAssetLink& link : _listLink )
        {
            if ( decodePinNodeId( link._fromPin ) != fromNodeId )
                continue;
            if ( decodePinOffset( link._fromPin ) != pinOffset )
                continue;
            return decodePinNodeId( link._toPin );
        }
        return 0;
    }

    int32 DialogueGraphAsset::findDefaultNextNodeId( int32 fromNodeId ) const
    {
        return findLinkedNodeId( fromNodeId, kPinOffsetOut );
    }

    int32 DialogueGraphAsset::findChoiceNextNodeId( int32 fromNodeId, int32 choiceIndex ) const
    {
        if ( 0 <= choiceIndex && choiceIndex < getMaxChoiceCount() )
        {
            const int32 nextId = findLinkedNodeId( fromNodeId, kPinOffsetChoiceBase + choiceIndex );
            if ( nextId > 0 )
                return nextId;
        }
        return findDefaultNextNodeId( fromNodeId );
    }

    int32 DialogueGraphAsset::findBranchNextNodeId( int32 fromNodeId, bool bTrue ) const
    {
        return findLinkedNodeId( fromNodeId, bTrue ? kPinOffsetTrue : kPinOffsetFalse );
    }

    const utf8* DialogueGraphAsset::nodeTypeName( DialogueAssetNodeType type )
    {
        const DialogueNodeInfo* pInfo = findNodeInfo( type );
        return pInfo != nullptr ? pInfo->_pName : "Unknown";
    }

    DialogueAssetNodeType DialogueGraphAsset::parseNodeType( string_view typeStr )
    {
        for ( const DialogueNodeInfo& info : kArrDialogueNodeInfo )
        {
            if ( typeStr == info._pName )
                return info._type;
        }
        return DialogueAssetNodeType::Dialogue;
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

        // **키가 아닐 수도 있는 텍스트로 물어본다.** `hashed_string` 을 만들어 물으면 그 대사 원문이
        // intern 아레나에 영구히 남는다(대화가 늘수록 함께 늘어나는 누수). 표는 해시로만
        // 열리므로 intern 없이 물어볼 수 있다.
        const utf8* pResolved = engine::getLocalizationManager().getStringByText( textOrKey, nullptr );
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
