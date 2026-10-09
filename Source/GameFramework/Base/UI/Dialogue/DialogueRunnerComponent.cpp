#include "pch.h"

#include "GameFramework/Base/UI/Dialogue/DialogueRunnerComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Dialogue/DialogueCursor.h"
#include "Engine/UI/UiSystem.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

namespace sw
{
    SW_LOG_CALLER( "DialogueRunnerComponent" );

    DialogueRunnerComponent::DialogueRunnerComponent()
        : _graphPath{}
        , _bPostSubtitles{ false }
        , _graph{}
        , _pFlags{ nullptr }
        , _pUiSystemOverride{ nullptr }
        , _currentSpeaker{}
        , _currentText{}
        , _listCurrentChoice{}
        , _onLine{}
        , _onChoices{}
        , _onEvent{}
        , _onFinished{}
        , _state{ DialogueRunnerState::Idle }
        , _currentNodeId{ 0 }
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
        const string path = _graphPath;
        if ( loadGraphFile( path ) == false )
            SW_LOG_WARNING( "Dialogue graph '%#' could not be loaded", path );
    }

    void DialogueRunnerComponent::onEndPlay()
    {
        stopDialogue();
        Component::onEndPlay();
    }

    void DialogueRunnerComponent::onTick( [[maybe_unused]] float32 deltaTime )
    {
    }

    bool DialogueRunnerComponent::loadGraphFile( string_view jsonPath )
    {
        _graphPath = string( jsonPath );
        return _graph.loadFromFile( jsonPath );
    }

    bool DialogueRunnerComponent::loadGraphJson( string_view jsonContent )
    {
        return _graph.parseJson( jsonContent );
    }

    void DialogueRunnerComponent::setGraph( DialogueGraphAsset graph )
    {
        _graph = std::move( graph );
    }

    bool DialogueRunnerComponent::startDialogue( int32 startNodeId )
    {
        if ( _graph._listNode.empty() )
        {
            SW_LOG_WARNING( "DialogueRunner: No valid nodes in graph." );
            return false;
        }

        int32 targetId = startNodeId;
        if ( targetId <= 0 )
        {
            const DialogueAssetNode* pStart = _graph.findStartNode();
            targetId                        = ( pStart != nullptr ) ? pStart->_id : 0;
        }

        if ( targetId <= 0 )
        {
            SW_LOG_WARNING( "DialogueRunner: No valid root or start node to execute." );
            return false;
        }

        executeNode( targetId );
        return true;
    }

    bool DialogueRunnerComponent::advance()
    {
        if ( _state != DialogueRunnerState::ShowingDialogue )
            return false;

        stepFrom( _currentNodeId, DialogueStepInput{}, 0 );
        return true;
    }

    bool DialogueRunnerComponent::selectChoice( int32 choiceIndex )
    {
        if ( _state != DialogueRunnerState::WaitingForChoice )
            return false;

        const DialogueAssetNode* pNode = _graph.findNode( _currentNodeId );
        DialogueStepInput        input{};
        input._choiceIndex = choiceIndex;
        const int32 nextId = ( pNode != nullptr ) ? DialogueCursor::step( _graph, *pNode, input ) : 0;
        if ( nextId > 0 )
        {
            executeNode( nextId );
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
        _currentNodeId        = 0;
        _currentSpeaker.clear();
        _currentText.clear();
        _listCurrentChoice.clear();

        if ( bWasActive && _onFinished.isBound() )
            _onFinished();
    }

    void DialogueRunnerComponent::previewLine( string speaker, string text )
    {
        _state          = DialogueRunnerState::ShowingDialogue;
        _currentSpeaker = std::move( speaker );
        _currentText    = std::move( text );
        _listCurrentChoice.clear();
        notifyLine();
    }

    void DialogueRunnerComponent::setFlags( GameFlags* pFlags )
    {
        _pFlags = pFlags;
    }

    DialogueRunnerState DialogueRunnerComponent::getState() const
    {
        return _state;
    }

    int32 DialogueRunnerComponent::getCurrentNodeId() const
    {
        return _currentNodeId;
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

    void DialogueRunnerComponent::setOnDialogueLine( OnDialogueLineFunc func )
    {
        _onLine = std::move( func );
    }

    void DialogueRunnerComponent::setOnDialogueChoices( OnDialogueChoicesFunc func )
    {
        _onChoices = std::move( func );
    }

    void DialogueRunnerComponent::setOnDialogueEvent( OnDialogueEventFunc func )
    {
        _onEvent = std::move( func );
    }

    void DialogueRunnerComponent::setOnDialogueFinished( OnDialogueFinishedFunc func )
    {
        _onFinished = std::move( func );
    }

    void DialogueRunnerComponent::notifyLine()
    {
        if ( _bPostSubtitles )
        {
            UiSystem* pUiSystem = _pUiSystemOverride != nullptr ? _pUiSystemOverride : game::getService<UiSystem>();
            if ( pUiSystem != nullptr && pUiSystem->isInitialized() && _currentText.empty() == false )
                (void)pUiSystem->getSubtitles().post( _currentSpeaker, _currentText );
        }
        if ( _onLine.isBound() == false )
            return;

        // **사본을 넘긴다.** 델리게이트는 `const string&` 를 받는데 그것이 이 객체의 멤버를
        // 그대로 가리키면, 핸들러가 그 안에서 `advance()` · `stopDialogue()` · `startDialogue()`
        // 를 부르는 순간 **자기가 받은 참조가 바뀌거나 비워진다.** 대화 UI 에서 "이 줄을 보고
        // 바로 다음으로 넘긴다" 는 가장 흔한 사용법이고, 그러면 핸들러가 돌아와서 읽는 `text`
        // 는 방금 받은 줄이 아니라 다음 줄이다(재할당이 일어났으면 그마저도 아니다).
        const string speaker = _currentSpeaker;
        const string text    = _currentText;
        _onLine( speaker, text );
    }

    void DialogueRunnerComponent::notifyChoices()
    {
        if ( _onChoices.isBound() == false )
            return;

        // 위와 같은 이유다. 이쪽은 더 나쁜데, 핸들러가 목록을 **돌면서** `selectChoice()` 를
        // 부르면 그 순간 `_listCurrentChoice` 가 비워지고 다시 채워진다.
        const vector<string> listChoice = _listCurrentChoice;
        _onChoices( listChoice );
    }

    bool DialogueRunnerComponent::evaluateCondition( const string& condition ) const
    {
        // 문법은 월드 플래그의 조건식 하나다(`GameFlags::parseCondition`). 빈 식은 참이고, 읽지 못한 식은 `GameFlags` 가 경고하고 거짓이다.
        // 저장소가 없으면 모든 이름이 0 인 빈 플래그로 평가한다.
        if ( _pFlags != nullptr )
            return _pFlags->evaluate( condition );
        const GameFlags noFlags;
        return noFlags.evaluate( condition );
    }

    void DialogueRunnerComponent::executeAction( string actionCmd )
    {
        if ( actionCmd.empty() )
            return;

        // **값으로 받는다.** `const string&` 로 받으면 `node._actionCommand`, 곧 `_graph` 가 가진
        // 문자열을 가리킨다. 아래 `_onEvent` 핸들러가 `loadGraphFile()` 로 그래프를 갈면
        // 그 참조는 죽은 메모리가 되는데, **그 뒤로도 계속 읽는다**(`startsWith` · `substr`).
        SW_LOG_TRACE( "Execute Action: %#", actionCmd );
        if ( _onEvent.isBound() )
            _onEvent( actionCmd );

        if ( _pFlags != nullptr )
        {
            constexpr string_view kSetFlag = "set_flag:";
            if ( StringUtil::startsWith( actionCmd, kSetFlag ) )
            {
                const string rest  = actionCmd.substr( kSetFlag.size() );
                const size_t colon = rest.find( ':' );
                const string key   = ( colon != string::npos ) ? rest.substr( 0, colon ) : string{ rest };
                int32        val{ 1 };
                if ( colon != string::npos && StringUtil::parseInt( rest.substr( colon + 1 ), val ) == false )
                    SW_LOG_WARNING( "Dialogue action '%#' sets a value that is not a number - using 1", rest );
                // 0 은 지우기다(`GameFlags` 규칙) — 읽는 쪽에는 없는 플래그와 같은 0 이다.
                _pFlags->setFlag( hashed_string( key ), val );
            }
        }
    }

    void DialogueRunnerComponent::finishDialogue()
    {
        _state = DialogueRunnerState::Finished;
        if ( _onFinished.isBound() )
            _onFinished();
    }

    void DialogueRunnerComponent::stepFrom( int32 nodeId, const DialogueStepInput& input, int32 recursionDepth )
    {
        // 노드를 다시 찾는다 — Action 핸들러가 그래프를 갈았으면 앞서 찾은 노드 참조는 죽은 메모리다.
        const DialogueAssetNode* pNode = _graph.findNode( nodeId );
        executeNode( pNode != nullptr ? DialogueCursor::step( _graph, *pNode, input ) : 0, recursionDepth );
    }

    void DialogueRunnerComponent::executeNode( int32 nodeId, int32 recursionDepth )
    {
        ++_transitionSerial;
        if ( recursionDepth > 64 )
        {
            SW_LOG_WARNING( "DialogueRunner: Cyclic node transition detected at node %#; breaking loop.", nodeId );
            finishDialogue();
            return;
        }

        if ( nodeId <= 0 )
        {
            finishDialogue();
            return;
        }

        const DialogueAssetNode* pNode = _graph.findNode( nodeId );
        if ( pNode == nullptr )
        {
            SW_LOG_WARNING( "Node %# not found in graph.", nodeId );
            finishDialogue();
            return;
        }

        _currentNodeId                = nodeId;
        const DialogueAssetNode& node = *pNode;
        DialogueStepInput        input{};

        switch ( node._type )
        {
            case DialogueAssetNodeType::Start:
            {
                break;
            }
            case DialogueAssetNodeType::Dialogue:
            {
                _state          = DialogueRunnerState::ShowingDialogue;
                _currentSpeaker = DialogueGraphAsset::resolveLocalizedText( node._speaker );
                _currentText    = DialogueGraphAsset::resolveLocalizedText( node._text );
                _listCurrentChoice.clear();
                notifyLine();
                return;
            }
            case DialogueAssetNodeType::Choice:
            {
                _state          = DialogueRunnerState::WaitingForChoice;
                _currentSpeaker = DialogueGraphAsset::resolveLocalizedText( node._speaker );
                _currentText    = DialogueGraphAsset::resolveLocalizedText( node._text );
                _listCurrentChoice.clear();
                _listCurrentChoice.reserve( node._listChoice.size() );
                for ( const string& choice : node._listChoice )
                {
                    _listCurrentChoice.push_back( DialogueGraphAsset::resolveLocalizedText( choice ) );
                }

                notifyChoices();
                return;
            }
            case DialogueAssetNodeType::Branch:
            {
                input._bConditionMet = evaluateCondition( node._condition );
                break;
            }
            case DialogueAssetNodeType::Action:
            {
                // 핸들러가 그 안에서 대화를 멈추거나 · 다시 시작하거나 · 진행시켰으면 여기서 더 가지 않는다.
                // 상태 값으로는 알 수 없다 — 첫 대사 전의 Action 은 원래 Idle 에서 돈다.
                const uint32 serialBeforeAction = _transitionSerial;
                executeAction( node._actionCommand );
                if ( _transitionSerial != serialBeforeAction )
                    return;
                break;
            }
            case DialogueAssetNodeType::End:
            {
                finishDialogue();
                return;
            }
            case DialogueAssetNodeType::Count:
            {
                SW_LOG_WARNING( "DialogueRunner: node %# has unknown type %#; finishing the dialogue.", nodeId, static_cast<uint32>( node._type ) );
                finishDialogue();
                return;
            }
        }

        stepFrom( nodeId, input, recursionDepth + 1 );
    }
} // namespace sw
