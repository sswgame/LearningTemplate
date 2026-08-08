#pragma once
/**
 * @file ActionMap.h
 * @brief Named gameplay actions bound to keyboard keys and/or gamepad buttons.
 */
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Core/Input/GamepadXInput.h"
#include "Core/Input/InputManager.h"

namespace sw
{
	/** @brief Well-known default action names. */
	namespace Action
	{
		inline constexpr const utf8* MoveUp	  = "MoveUp";
		inline constexpr const utf8* MoveDown  = "MoveDown";
		inline constexpr const utf8* MoveLeft  = "MoveLeft";
		inline constexpr const utf8* MoveRight = "MoveRight";
		inline constexpr const utf8* Interact	= "Interact";
		inline constexpr const utf8* Confirm	= "Confirm";
		inline constexpr const utf8* Cancel		= "Cancel";
		inline constexpr const utf8* FightMove0 = "FightMove0";
		inline constexpr const utf8* FightMove1 = "FightMove1";
	} // namespace Action

	enum class InputBindingSource : uint8
	{
		Key = 0,
		GamepadButton
	};

	struct InputBinding
	{
		InputBindingSource _source = InputBindingSource::Key;
		Key				   _key	   = Key::Unknown;
		GamepadButton	   _button = GamepadButton::Count;
	};

	/**
	 * @class ActionMap
	 * @brief Resolves named actions against InputManager + optional GamepadXInput.
	 */
	class SW_API ActionMap
	{
	public:
		ActionMap();

		void setInputManager( InputManager* input ) { _input = input; }
		void setGamepad( GamepadXInput* gamepad ) { _gamepad = gamepad; }

		InputManager*  getInputManager() const { return _input; }
		GamepadXInput* getGamepad() const { return _gamepad; }

		/** @brief Clear bindings and install Move/Interact/Confirm/Cancel defaults. */
		void bindDefaults();
		void clear();

		void bind( std::string_view action, Key key );
		void bind( std::string_view action, GamepadButton button );

		bool isActionDown( std::string_view action ) const;
		bool wasActionPressed( std::string_view action ) const;

	private:
		bool evaluateDown( const InputBinding& binding ) const;
		bool evaluatePressed( const InputBinding& binding ) const;

		InputManager*								 _input	  = nullptr;
		GamepadXInput*								 _gamepad = nullptr;
		std::unordered_map<std::string, std::vector<InputBinding>> _bindings;
	};
} // namespace sw
