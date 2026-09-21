#include "pch.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 노드 타입 ↔ 이름 표. **이름을 짓는 쪽과 읽는 쪽이 같은 표를 본다.**
         * @details 예전에는 `nodeTypeName` 의 switch 와 `parseNodeType` 의 if 사슬이 따로 있었다.
         *          열거자를 하나 더하고 한쪽만 고치면, 그 이름은 저장은 되는데 읽을 때 조용히
         *          `Dialogue` 로 떨어진다 — 파일은 멀쩡한데 대화만 달라진다.
         */
        struct DialogueGraphAssetInternal
        {
            /** @brief 타입 하나와 그 JSON 이름입니다. */
            struct NodeTypeName
            {
                DialogueAssetNodeType _type;  /**< 노드 타입입니다. */
                const utf8*           _pName; /**< JSON 에 적히는 이름입니다. */
            };

            /** @brief 저장·해석에 함께 쓰는 유일한 표입니다. */
            static constexpr NodeTypeName kArrNodeTypeName[] = {
                {   DialogueAssetNodeType::Start,    "Start"},
                {DialogueAssetNodeType::Dialogue, "Dialogue"},
                {  DialogueAssetNodeType::Choice,   "Choice"},
                {  DialogueAssetNodeType::Branch,   "Branch"},
                {  DialogueAssetNodeType::Action,   "Action"},
                {     DialogueAssetNodeType::End,      "End"},
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool DialogueGraphAsset::loadFromFile( string_view path )
    {
        _listNode.clear();
        _listLink.clear();
        if ( path.empty() )
            return false;

        JsonDocument doc;
        if ( doc.loadPath( path ) == false )
            return false;
        // 파싱한 문서를 다시 문자열로 덤프해 parseJson 에 넘기고 있었다 — 같은 JSON 을 두 번
        // 파싱하고 그 사이에 문서 전체 길이의 문자열을 한 번 더 만들던 자리다.
        parseRoot( doc.getRoot() );
        return true;
    }

    bool DialogueGraphAsset::saveToFile( string_view path ) const
    {
        if ( path.empty() )
            return false;
        const string dir = FileUtil::getDirectoryPart( path );
        if ( dir.empty() == false )
            FileUtil::ensureDirectoryExists( dir );
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

            // 선택지는 **문자열 배열**이라 객체만 거르는 위 헬퍼가 맞지 않는다 — 여기서 그대로 읽는다.
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
            // id 가 없으면 순번을 쓴다 — 손으로 적은 파일이 id 를 빼먹는 일이 있다.
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
        // 오프셋이 자릿수를 넘으면 노드 id 를 오염시킨다 — 링크가 **다른 노드**를 가리키게 된다.
        // 조용히 그런 값을 만들지 않고 0(없는 핀)을 돌려준다.
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
        if ( pin <= 0 )
            return 0;
        const int32 scale = ( pin >= kPinScale ) ? kPinScale : 10;
        return pin / scale;
    }

    int32 DialogueGraphAsset::decodePinOffset( int32 pin )
    {
        if ( pin <= 0 )
            return 0;
        const int32 scale = ( pin >= kPinScale ) ? kPinScale : 10;
        return pin % scale;
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
        for ( const DialogueGraphAssetInternal::NodeTypeName& entry : DialogueGraphAssetInternal::kArrNodeTypeName )
        {
            if ( entry._type == type )
                return entry._pName;
        }
        return "Unknown";
    }

    DialogueAssetNodeType DialogueGraphAsset::parseNodeType( string_view typeStr )
    {
        for ( const DialogueGraphAssetInternal::NodeTypeName& entry : DialogueGraphAssetInternal::kArrNodeTypeName )
        {
            if ( typeStr == entry._pName )
                return entry._type;
        }
        return DialogueAssetNodeType::Dialogue;
    }

    string DialogueGraphAsset::resolveLocalizedText( string_view textOrKey )
    {
        if ( textOrKey.empty() )
            return {};
        if ( engine::areEngineServicesBound() == false )
            return string{ textOrKey };

        // **키가 아닐 수도 있는 텍스트로 물어본다.** `hashed_string` 을 만들어 물으면 그 대사 원문이
        // intern 아레나에 영구히 남는다 — 대화가 늘수록 함께 늘어나는 누수였다. 표는 해시로만
        // 열리므로 intern 없이 물어볼 수 있다.
        const utf8* pResolved = engine::getLocalizationManager().getStringByText( textOrKey, nullptr );
        if ( StringUtil::isNullOrEmpty( pResolved ) )
            return string{ textOrKey };
        return string{ pResolved };
    }
} // namespace sw
