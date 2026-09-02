#include "pch.h"

#include "Engine/Input/Devices/GamepadDevice.h"

#include "Core/String/StringUtil.h"

#include "Engine/Input/GamepadButtons.h"

namespace sw
{
    namespace
    {
        struct GamepadDeviceInternal
        {
            struct GamepadNameEntry
            {
                const utf8*   _pName;
                GamepadButton _button;
            };

            static constexpr GamepadNameEntry kArrGamepadNames[] = {
                {            "A",             GamepadButton::A},
                {            "B",             GamepadButton::B},
                {            "X",             GamepadButton::X},
                {            "Y",             GamepadButton::Y},
                {       "DPadUp",        GamepadButton::DPadUp},
                {     "DPadDown",      GamepadButton::DPadDown},
                {     "DPadLeft",      GamepadButton::DPadLeft},
                {    "DPadRight",     GamepadButton::DPadRight},
                {        "Start",         GamepadButton::Start},
                {         "Back",          GamepadButton::Back},
                { "LeftShoulder",  GamepadButton::LeftShoulder},
                {"RightShoulder", GamepadButton::RightShoulder},
                {    "LeftThumb",     GamepadButton::LeftThumb},
                {   "RightThumb",    GamepadButton::RightThumb},
            };
        };
    } // namespace

    GamepadButton GamepadButtons::fromName( string_view name )
    {
        if ( name.empty() )
            return GamepadButton::Count;
        for ( const GamepadDeviceInternal::GamepadNameEntry& entry : GamepadDeviceInternal::kArrGamepadNames )
        {
            if ( StringUtil::equals( name, entry._pName, true ) )
                return entry._button;
        }
        return GamepadButton::Count;
    }

    const utf8* GamepadButtons::toName( GamepadButton button )
    {
        for ( const GamepadDeviceInternal::GamepadNameEntry& entry : GamepadDeviceInternal::kArrGamepadNames )
        {
            if ( entry._button == button )
                return entry._pName;
        }
        return nullptr;
    }

    GamepadDevice::GamepadDevice( uint32 deviceIndex )
        : _onConnectionChanged{}
        , _deviceIndex{ deviceIndex }
        , _buttonMask{ 0 }
        , _prevButtonMask{ 0 }
        , _leftStickX{ 0.0f }
        , _leftStickY{ 0.0f }
        , _rightStickX{ 0.0f }
        , _rightStickY{ 0.0f }
        , _leftTrigger{ 0.0f }
        , _rightTrigger{ 0.0f }
        , _prevLeftTrigger{ 0.0f }
        , _prevRightTrigger{ 0.0f }
        , _leftMotorSpeed{ 0.0f }
        , _rightMotorSpeed{ 0.0f }
        , _vibrationDurationTimer{ 0.0f }
        , _triggerDeadzone{ 0.05f }
        , _bTimedVibrationActive{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void GamepadDevice::onFrameBegin( float32 deltaTime )
    {
        _prevButtonMask   = _buttonMask;
        _prevLeftTrigger  = _leftTrigger;
        _prevRightTrigger = _rightTrigger;

        if ( _bTimedVibrationActive == SW_TRUE )
        {
            _vibrationDurationTimer -= deltaTime;
            if ( _vibrationDurationTimer <= 0.0f )
            {
                stopVibration();
            }
        }
    }

    void GamepadDevice::onFrameEnd()
    {
    }

    void GamepadDevice::resetState()
    {
        _buttonMask       = 0;
        _prevButtonMask   = 0;
        _leftStickX       = 0.0f;
        _leftStickY       = 0.0f;
        _rightStickX      = 0.0f;
        _rightStickY      = 0.0f;
        _leftTrigger      = 0.0f;
        _rightTrigger     = 0.0f;
        _prevLeftTrigger  = 0.0f;
        _prevRightTrigger = 0.0f;
        stopVibration();
    }

    bool GamepadDevice::isControlDown( uint16 controlIndex ) const
    {
        if ( controlIndex == 100 ) // Left Trigger
            return isLeftTriggerDown();
        if ( controlIndex == 101 ) // Right Trigger
            return isRightTriggerDown();
        if ( controlIndex >= static_cast<uint16>( GamepadButton::Count ) )
            return false;
        return isButtonDown( static_cast<GamepadButton>( controlIndex ) );
    }

    bool GamepadDevice::wasControlPressed( uint16 controlIndex ) const
    {
        if ( controlIndex == 100 )
            return ( _leftTrigger >= 0.5f ) && ( _prevLeftTrigger < 0.5f );
        if ( controlIndex == 101 )
            return ( _rightTrigger >= 0.5f ) && ( _prevRightTrigger < 0.5f );
        if ( controlIndex >= static_cast<uint16>( GamepadButton::Count ) )
            return false;
        return wasButtonPressed( static_cast<GamepadButton>( controlIndex ) );
    }

    bool GamepadDevice::wasControlReleased( uint16 controlIndex ) const
    {
        if ( controlIndex == 100 )
            return ( _leftTrigger < 0.5f ) && ( _prevLeftTrigger >= 0.5f );
        if ( controlIndex == 101 )
            return ( _rightTrigger < 0.5f ) && ( _prevRightTrigger >= 0.5f );
        if ( controlIndex >= static_cast<uint16>( GamepadButton::Count ) )
            return false;
        return wasButtonReleased( static_cast<GamepadButton>( controlIndex ) );
    }

    float32 GamepadDevice::getControlValue( uint16 controlIndex ) const
    {
        if ( controlIndex == 100 ) // Left Trigger
            return _leftTrigger;
        if ( controlIndex == 101 ) // Right Trigger
            return _rightTrigger;
        if ( controlIndex == 102 ) // Left Stick X
            return _leftStickX;
        if ( controlIndex == 103 ) // Left Stick Y
            return _leftStickY;
        if ( controlIndex == 104 ) // Right Stick X
            return _rightStickX;
        if ( controlIndex == 105 ) // Right Stick Y
            return _rightStickY;
        return isControlDown( controlIndex ) ? 1.0f : 0.0f;
    }

    bool GamepadDevice::isButtonDown( GamepadButton button ) const
    {
        const uint32 bit = 1u << static_cast<uint32>( button );
        return ( _buttonMask & bit ) != 0;
    }

    bool GamepadDevice::wasButtonPressed( GamepadButton button ) const
    {
        const uint32 bit = 1u << static_cast<uint32>( button );
        return ( ( _buttonMask & bit ) != 0 ) && ( ( _prevButtonMask & bit ) == 0 );
    }

    bool GamepadDevice::wasButtonReleased( GamepadButton button ) const
    {
        const uint32 bit = 1u << static_cast<uint32>( button );
        return ( ( _buttonMask & bit ) == 0 ) && ( ( _prevButtonMask & bit ) != 0 );
    }

    void GamepadDevice::setButtonDown( GamepadButton button, bool bDown )
    {
        const size_t index = static_cast<size_t>( button );
        if ( index >= static_cast<size_t>( GamepadButton::Count ) )
            return;

        const uint32 bit = 1u << index;
        if ( bDown )
            _buttonMask |= bit;
        else
            _buttonMask &= ~bit;
    }

    void GamepadDevice::setAxis( uint16 axisIndex, float32 value )
    {
        switch ( axisIndex )
        {
            case 0:
                _leftStickX = value;
                break;
            case 1:
                _leftStickY = value;
                break;
            case 2:
                _rightStickX = value;
                break;
            case 3:
                _rightStickY = value;
                break;
            case 4:
                _leftTrigger = value < _triggerDeadzone ? 0.0f : value;
                break;
            case 5:
                _rightTrigger = value < _triggerDeadzone ? 0.0f : value;
                break;
            default:
                break;
        }
    }
} // namespace sw
