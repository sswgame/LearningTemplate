#include "pch.h"

#include "GameFramework/Transition/GameModeStateMachine.h"

#include "Core/Log/Logger.h"

namespace sw
{
	SW_LOG_CALLER( "GameModeStateMachine" );

	GameModeStateMachine::GameModeStateMachine()
		: _currentMode{}
		, _previousMode{}
		, _mapHandler{}
		, _onModeChanged{}
	{
	}

	void GameModeStateMachine::registerHandler( const hashed_string& mode, shared_ptr<IGameModeHandler> pHandler )
	{
		if ( pHandler == nullptr )
		{
			unregisterHandler( mode );
			return;
		}

		_mapHandler[mode] = std::move( pHandler );
	}

	void GameModeStateMachine::unregisterHandler( const hashed_string& mode )
	{
		auto it = _mapHandler.find( mode );
		if ( it != _mapHandler.end() )
		{
			if ( _currentMode == mode )
			{
				it->second->onExit( GameModes::none() );
				_currentMode = {};
			}
			_mapHandler.erase( it );
		}
	}

	bool GameModeStateMachine::transitionTo( const hashed_string& newMode )
	{
		if ( _currentMode == newMode )
			return true;

		if ( _bIsTransitioning )
		{
			SW_LOG_WARNING( "Re-entrant mode transition to %# ignored while transitioning.", newMode.c_str() );
			return false;
		}

		_bIsTransitioning = true;
		struct TransitionGuard
		{
			bool& _flag;
			~TransitionGuard() { _flag = false; }
		} guard{ _bIsTransitioning };

		const hashed_string oldMode = _currentMode;

		// 1) Exit previous mode handler
		auto oldIt = _mapHandler.find( oldMode );
		if ( oldIt != _mapHandler.end() && oldIt->second != nullptr )
		{
			oldIt->second->onExit( newMode );
		}

		_previousMode = oldMode;
		_currentMode  = newMode;

		// 2) Enter new mode handler
		auto newIt = _mapHandler.find( newMode );
		if ( newIt != _mapHandler.end() && newIt->second != nullptr )
		{
			newIt->second->onEnter( oldMode );
		}

		// 3) Broadcast notification
		if ( _onModeChanged.isBound() )
		{
			_onModeChanged( oldMode, newMode );
		}

		SW_LOG_INFO( "Mode transitioned: %# -> %#", oldMode.c_str(), newMode.c_str() );
		return true;
	}

	void GameModeStateMachine::update( float32 deltaTime )
	{
		auto it = _mapHandler.find( _currentMode );
		if ( it != _mapHandler.end() && it->second != nullptr )
		{
			it->second->onUpdate( deltaTime );
		}
	}

	void GameModeStateMachine::reset()
	{
		if ( _currentMode.empty() == false )
		{
			auto it = _mapHandler.find( _currentMode );
			if ( it != _mapHandler.end() && it->second != nullptr )
			{
				it->second->onExit( GameModes::none() );
			}
		}

		_previousMode = _currentMode;
		_currentMode  = {};
	}

	IGameModeHandler* GameModeStateMachine::getCurrentHandler() const
	{
		auto it = _mapHandler.find( _currentMode );
		if ( it != _mapHandler.end() )
			return it->second.get();
		return nullptr;
	}
} // namespace sw
