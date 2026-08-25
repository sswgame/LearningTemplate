#include "pch.h"

#include "GameFramework/Transition/GameModeStateMachine.h"

#include "Core/Log/Logger.h"

namespace sw
{
	GameModeStateMachine::GameModeStateMachine()
		: _currentMode{ GamePlayMode::None }
		, _previousMode{ GamePlayMode::None }
		, _mapHandlers{}
		, _onModeChanged{}
	{
	}

	void GameModeStateMachine::registerHandler( GamePlayMode mode, std::shared_ptr<IGameModeHandler> pHandler )
	{
		if ( pHandler == nullptr )
		{
			unregisterHandler( mode );
			return;
		}

		_mapHandlers[mode] = std::move( pHandler );
	}

	void GameModeStateMachine::unregisterHandler( GamePlayMode mode )
	{
		auto it = _mapHandlers.find( mode );
		if ( it != _mapHandlers.end() )
		{
			if ( _currentMode == mode )
			{
				it->second->onExit( GamePlayMode::None );
				_currentMode = GamePlayMode::None;
			}
			_mapHandlers.erase( it );
		}
	}

	bool GameModeStateMachine::transitionTo( GamePlayMode newMode )
	{
		if ( _currentMode == newMode )
			return true;

		const GamePlayMode oldMode = _currentMode;

		// 1) Exit previous mode handler
		auto oldIt = _mapHandlers.find( oldMode );
		if ( oldIt != _mapHandlers.end() && oldIt->second != nullptr )
		{
			oldIt->second->onExit( newMode );
		}

		_previousMode = oldMode;
		_currentMode  = newMode;

		// 2) Enter new mode handler
		auto newIt = _mapHandlers.find( newMode );
		if ( newIt != _mapHandlers.end() && newIt->second != nullptr )
		{
			newIt->second->onEnter( oldMode );
		}

		// 3) Broadcast notification
		if ( _onModeChanged.isBound() )
		{
			_onModeChanged( oldMode, newMode );
		}

		SW_LOG_INFO( "[GameModeStateMachine] Mode transitioned: %# -> %#", static_cast<uint32>( oldMode ), static_cast<uint32>( newMode ) );
		return true;
	}

	void GameModeStateMachine::update( float32 deltaTime )
	{
		auto it = _mapHandlers.find( _currentMode );
		if ( it != _mapHandlers.end() && it->second != nullptr )
		{
			it->second->onUpdate( deltaTime );
		}
	}

	void GameModeStateMachine::reset()
	{
		if ( _currentMode != GamePlayMode::None )
		{
			auto it = _mapHandlers.find( _currentMode );
			if ( it != _mapHandlers.end() && it->second != nullptr )
			{
				it->second->onExit( GamePlayMode::None );
			}
		}

		_previousMode = _currentMode;
		_currentMode  = GamePlayMode::None;
	}

	IGameModeHandler* GameModeStateMachine::getCurrentHandler() const
	{
		auto it = _mapHandlers.find( _currentMode );
		if ( it != _mapHandlers.end() )
			return it->second.get();
		return nullptr;
	}
} // namespace sw
