#include "pch.h"

#include "GameFramework/UI/DialogueRunnerComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/SaveGame.h"

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
    {
    }

    void DialogueRunnerComponent::onBeginPlay()
    {
        if ( _graphPath.empty() == false && _graph._listNode.empty() )
            loadGraphFile( _graphPath );
    }

    void DialogueRunnerComponent::onEndPlay()
    {
        stopDialogue();
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

        executeNode( _graph.findDefaultNextNodeId( _currentNodeId ) );
        return true;
    }

    bool DialogueRunnerComponent::selectChoice( int32 choiceIndex )
    {
        if ( _state != DialogueRunnerState::WaitingForChoice )
            return false;

        const int32 nextId = _graph.findChoiceNextNodeId( _currentNodeId, choiceIndex );
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
        if ( _onLine.isBound() )
            _onLine( _currentSpeaker, _currentText );
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
        _onLine = func;
    }

    void DialogueRunnerComponent::setOnDialogueChoices( OnDialogueChoicesFunc func )
    {
        _onChoices = func;
    }

    void DialogueRunnerComponent::setOnDialogueEvent( OnDialogueEventFunc func )
    {
        _onEvent = func;
    }

    void DialogueRunnerComponent::setOnDialogueFinished( OnDialogueFinishedFunc func )
    {
        _onFinished = func;
    }

    bool DialogueRunnerComponent::evaluateCondition( const string& condition ) const
    {
        if ( condition.empty() )
            return true;

        string flagKey = condition;
        int32  expectedVal{ 1 };
        bool   bEqualsComparison{ true };

        const size_t eqPos = condition.find( "==" );
        if ( eqPos != string::npos )
        {
            flagKey            = condition.substr( 0, eqPos );
            const string right = condition.substr( eqPos + 2 );
            StringUtil::parseInt( right, expectedVal );
        }
        else
        {
            const size_t neqPos = condition.find( "!=" );
            if ( neqPos != string::npos )
            {
                flagKey            = condition.substr( 0, neqPos );
                const string right = condition.substr( neqPos + 2 );
                StringUtil::parseInt( right, expectedVal );
                bEqualsComparison = false;
            }
        }

        flagKey = StringUtil::trim( flagKey );

        constexpr string_view kPrefix = "flag.";
        if ( StringUtil::startsWith( flagKey, kPrefix ) )
            flagKey = flagKey.substr( kPrefix.size() );

        const int32 currentVal = ( _pFlagStore != nullptr ) ? _pFlagStore->getFlag( flagKey ) : 0;
        return ( bEqualsComparison ) ? ( currentVal == expectedVal ) : ( currentVal != expectedVal );
    }

    void DialogueRunnerComponent::executeAction( const string& actionCmd )
    {
        if ( actionCmd.empty() )
            return;

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
                if ( colon != string::npos )
                    StringUtil::parseInt( rest.substr( colon + 1 ), val );
                _pFlagStore->setFlag( key, val );
            }
        }
    }

    void DialogueRunnerComponent::executeNode( int32 nodeId, int32 recursionDepth )
    {
        if ( recursionDepth > 64 )
        {
            SW_LOG_WARNING( "DialogueRunner: Cyclic node transition detected at node %#; breaking loop.", nodeId );
            _state = DialogueRunnerState::Finished;
            if ( _onFinished.isBound() )
                _onFinished();
            return;
        }

        if ( nodeId <= 0 )
        {
            _state = DialogueRunnerState::Finished;
            if ( _onFinished.isBound() )
                _onFinished();
            return;
        }

        const DialogueAssetNode* pNode = _graph.findNode( nodeId );
        if ( pNode == nullptr )
        {
            SW_LOG_WARNING( "Node %# not found in graph.", nodeId );
            _state = DialogueRunnerState::Finished;
            if ( _onFinished.isBound() )
                _onFinished();
            return;
        }

        _currentNodeId                = nodeId;
        const DialogueAssetNode& node = *pNode;

        if ( node._type == DialogueAssetNodeType::Start )
        {
            executeNode( _graph.findDefaultNextNodeId( nodeId ), recursionDepth + 1 );
        }
        else if ( node._type == DialogueAssetNodeType::Dialogue )
        {
            _state          = DialogueRunnerState::ShowingDialogue;
            _currentSpeaker = DialogueGraphAsset::resolveLocalizedText( node._speaker );
            _currentText    = DialogueGraphAsset::resolveLocalizedText( node._text );
            _listCurrentChoice.clear();

            if ( _onLine.isBound() )
                _onLine( _currentSpeaker, _currentText );
        }
        else if ( node._type == DialogueAssetNodeType::Choice )
        {
            _state          = DialogueRunnerState::WaitingForChoice;
            _currentSpeaker = DialogueGraphAsset::resolveLocalizedText( node._speaker );
            _currentText    = DialogueGraphAsset::resolveLocalizedText( node._text );
            _listCurrentChoice.clear();
            _listCurrentChoice.reserve( node._listChoice.size() );
            for ( const string& choice : node._listChoice )
                _listCurrentChoice.push_back( DialogueGraphAsset::resolveLocalizedText( choice ) );

            if ( _onChoices.isBound() )
                _onChoices( _listCurrentChoice );
        }
        else if ( node._type == DialogueAssetNodeType::Branch )
        {
            const bool bConditionMet = evaluateCondition( node._condition );
            int32      nextId        = _graph.findBranchNextNodeId( nodeId, bConditionMet );
            if ( nextId <= 0 )
                nextId = _graph.findDefaultNextNodeId( nodeId );
            executeNode( nextId, recursionDepth + 1 );
        }
        else if ( node._type == DialogueAssetNodeType::Action )
        {
            executeAction( node._actionCommand );
            if ( _state == DialogueRunnerState::Idle || _state == DialogueRunnerState::Finished )
                return;
            executeNode( _graph.findDefaultNextNodeId( nodeId ), recursionDepth + 1 );
        }
        else if ( node._type == DialogueAssetNodeType::End )
        {
            _state = DialogueRunnerState::Finished;
            if ( _onFinished.isBound() )
                _onFinished();
        }
    }
} // namespace sw
