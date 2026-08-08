/**
 * @file ActionMap.cpp
 * @brief Named action → key/gamepad binding resolution.
 */
#include "ActionMap.h"

namespace sw
{
	ActionMap::ActionMap() = default;

	void ActionMap::clear()
	{
		_bindings.clear();
	}

	void ActionMap::bindDefaults()
	{
		clear();
		bind( Action::MoveUp, Key::W );
		bind( Action::MoveUp, Key::Up );
		bind( Action::MoveUp, GamepadButton::DPadUp );

		bind( Action::MoveDown, Key::S );
		bind( Action::MoveDown, Key::Down );
		bind( Action::MoveDown, GamepadButton::DPadDown );

		bind( Action::MoveLeft, Key::A );
		bind( Action::MoveLeft, Key::Left );
		bind( Action::MoveLeft, GamepadButton::DPadLeft );

		bind( Action::MoveRight, Key::D );
		bind( Action::MoveRight, Key::Right );
		bind( Action::MoveRight, GamepadButton::DPadRight );

		bind( Action::Interact, Key::E );
		bind( Action::Interact, Key::Z );
		bind( Action::Interact, GamepadButton::X ); // FaceLeft — avoid Confirm's A

		bind( Action::Confirm, Key::Enter );
		bind( Action::Confirm, Key::Space );
		bind( Action::Confirm, GamepadButton::A );

		bind( Action::Cancel, Key::Escape );
		bind( Action::Cancel, GamepadButton::B );

		bind( Action::FightMove0, Key::Digit1 );
		bind( Action::FightMove1, Key::Digit2 );
	}

	void ActionMap::bind( std::string_view action, Key key )
	{
		InputBinding binding{};
		binding._source = InputBindingSource::Key;
		binding._key	= key;
		_bindings[std::string( action )].push_back( binding );
	}

	void ActionMap::bind( std::string_view action, GamepadButton button )
	{
		InputBinding binding{};
		binding._source = InputBindingSource::GamepadButton;
		binding._button = button;
		_bindings[std::string( action )].push_back( binding );
	}

	bool ActionMap::evaluateDown( const InputBinding& binding ) const
	{
		switch ( binding._source )
		{
			case InputBindingSource::Key:
				return _input != nullptr && _input->isKeyDown( binding._key );
			case InputBindingSource::GamepadButton:
				return _gamepad != nullptr && _gamepad->isButtonDown( binding._button );
			default:
				return false;
		}
	}

	bool ActionMap::evaluatePressed( const InputBinding& binding ) const
	{
		switch ( binding._source )
		{
			case InputBindingSource::Key:
				return _input != nullptr && _input->wasKeyPressed( binding._key );
			case InputBindingSource::GamepadButton:
				return _gamepad != nullptr && _gamepad->wasButtonPressed( binding._button );
			default:
				return false;
		}
	}

	bool ActionMap::isActionDown( std::string_view action ) const
	{
		const auto it = _bindings.find( std::string( action ) );
		if ( it == _bindings.end() )
			return false;
		for ( const InputBinding& binding : it->second )
		{
			if ( evaluateDown( binding ) )
				return true;
		}
		return false;
	}

	bool ActionMap::wasActionPressed( std::string_view action ) const
	{
		const auto it = _bindings.find( std::string( action ) );
		if ( it == _bindings.end() )
			return false;
		for ( const InputBinding& binding : it->second )
		{
			if ( evaluatePressed( binding ) )
				return true;
		}
		return false;
	}
} // namespace sw
