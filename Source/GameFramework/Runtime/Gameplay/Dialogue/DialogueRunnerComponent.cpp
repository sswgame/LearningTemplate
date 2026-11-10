#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Dialogue/DialogueRunnerComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Dialogue/DialogueCursor.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Presentation/ISubtitleSink.h"

#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    DialogueRunnerComponent::DialogueRunnerComponent()
        : _graphPath{}
        , _bPostSubtitles{ false }
        , _listSpeakerSlot{}
        , _graph{}
        , _listCompiled{}
        , _pFlags{ nullptr }
        , _world{}
        , _pSubtitleSinkOverride{ nullptr }
        , _currentSpeaker{}
        , _currentText{}
        , _listCurrentChoice{}
        , _onLine{}
        , _onChoices{}
        , _onEvent{}
        , _onFinished{}
        , _state{ DialogueRunnerState::Idle }
        , _currentNodeID{ 0 }
        , _currentSpeakerID{ kDialogueNoSpeaker }
        , _transitionSerial{ 0 }
    {
        setCanEverTick( false ); // 대화는 입력 · 이벤트로만 넘어간다 — 빈 틱에 워커를 쓰지 않는다
    }

    void DialogueRunnerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _graph._listNode.empty() )
            loadGraphFromPath();
    }

    void DialogueRunnerComponent::onPostLoad()
    {
        Component::onPostLoad();
        loadGraphFromPath();
    }

    void DialogueRunnerComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_graphPathName( "_graphPath" );
        if ( propertyName == s_graphPathName )
            loadGraphFromPath();
    }

    void DialogueRunnerComponent::loadGraphFromPath()
    {
        if ( _graphPath.empty() )
            return;
        const string path = _graphPath.path();
        if ( loadGraphFile( AssetID{ path } ) == false )
            SW_LOG_WARNING_ID( "DialogueRunnerComponent.DialogueGraphCouldNot", "Dialogue graph '%#' could not be loaded", path );
    }

    void DialogueRunnerComponent::onEndPlay()
    {
        stopDialogue();
        Component::onEndPlay();
    }

    void DialogueRunnerComponent::onTick( [[maybe_unused]] float32 deltaTime )
    {
    }

    bool DialogueRunnerComponent::loadGraphFile( const AssetID& assetID )
    {
        const string_view xmlPath = assetID.view();
        _graphPath                = string( xmlPath );
        return _graph.loadFromAsset( assetID ) && compileGraph();
    }

    bool DialogueRunnerComponent::loadGraphXML( string_view xmlContent )
    {
        return _graph.loadFromXMLText( xmlContent ) && compileGraph();
    }

    void DialogueRunnerComponent::setGraph( DialogueGraphAsset graph )
    {
        _graph = std::move( graph );
        (void)compileGraph(); // 틀린 글은 오류 로그를 남기고 빈 그래프가 된다 - 시작이 "노드 없음" 으로 거절한다
    }

    bool DialogueRunnerComponent::compileGraph()
    {
        _listCompiled.clear();
        const string_view sourceName = _graphPath.empty() ? string_view{ "DialogueRunner" } : string_view{ _graphPath };
        bool              bValid     = true;
        for ( const DialogueAssetNode& node : _graph._listNode )
        {
            const bool bBranch = node._type == DialogueAssetNodeKind::Branch;
            const bool bAction = node._type == DialogueAssetNodeKind::Action;
            if ( bBranch == false && bAction == false )
                continue;
            DialogueCompiledNode compiled;
            compiled._nodeID = node._id;
            if ( bBranch )
                bValid = ConditionExpr::compile( node._condition, sourceName, compiled._condition ) && bValid;
            else if ( node._actionCommand.empty() == false )
                bValid = GameActionCall::compileText( node._actionCommand, sourceName, compiled._action ) && bValid;
            _listCompiled.push_back( std::move( compiled ) );
        }
        if ( bValid == false )
        {
            _graph = DialogueGraphAsset{};
            _listCompiled.clear();
        }
        return bValid;
    }

    const DialogueCompiledNode* DialogueRunnerComponent::findCompiled( int32 nodeID ) const
    {
        for ( const DialogueCompiledNode& compiled : _listCompiled )
        {
            if ( compiled._nodeID == nodeID )
                return &compiled;
        }
        return nullptr;
    }

    GameplayContext DialogueRunnerComponent::makeWorld( int32 nodeID ) const
    {
        GameplayContext world = _world;
        world.set<GameFlagsSlot>( _pFlags );
        world.set<GameFlagsWriteSlot>( _pFlags );
        world.setSeed( GameConditions::makeSeed( static_cast<int32>( _transitionSerial ), nodeID ) );
        return world;
    }

    bool DialogueRunnerComponent::startDialogue( int32 startNodeID )
    {
        if ( _graph._listNode.empty() )
        {
            SW_LOG_WARNING_ID( "DialogueRunnerComponent.DialogueRunnerNoValidNodes", "DialogueRunner: No valid nodes in graph." );
            return false;
        }

        int32 targetID = startNodeID;
        if ( targetID <= 0 )
        {
            const DialogueAssetNode* pStart = _graph.findStartNode();
            targetID                        = ( pStart != nullptr ) ? pStart->_id : 0;
        }

        if ( targetID <= 0 )
        {
            SW_LOG_WARNING_ID( "DialogueRunnerComponent.DialogueRunnerNoValidRoot", "DialogueRunner: No valid root or start node to execute." );
            return false;
        }

        executeNode( targetID );
        return true;
    }

    bool DialogueRunnerComponent::advance()
    {
        if ( _state != DialogueRunnerState::ShowingDialogue )
            return false;

        executeNode( findNextNode( _currentNodeID, DialogueStepInput{} ) );
        return true;
    }

    bool DialogueRunnerComponent::selectChoice( int32 choiceIndex )
    {
        if ( _state != DialogueRunnerState::WaitingForChoice )
            return false;

        // 없는 번호는 호출한 쪽의 잘못이다 - 대화를 끝내지 않고 선택을 계속 기다린다.
        if ( choiceIndex < 0 || static_cast<size_t>( choiceIndex ) >= _listCurrentChoice.size() )
            return false;

        const DialogueAssetNode* pNode = _graph.findNode( _currentNodeID );
        DialogueStepInput        input{};
        input._choiceIndex           = choiceIndex;
        const optional<int32> nextID = ( pNode != nullptr ) ? DialogueCursor::step( _graph, *pNode, input ) : nullopt;
        if ( nextID.has_value() )
        {
            executeNode( nextID );
            return true;
        }

        stopDialogue();
        return false;
    }

    void DialogueRunnerComponent::stopDialogue()
    {
        ++_transitionSerial;
        const bool bWasActive = ( _state != DialogueRunnerState::Idle && _state != DialogueRunnerState::Finished );
        _state                = DialogueRunnerState::Idle;
        _currentNodeID        = 0;
        _currentSpeakerID     = kDialogueNoSpeaker;
        _currentSpeaker.clear();
        _currentText.clear();
        _listCurrentChoice.clear();

        if ( bWasActive && _onFinished.isBound() )
        {
            const OnDialogueFinishedFunc onFinished = _onFinished.get();
            onFinished();
        }
    }

    GameObjectHandle DialogueRunnerComponent::previewLine( string graphPath, uint32 speakerID, string text )
    {
        if ( _graphPath.path() != graphPath )
            return GameObjectHandle{}; // 다른 그래프를 돌리는 러너다
        _state            = DialogueRunnerState::ShowingDialogue;
        _currentSpeakerID = speakerID;
        _currentSpeaker   = _graph.resolveSpeakerName( speakerID );
        _currentText      = DialogueGraphAsset::resolveLocalizedText( text );
        _listCurrentChoice.clear();
        notifyLine();
        const GameObject* pSpeaker = findSpeakerObject( speakerID );
        return pSpeaker != nullptr ? pSpeaker->getHandle() : GameObjectHandle{};
    }

    GameObject* DialogueRunnerComponent::findSpeakerObject( uint32 speakerID ) const
    {
        const GameObject*        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || speakerID == kDialogueNoSpeaker )
            return nullptr;
        const DialogueSpeakerSlot* pSlot = VectorUtil::findBy<&DialogueSpeakerSlot::_speaker>( _listSpeakerSlot, speakerID );
        return pSlot != nullptr && pSlot->_object.isValid() ? pManager->resolveGameObject( pSlot->_object ) : nullptr;
    }

    void DialogueRunnerComponent::setSpeakerObject( uint32 speakerID, GameObjectHandle object )
    {
        VectorUtil::removeIf( _listSpeakerSlot, [speakerID]( const DialogueSpeakerSlot& slot )
        { return slot._speaker == speakerID; } );
        if ( object.isValid() )
            _listSpeakerSlot.push_back( DialogueSpeakerSlot{ speakerID, object } );
    }

    void DialogueRunnerComponent::setFlags( GameFlags* pFlags )
    {
        _pFlags = pFlags;
    }

    void DialogueRunnerComponent::setGameplayContext( const GameplayContext& context )
    {
        _world = context;
    }

    DialogueRunnerState DialogueRunnerComponent::getState() const
    {
        return _state;
    }

    int32 DialogueRunnerComponent::getCurrentNodeID() const
    {
        return _currentNodeID;
    }

    const string& DialogueRunnerComponent::getCurrentSpeaker() const
    {
        return _currentSpeaker;
    }

    const string& DialogueRunnerComponent::getCurrentText() const
    {
        return _currentText;
    }

    const vector<string>& DialogueRunnerComponent::getCurrentChoices() const
    {
        return _listCurrentChoice;
    }

    void DialogueRunnerComponent::notifyLine()
    {
        if ( _bPostSubtitles )
        {
            // 자막은 화면 스택 위의 엔진 서비스다 — 게임 코드는 창구만 안다. 창구가 없으면(전용 서버) 보일 곳이 없다.
            ISubtitleSink* pSubtitles = _pSubtitleSinkOverride != nullptr ? _pSubtitleSinkOverride : game::findSubsystem<ISubtitleSink>();
            if ( pSubtitles != nullptr && _currentText.empty() == false )
                pSubtitles->postSubtitle( _currentSpeaker, _currentText, 0.0f ); // 읽기 시간
        }
        if ( _onLine.isBound() == false )
            return;

        // **사본을 넘긴다.** 델리게이트는 `const string&` 를 받는데 그것이 이 객체의 멤버를
        // 그대로 가리키면, 핸들러가 그 안에서 `advance()` · `stopDialogue()` · `startDialogue()`
        // 를 부르는 순간 **자기가 받은 참조가 바뀌거나 비워진다.** 대화 UI 에서 "이 줄을 보고
        // 바로 다음으로 넘긴다" 는 가장 흔한 사용법이고, 그러면 핸들러가 돌아와서 읽는 `text`
        // 는 방금 받은 줄이 아니라 다음 줄이다(재할당이 일어났으면 그마저도 아니다).
        const string             speaker = _currentSpeaker;
        const string             text    = _currentText;
        const OnDialogueLineFunc onLine  = _onLine.get(); // 핸들러가 안에서 구독을 놓아도 부르는 델리게이트는 산다
        onLine( speaker, text );
    }

    void DialogueRunnerComponent::notifyChoices()
    {
        if ( _onChoices.isBound() == false )
            return;

        // 위와 같은 이유다. 이쪽은 더 나쁜데, 핸들러가 목록을 **돌면서** `selectChoice()` 를
        // 부르면 그 순간 `_listCurrentChoice` 가 비워지고 다시 채워진다.
        const vector<string>        listChoice = _listCurrentChoice;
        const OnDialogueChoicesFunc onChoices  = _onChoices.get();
        onChoices( listChoice );
    }

    bool DialogueRunnerComponent::evaluateCondition( int32 nodeID ) const
    {
        const DialogueCompiledNode* pCompiled = findCompiled( nodeID );
        return pCompiled != nullptr && pCompiled->_condition.evaluate( makeWorld( nodeID ) );
    }

    void DialogueRunnerComponent::executeAction( int32 nodeID, string actionCmd )
    {
        const DialogueCompiledNode* pCompiled = findCompiled( nodeID );
        if ( pCompiled == nullptr || pCompiled->_action.isValid() == false )
            return;
        const GameActionCall call = pCompiled->_action; // 핸들러가 그래프를 갈아도 호출은 산다(칸은 나눠 든다)
        ActionContext        context;
        context._world  = makeWorld( nodeID );
        context._pOwner = this;

        // **값으로 받는다.** `const string&` 로 받으면 `node._actionCommand`, 곧 `_graph` 가 가진
        // 문자열을 가리킨다. 아래 `_onEvent` 핸들러가 `loadGraphFile()` 로 그래프를 갈면
        // 그 참조는 죽은 메모리가 되는데, **그 뒤로도 계속 읽는다**(`startsWith` · `substr`).
        SW_LOG_TRACE_ID( "DialogueRunnerComponent.ExecuteAction", "Execute Action: %#", actionCmd );
        if ( _onEvent.isBound() )
        {
            const OnDialogueEventFunc onEvent = _onEvent.get();
            onEvent( actionCmd );
        }

        // 대화는 사람을 기다릴 뿐 동작을 기다리지 않는다 — `Running` 은 끝난 것으로 본다. 플래그 저장소가 없는 러너의 `flag.*` 실패는 정상이다.
        (void)call.invoke( context );
    }

    void DialogueRunnerComponent::finishDialogue()
    {
        _state = DialogueRunnerState::Finished;
        if ( _onFinished.isBound() )
        {
            const OnDialogueFinishedFunc onFinished = _onFinished.get();
            onFinished();
        }
    }

    optional<int32> DialogueRunnerComponent::findNextNode( int32 nodeID, const DialogueStepInput& input ) const
    {
        // 노드를 다시 찾는다 — Action 핸들러가 그래프를 갈았으면 앞서 찾은 노드 참조는 죽은 메모리다.
        const DialogueAssetNode* pNode = _graph.findNode( nodeID );
        return pNode != nullptr ? DialogueCursor::step( _graph, *pNode, input ) : nullopt;
    }

    void DialogueRunnerComponent::executeNode( optional<int32> startNodeID )
    {
        // 재귀로 따라가면 깊이 제한에 닿기 전에 스택이 넘친다(Debug 에서 프레임이 크다) — 지나가는 노드는 반복으로 돈다.
        optional<int32> nodeID = startNodeID;
        for ( int32 chainedCount = 0; chainedCount <= kMaxChainedNodes; ++chainedCount )
        {
            if ( runNode( nodeID, nodeID ) == false )
                return;
        }
        SW_LOG_WARNING_ID( "DialogueRunnerComponent.DialogueRunnerCyclicNodeTransition", "DialogueRunner: Cyclic node transition detected at node %#; breaking loop.", nodeID.value_or( 0 ) );
        finishDialogue();
    }

    bool DialogueRunnerComponent::runNode( optional<int32> nextNodeID, optional<int32>& outNextNodeID )
    {
        ++_transitionSerial;
        if ( nextNodeID.has_value() == false )
        {
            finishDialogue();
            return false;
        }
        const int32 nodeID = *nextNodeID;

        const DialogueAssetNode* pNode = _graph.findNode( nodeID );
        if ( pNode == nullptr )
        {
            SW_LOG_WARNING_ID( "DialogueRunnerComponent.NodeNotFoundGraph", "Node %# not found in graph.", nodeID );
            finishDialogue();
            return false;
        }

        _currentNodeID                = nodeID;
        const DialogueAssetNode& node = *pNode;
        DialogueStepInput        input{};

        switch ( node._type )
        {
            case DialogueAssetNodeKind::Start:
            {
                break;
            }
            case DialogueAssetNodeKind::Dialogue:
            {
                _state            = DialogueRunnerState::ShowingDialogue;
                _currentSpeakerID = node._speaker;
                _currentSpeaker   = _graph.resolveSpeakerName( node._speaker );
                _currentText      = DialogueGraphAsset::resolveLocalizedText( node._text );
                _listCurrentChoice.clear();
                notifyLine();
                return false;
            }
            case DialogueAssetNodeKind::Choice:
            {
                _state            = DialogueRunnerState::WaitingForChoice;
                _currentSpeakerID = node._speaker;
                _currentSpeaker   = _graph.resolveSpeakerName( node._speaker );
                _currentText      = DialogueGraphAsset::resolveLocalizedText( node._text );
                _listCurrentChoice.clear();
                _listCurrentChoice.reserve( node._listChoice.size() );
                for ( const string& choice : node._listChoice )
                {
                    _listCurrentChoice.push_back( DialogueGraphAsset::resolveLocalizedText( choice ) );
                }

                notifyChoices();
                return false;
            }
            case DialogueAssetNodeKind::Branch:
            {
                input._bConditionMet = evaluateCondition( nodeID );
                break;
            }
            case DialogueAssetNodeKind::Action:
            {
                // 핸들러가 그 안에서 대화를 멈추거나 · 다시 시작하거나 · 진행시켰으면 여기서 더 가지 않는다.
                // 상태 값으로는 알 수 없다 — 첫 대사 전의 Action 은 원래 Idle 에서 돈다.
                const uint32 serialBeforeAction = _transitionSerial;
                executeAction( nodeID, node._actionCommand );
                if ( _transitionSerial != serialBeforeAction )
                    return false;
                break;
            }
            case DialogueAssetNodeKind::End:
            {
                finishDialogue();
                return false;
            }
            case DialogueAssetNodeKind::Count:
            {
                SW_LOG_WARNING_ID( "DialogueRunnerComponent.DialogueRunnerNodeUnknownType", "DialogueRunner: node %# has unknown type %#; finishing the dialogue.", nodeID, static_cast<uint32>( node._type ) );
                finishDialogue();
                return false;
            }
        }

        outNextNodeID = findNextNode( nodeID, input );
        return true;
    }
} // namespace sw
