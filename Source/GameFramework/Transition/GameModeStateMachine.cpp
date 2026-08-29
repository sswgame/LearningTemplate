#include "pch.h"

#include "GameFramework/Transition/GameModeStateMachine.h"

#include "Core/Log/Logger.h"

namespace sw
{
	SW_LOG_CALLER( "GameModeStateMachine" );

	GameModeStateMachine::GameModeStateMachine()
		: _currentMode{ GamePlayMode::None }
		, _previousMode{ GamePlayMode::None }
		, _mapHandler{}
		, _onModeChanged{}
	{
	}

	void GameModeStateMachine::registerHandler( GamePlayMode mode, shared_ptr<IGameModeHandler> pHandler )
	{
		if ( pHandler == nullptr )
		{
			unregisterHandler( mode );
			return;
		}

		_mapHandler[mode] = std::move( pHandler );
	}

	void GameModeStateMachine::unregisterHandler( GamePlayMode mode )
	{
		auto it = _mapHandler.find( mode );
		if ( it != _mapHandler.end() )
		{
			if ( _currentMode == mode )
			{
				it->second->onExit( GamePlayMode::None );
				_currentMode = GamePlayMode::None;
			}
			_mapHandler.erase( it );
		}
	}

	bool GameModeStateMachine::transitionTo( GamePlayMode newMode )
	{
		if ( _currentMode == newMode )
			return true;

		const GamePlayMode oldMode = _currentMode;

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

		SW_LOG_INFO( "Mode transitioned: %# -> %#", static_cast<uint32>( oldMode ), static_cast<uint32>( newMode ) );
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
		if ( _currentMode != GamePlayMode::None )
		{
			auto it = _mapHandler.find( _currentMode );
			if ( it != _mapHandler.end() && it->second != nullptr )
			{
				it->second->onExit( GamePlayMode::None );
			}
		}

		_previousMode = _currentMode;
		_currentMode  = GamePlayMode::None;
	}

	IGameModeHandler* GameModeStateMachine::getCurrentHandler() const
	{
		auto it = _mapHandler.find( _currentMode );
		if ( it != _mapHandler.end() )
			return it->second.get();
		return nullptr;
	}
} // namespace sw
