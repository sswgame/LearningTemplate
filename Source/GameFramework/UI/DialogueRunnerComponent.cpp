#include "pch.h"

#include "GameFramework/UI/DialogueRunnerComponent.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
	DialogueRunnerComponent::DialogueRunnerComponent()
		: _pSaveSlot{ nullptr }
		, _state{ DialogueRunnerState::Idle }
		, _currentNodeId{ 0 }
		, _currentSpeaker{}
		, _currentText{}
		, _listCurrentChoices{}
		, _mapNodes{}
		, _onLine{}
		, _onChoices{}
		, _onEvent{}
		, _onFinished{}
	{
	}

	void DialogueRunnerComponent::onBeginPlay()
	{
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
		vector<uint8> listData;
		if ( FileUtil::readFile( jsonPath, listData ) == false || listData.empty() )
		{
			SW_LOG_WARNING( "[DialogueRunnerComponent] Failed to read graph file: %#", jsonPath );
			return false;
		}

		const string json( listData.begin(), listData.end() );
		return loadGraphJson( json );
	}

	bool DialogueRunnerComponent::loadGraphJson( string_view jsonContent )
	{
		_mapNodes.clear();

		JsonDocument doc;
		if ( doc.parse( jsonContent ) == false )
		{
			SW_LOG_WARNING( "[DialogueRunnerComponent] Failed to parse graph JSON" );
			return false;
		}

		const JsonValue root = doc.root();

		// 1) Parse Nodes
		if ( root.has( "nodes" ) )
		{
			const JsonValue nodesVal = root.get( "nodes" );
			if ( nodesVal.isArray() )
			{
				const size_t nodeCount = nodesVal.size();
				for ( size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
				{
					const JsonValue nodeJson = nodesVal.at( nodeIndex );
					if ( nodeJson.isObject() == false )
						continue;

					RuntimeNode node{};
					node._id		= static_cast<int32>( nodeJson.get( "id" ).asInt( 0 ) );
					node._type		= nodeJson.get( "type" ).asString();
					node._speaker	= nodeJson.get( "speaker" ).asString();
					node._text		= nodeJson.get( "text" ).asString();
					node._condition = nodeJson.get( "condition" ).asString();
					node._action	= nodeJson.get( "action" ).asString();

					if ( nodeJson.has( "choices" ) )
					{
						const JsonValue choicesVal = nodeJson.get( "choices" );
						if ( choicesVal.isArray() )
						{
							const size_t choiceCount = choicesVal.size();
							node._listChoices.reserve( choiceCount );
							for ( size_t choiceIndex = 0; choiceIndex < choiceCount; ++choiceIndex )
							{
								node._listChoices.push_back( choicesVal.at( choiceIndex ).asString() );
							}
						}
					}

					if ( node._id > 0 )
						_mapNodes[node._id] = std::move( node );
				}
			}
		}

		// 2) Parse Links
		if ( root.has( "links" ) )
		{
			const JsonValue linksVal = root.get( "links" );
			if ( linksVal.isArray() )
			{
				const size_t linkCount = linksVal.size();
				for ( size_t linkIndex = 0; linkIndex < linkCount; ++linkIndex )
				{
					const JsonValue linkJson = linksVal.at( linkIndex );
					if ( linkJson.isObject() == false )
						continue;

					const int32 fromPin = static_cast<int32>( linkJson.get( "from" ).asInt( 0 ) );
					const int32 toPin	= static_cast<int32>( linkJson.get( "to" ).asInt( 0 ) );

					if ( fromPin > 0 && toPin > 0 )
					{
						const bool	bIsHundredScale = ( fromPin >= 100 );
						const int32 scale			= bIsHundredScale ? 100 : 10;
						const int32 sourceNodeId	= fromPin / scale;
						const int32 targetNodeId	= toPin / scale;
						const int32 pinOffset		= fromPin % scale;

						auto it = _mapNodes.find( sourceNodeId );
						if ( it != _mapNodes.end() )
						{
							RuntimeNode& srcNode = it->second;
							if ( bIsHundredScale )
							{
								if ( pinOffset == 2 )
								{
									// Default Out Pin (kPinOutputOffset = 2)
									srcNode._nextDefaultNodeId = targetNodeId;
								}
								else if ( pinOffset == 3 )
								{
									// Branch True (kPinTrueOffset = 3)
									srcNode._trueNodeId = targetNodeId;
								}
								else if ( pinOffset == 4 )
								{
									// Branch False (kPinFalseOffset = 4)
									srcNode._falseNodeId = targetNodeId;
								}
								else if ( pinOffset >= 10 )
								{
									// Choice Pins (kPinChoiceBase = 10 + index)
									const int32 choiceIndex				   = pinOffset - 10;
									srcNode._mapChoiceToNodeId[choiceIndex] = targetNodeId;
								}
							}
							else
							{
								if ( pinOffset == 1 )
								{
									// Default Out Pin
									srcNode._nextDefaultNodeId = targetNodeId;
								}
								else if ( pinOffset == 2 )
								{
									// Branch True (or choice 0)
									srcNode._trueNodeId			  = targetNodeId;
									srcNode._mapChoiceToNodeId[0] = targetNodeId;
								}
								else if ( pinOffset == 3 )
								{
									// Branch False (or choice 1)
									srcNode._falseNodeId		  = targetNodeId;
									srcNode._mapChoiceToNodeId[1] = targetNodeId;
								}
								else if ( pinOffset >= 4 )
								{
									// Additional Choice Pins
									srcNode._mapChoiceToNodeId[pinOffset - 2] = targetNodeId;
								}
							}
						}
					}
				}
			}
		}

		SW_LOG_INFO( "[DialogueRunnerComponent] Loaded dialogue graph (%# nodes)", static_cast<uint32>( _mapNodes.size() ) );
		return _mapNodes.empty() == false;
	}

	bool DialogueRunnerComponent::startDialogue( int32 startNodeId )
	{
		if ( _mapNodes.empty() )
		{
			SW_LOG_WARNING( "[DialogueRunnerComponent] Cannot start dialogue: no nodes loaded." );
			return false;
		}

		int32 targetId = startNodeId;
		if ( targetId <= 0 )
		{
			// Find Start node
			for ( const auto& [id, node] : _mapNodes )
			{
				if ( node._type == "Start" )
				{
					targetId = id;
					break;
				}
			}
			if ( targetId <= 0 )
				targetId = _mapNodes.begin()->first;
		}

		executeNode( targetId );
		return true;
	}

	bool DialogueRunnerComponent::advance()
	{
		if ( _state != DialogueRunnerState::ShowingDialogue )
			return false;

		auto it = _mapNodes.find( _currentNodeId );
		if ( it == _mapNodes.end() )
		{
			stopDialogue();
			return false;
		}

		executeNode( it->second._nextDefaultNodeId );
		return true;
	}

	bool DialogueRunnerComponent::selectChoice( int32 choiceIndex )
	{
		if ( _state != DialogueRunnerState::WaitingForChoice )
			return false;

		auto it = _mapNodes.find( _currentNodeId );
		if ( it == _mapNodes.end() )
		{
			stopDialogue();
			return false;
		}

		const auto& mapChoices = it->second._mapChoiceToNodeId;
		auto		chIt	   = mapChoices.find( choiceIndex );
		if ( chIt != mapChoices.end() )
		{
			executeNode( chIt->second );
			return true;
		}

		if ( it->second._nextDefaultNodeId > 0 )
		{
			executeNode( it->second._nextDefaultNodeId );
			return true;
		}

		stopDialogue();
		return false;
	}

	void DialogueRunnerComponent::stopDialogue()
	{
		const bool bWasActive = ( _state != DialogueRunnerState::Idle && _state != DialogueRunnerState::Finished );
		_state				  = DialogueRunnerState::Idle;
		_currentNodeId		  = 0;
		_currentSpeaker.clear();
		_currentText.clear();
		_listCurrentChoices.clear();

		if ( bWasActive && _onFinished.isBound() )
			_onFinished();
	}

	void DialogueRunnerComponent::setSaveSlot( SaveSlot* pSaveSlot )
	{
		_pSaveSlot = pSaveSlot;
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
		return _listCurrentChoices;
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
			flagKey			   = condition.substr( 0, eqPos );
			const string right = condition.substr( eqPos + 2 );
			expectedVal		   = StringUtil::atoi( right.c_str() );
		}
		else
		{
			const size_t neqPos = condition.find( "!=" );
			if ( neqPos != string::npos )
			{
				flagKey			   = condition.substr( 0, neqPos );
				const string right = condition.substr( neqPos + 2 );
				expectedVal		   = StringUtil::atoi( right.c_str() );
				bEqualsComparison  = false;
			}
		}

		// Trim whitespace and flag. prefix
		while ( flagKey.empty() == false && ( flagKey.front() == ' ' || flagKey.front() == '\t' ) )
			flagKey.erase( flagKey.begin() );
		while ( flagKey.empty() == false && ( flagKey.back() == ' ' || flagKey.back() == '\t' ) )
			flagKey.pop_back();

		constexpr const char* kPrefix = "flag.";
		if ( flagKey.rfind( kPrefix, 0 ) == 0 )
			flagKey = flagKey.substr( StringUtil::strlen( kPrefix ) );

		const int32 currentVal = ( _pSaveSlot != nullptr ) ? _pSaveSlot->getFlag( flagKey ) : 0;
		return ( bEqualsComparison ) ? ( currentVal == expectedVal ) : ( currentVal != expectedVal );
	}

	void DialogueRunnerComponent::executeAction( const string& actionCmd )
	{
		if ( actionCmd.empty() )
			return;

		SW_LOG_INFO( "[DialogueRunnerComponent] Execute Action: %#", actionCmd );
		if ( _onEvent.isBound() )
			_onEvent( actionCmd );

		if ( _pSaveSlot != nullptr )
		{
			// Example action: set_flag:quest_started:1
			constexpr const char* kSetFlag = "set_flag:";
			if ( actionCmd.rfind( kSetFlag, 0 ) == 0 )
			{
				const string rest  = actionCmd.substr( StringUtil::strlen( kSetFlag ) );
				const size_t colon = rest.find( ':' );
				const string key   = ( colon != string::npos ) ? rest.substr( 0, colon ) : string{ rest };
				const int32	 val   = ( colon != string::npos ) ? StringUtil::atoi( rest.c_str() + colon + 1 ) : 1;
				_pSaveSlot->setFlag( key, val );
			}
		}
	}

	void DialogueRunnerComponent::executeNode( int32 nodeId )
	{
		if ( nodeId <= 0 )
		{
			_state = DialogueRunnerState::Finished;
			if ( _onFinished.isBound() )
				_onFinished();
			return;
		}

		auto it = _mapNodes.find( nodeId );
		if ( it == _mapNodes.end() )
		{
			SW_LOG_WARNING( "[DialogueRunnerComponent] Node %# not found in graph.", nodeId );
			_state = DialogueRunnerState::Finished;
			if ( _onFinished.isBound() )
				_onFinished();
			return;
		}

		_currentNodeId			= nodeId;
		const RuntimeNode& node = it->second;

		if ( node._type == "Start" )
		{
			executeNode( node._nextDefaultNodeId );
		}
		else if ( node._type == "Dialogue" )
		{
			_state			= DialogueRunnerState::ShowingDialogue;
			_currentSpeaker = node._speaker;
			_currentText	= node._text;
			_listCurrentChoices.clear();

			if ( _onLine.isBound() )
				_onLine( _currentSpeaker, _currentText );
		}
		else if ( node._type == "Choice" )
		{
			_state				= DialogueRunnerState::WaitingForChoice;
			_currentSpeaker		= node._speaker;
			_currentText		= node._text;
			_listCurrentChoices = node._listChoices;

			if ( _onChoices.isBound() )
				_onChoices( _listCurrentChoices );
		}
		else if ( node._type == "Branch" )
		{
			const bool	bConditionMet = evaluateCondition( node._condition );
			const int32 nextId		  = bConditionMet ? node._trueNodeId : node._falseNodeId;
			executeNode( nextId > 0 ? nextId : node._nextDefaultNodeId );
		}
		else if ( node._type == "Action" )
		{
			executeAction( node._action );
			executeNode( node._nextDefaultNodeId );
		}
		else if ( node._type == "End" )
		{
			_state = DialogueRunnerState::Finished;
			if ( _onFinished.isBound() )
				_onFinished();
		}
	}
} // namespace sw
