#include "pch.h"

#include "GameFramework/UI/DialogueRunnerComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Dialogue/DialogueCursor.h"

#include "GameFramework/Base/SaveGame.h"

namespace sw
{
    namespace
    {
        struct DialogueRunnerComponentInternal
        {
            /** @brief 조건식의 비교 연산자입니다. */
            enum class CompareOp : uint8
            {
                Equal,
                NotEqual,
                GreaterEqual,
                LessEqual,
                Greater,
                Less
            };

            struct CompareOpToken
            {
                const utf8* _pToken;
                CompareOp   _op;
            };

            /**
             * @brief 조건식이 아는 비교 연산자 표입니다. 연산자는 **이 표 하나가** 정합니다.
             * @details 예전에는 `==` 와 `!=` 를 손으로 따로 찾아, 표에 없는 `>=` · `<=` · `>` · `<` 가 든 식(`flag.gold >= 10`)은 **식 전체가
             *          플래그 키**로 읽혀 늘 거짓이었다. 같은 자리에서는 앞에 적힌 것이 이기므로 두 글자 연산자를 먼저 둔다 — `>=` 의 `>` 를
             *          먼저 맞추면 오른쪽이 `=10` 이 된다.
             */
            static constexpr CompareOpToken kArrCompareOp[] = {
                {">=", CompareOp::GreaterEqual},
                {"<=",    CompareOp::LessEqual},
                {"==",        CompareOp::Equal},
                {"!=",     CompareOp::NotEqual},
                { ">",      CompareOp::Greater},
                { "<",         CompareOp::Less},
            };

            /** @brief 조건식에서 가장 앞의 비교 연산자를 찾습니다. 없으면 false 입니다. */
            [[nodiscard]] static bool findCompareOp( string_view condition, size_t& outPos, const CompareOpToken*& pOutOp )
            {
                for ( size_t pos = 0; pos < condition.size(); ++pos )
                {
                    for ( const CompareOpToken& entry : kArrCompareOp )
                    {
                        const string_view token{ entry._pToken };
                        if ( condition.compare( pos, token.size(), token ) != 0 )
                            continue;
                        outPos = pos;
                        pOutOp = &entry;
                        return true;
                    }
                }
                return false;
            }

            static bool compare( int32 lhs, CompareOp op, int32 rhs )
            {
                switch ( op )
                {
                    case CompareOp::Equal:
                        return lhs == rhs;
                    case CompareOp::NotEqual:
                        return lhs != rhs;
                    case CompareOp::GreaterEqual:
                        return lhs >= rhs;
                    case CompareOp::LessEqual:
                        return lhs <= rhs;
                    case CompareOp::Greater:
                        return lhs > rhs;
                    case CompareOp::Less:
                        return lhs < rhs;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "DialogueRunnerComponent" );

    DialogueRunnerComponent::DialogueRunnerComponent()
        : _graphPath{}
        , _graph{}
        , _pFlagStore{ nullptr }
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

    void DialogueRunnerComponent::setFlagStore( IFlagStore* pFlagStore )
    {
        _pFlagStore = pFlagStore;
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
        if ( condition.empty() )
            return true;

        // 연산자가 없으면 `키` 는 `키 == 1` 이다(켜진 플래그).
        const string_view                          conditionView{ condition.c_str(), condition.size() };
        string_view                                keyText = conditionView;
        int32                                      expectedVal{ 1 };
        DialogueRunnerComponentInternal::CompareOp op = DialogueRunnerComponentInternal::CompareOp::Equal;

        size_t                                                 opPos{ 0 };
        const DialogueRunnerComponentInternal::CompareOpToken* pOpToken = nullptr;
        if ( DialogueRunnerComponentInternal::findCompareOp( conditionView, opPos, pOpToken ) )
        {
            keyText                 = conditionView.substr( 0, opPos );
            const string_view right = StringUtil::trim( conditionView.substr( opPos + string_view{ pOpToken->_pToken }.size() ) );
            // 읽지 못한 식은 거짓이다 — 예전에는 알리기만 하고 기본값 1 과 비교해, `!=` 식은 오히려 참 쪽으로 갔다.
            if ( StringUtil::parseInt( right, expectedVal ) == false )
            {
                SW_LOG_WARNING( "Dialogue condition '%#' compares with '%#', which is not a number - the condition is false", condition, right );
                return false;
            }
            op = pOpToken->_op;
        }

        string_view flagKey = StringUtil::trim( keyText );
        // 표에 없는 연산자 글자(`=` 하나 · `!flag`)가 남았으면 키가 아니다 — 예전처럼 식 전체를 키로 읽으면 늘 0 이라 말없이 거짓이었다.
        if ( flagKey.empty() || flagKey.find_first_of( "=<>!" ) != string_view::npos )
        {
            SW_LOG_WARNING( "Dialogue condition '%#' is not understood (operators: == != >= <= > <) - the condition is false", condition );
            return false;
        }

        constexpr string_view kPrefix = "flag.";
        if ( StringUtil::startsWith( flagKey, kPrefix ) )
            flagKey = flagKey.substr( kPrefix.size() );

        const int32 currentVal = ( _pFlagStore != nullptr ) ? _pFlagStore->getFlag( flagKey ) : 0;
        return DialogueRunnerComponentInternal::compare( currentVal, op, expectedVal );
    }

    void DialogueRunnerComponent::executeAction( string actionCmd )
    {
        if ( actionCmd.empty() )
            return;

        // **값으로 받는다.** 예전에는 `const string&` 라서 `node._actionCommand` 를, 곧 `_graph`
        // 가 쥔 문자열을 가리켰다. 아래 `_onEvent` 핸들러가 `loadGraphFile()` 로 그래프를 갈면
        // 그 참조는 죽은 메모리가 되는데, **그 뒤로도 계속 읽는다**(`startsWith` · `substr`).
        SW_LOG_TRACE( "Execute Action: %#", actionCmd );
        if ( _onEvent.isBound() )
            _onEvent( actionCmd );

        if ( _pFlagStore != nullptr )
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
                _pFlagStore->setFlag( key, val );
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
                break;
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
                    _listCurrentChoice.push_back( DialogueGraphAsset::resolveLocalizedText( choice ) );

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
