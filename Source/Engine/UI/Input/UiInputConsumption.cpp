#include "pch.h"

#include "Engine/UI/Input/UiInputConsumption.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"

namespace sw
{
    namespace
    {
        struct UiInputConsumptionInternal
        {
            /** @brief 바인딩 하나가 "누름" 으로 쓰는 슬롯 자리입니다(조합 키는 트리거 키만 — 수정 키를 먹으면 달리기 Shift 가 묶인다). */
            static uint32 collectSlotIndices( const ActionBinding& binding, uint32 ( &outArrIndex )[4] )
            {
                switch ( binding._kind )
                {
                    case BindingKind::SingleSlot:
                    case BindingKind::Shortcut:
                    {
                        outArrIndex[0] = 0;
                        return 1;
                    }
                    case BindingKind::Axis1DComposite:
                    {
                        outArrIndex[0] = 0;
                        outArrIndex[1] = 1;
                        return 2;
                    }
                    case BindingKind::Vector2DComposite:
                    {
                        outArrIndex[0] = 0;
                        outArrIndex[1] = 1;
                        outArrIndex[2] = 2;
                        outArrIndex[3] = 3;
                        return 4;
                    }
                    case BindingKind::Chord:
                    {
                        outArrIndex[0] = 1;
                        return 1;
                    }
                    default:
                    {
                        return 0;
                    }
                }
            }

            static bool isSlotDown( const InputManager& input, const InputSlot& slot )
            {
                switch ( slot._deviceKind )
                {
                    case InputDeviceKind::Keyboard:
                    {
                        return input.getKeyboard() != nullptr && input.getKeyboard()->isKeyDown( static_cast<Key>( slot._controlIndex ) );
                    }
                    case InputDeviceKind::Mouse:
                    {
                        return input.getMouse() != nullptr && input.getMouse()->isButtonDown( static_cast<MouseButton>( slot._controlIndex ) );
                    }
                    case InputDeviceKind::Gamepad:
                    {
                        const GamepadDevice* pPad = input.getGamepad( slot._deviceIndex );
                        return pPad != nullptr && pPad->isConnected() && pPad->isButtonDown( static_cast<GamepadButton>( slot._controlIndex ) );
                    }
                    default:
                    {
                        return false;
                    }
                }
            }

            static uint8 makeStickBit( const ActionBinding& binding )
            {
                const uint32 bitIndex = static_cast<uint32>( binding._deviceIndex ) * 2u + ( binding._stick == GamepadStick::Right ? 1u : 0u );
                return bitIndex < 8u ? static_cast<uint8>( 1u << bitIndex ) : uint8{ 0 };
            }

            static float32 computeStickMagnitude( const InputManager& input, uint8 padIndex, GamepadStick stick )
            {
                const GamepadDevice* pPad = input.getGamepad( padIndex );
                if ( pPad == nullptr || pPad->isConnected() == false )
                    return 0.0f;
                const float2 value = stick == GamepadStick::Right ? pPad->getRightStick() : pPad->getLeftStick();
                return MathUtil::sqrt( value._x * value._x + value._y * value._y );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UiInputConsumption::UiInputConsumption()
        : _listConsumedSlot{}
        , _consumedStickMask{ 0 }
    {
    }

    void UiInputConsumption::update( const InputManager& input )
    {
        // 뗀 프레임은 남기고(뗌으로 발화하는 행동도 막는다) 그 다음 프레임에 뺀다. 그 사이 다시 눌렀으면 새 누름이다 — 뺀다.
        for ( size_t index = _listConsumedSlot.size(); index > 0; --index )
        {
            ConsumedSlot& entry = _listConsumedSlot[index - 1];
            const bool    bDown = UiInputConsumptionInternal::isSlotDown( input, entry._slot );
            if ( entry._bReleased == SW_TRUE )
                _listConsumedSlot.erase( _listConsumedSlot.begin() + static_cast<ptrdiff_t>( index - 1 ) );
            else if ( bDown == false )
                entry._bReleased = SW_TRUE;
        }
        for ( uint32 bitIndex = 0; bitIndex < 8u; ++bitIndex )
        {
            const uint8 bit = static_cast<uint8>( 1u << bitIndex );
            if ( ( _consumedStickMask & bit ) == 0 )
                continue;
            const GamepadStick stick = ( bitIndex % 2u ) == 1u ? GamepadStick::Right : GamepadStick::Left;
            if ( UiInputConsumptionInternal::computeStickMagnitude( input, static_cast<uint8>( bitIndex / 2u ), stick ) < kStickReleaseMagnitude )
                _consumedStickMask = static_cast<uint8>( _consumedStickMask & ~bit );
        }
    }

    void UiInputConsumption::consumeAction( const InputMap& inputMap, const InputManager& input, const hashed_string& action )
    {
        const uint32 bindingCount = inputMap.getBindingCount( action );
        for ( uint32 bindIndex = 0; bindIndex < bindingCount; ++bindIndex )
        {
            const ActionBinding* pBinding = inputMap.getBinding( action, bindIndex );
            if ( pBinding == nullptr )
                continue;
            if ( pBinding->_kind == BindingKind::GamepadStick2D )
            {
                if ( UiInputConsumptionInternal::computeStickMagnitude( input, pBinding->_deviceIndex, pBinding->_stick ) >= kStickReleaseMagnitude )
                    _consumedStickMask = static_cast<uint8>( _consumedStickMask | UiInputConsumptionInternal::makeStickBit( *pBinding ) );
                continue;
            }
            uint32       arrIndex[4]{};
            const uint32 slotCount = UiInputConsumptionInternal::collectSlotIndices( *pBinding, arrIndex );
            for ( uint32 slotIndex = 0; slotIndex < slotCount; ++slotIndex )
            {
                const InputSlot& slot = pBinding->_arrSlot[arrIndex[slotIndex]];
                if ( UiInputConsumptionInternal::isSlotDown( input, slot ) )
                    addSlot( slot );
            }
        }
    }

    void UiInputConsumption::consumeMouseButton( MouseButton button )
    {
        addSlot( InputSlot::fromMouseButton( button ) );
    }

    bool UiInputConsumption::isActionConsumed( const InputMap& inputMap, const InputManager& input, const hashed_string& action ) const
    {
        if ( hasConsumedInput() == false )
            return false;
        bool         bSawConsumed = false;
        bool         bSawFree     = false;
        const uint32 bindingCount = inputMap.getBindingCount( action );
        for ( uint32 bindIndex = 0; bindIndex < bindingCount; ++bindIndex )
        {
            const ActionBinding* pBinding = inputMap.getBinding( action, bindIndex );
            if ( pBinding == nullptr )
                continue;
            if ( pBinding->_kind == BindingKind::GamepadStick2D )
            {
                const bool bConsumed = ( _consumedStickMask & UiInputConsumptionInternal::makeStickBit( *pBinding ) ) != 0;
                const bool bTilted   = UiInputConsumptionInternal::computeStickMagnitude( input, pBinding->_deviceIndex, pBinding->_stick ) >= kStickReleaseMagnitude;
                bSawConsumed         = bSawConsumed || bConsumed;
                bSawFree             = bSawFree || ( bTilted && bConsumed == false );
                continue;
            }
            uint32       arrIndex[4]{};
            const uint32 slotCount = UiInputConsumptionInternal::collectSlotIndices( *pBinding, arrIndex );
            for ( uint32 slotIndex = 0; slotIndex < slotCount; ++slotIndex )
            {
                const InputSlot& slot = pBinding->_arrSlot[arrIndex[slotIndex]];
                // 먹힌 슬롯은 뗀 프레임까지 "먹힌 쪽" 으로 센다. 먹히지 않은 슬롯은 눌려 있을 때만 "보이는 쪽".
                if ( isSlotConsumed( slot ) )
                    bSawConsumed = true;
                else if ( UiInputConsumptionInternal::isSlotDown( input, slot ) )
                    bSawFree = true;
            }
        }
        return bSawConsumed && bSawFree == false;
    }

    bool UiInputConsumption::isSlotConsumed( const InputSlot& slot ) const
    {
        for ( const ConsumedSlot& entry : _listConsumedSlot )
        {
            if ( entry._slot == slot )
                return true;
        }
        return false;
    }

    void UiInputConsumption::clear()
    {
        _listConsumedSlot.clear();
        _consumedStickMask = 0;
    }

    void UiInputConsumption::addSlot( const InputSlot& slot )
    {
        for ( ConsumedSlot& entry : _listConsumedSlot )
        {
            if ( entry._slot == slot )
            {
                entry._bReleased = SW_FALSE;
                return;
            }
        }
        _listConsumedSlot.push_back( ConsumedSlot{ slot, SW_FALSE } );
    }
} // namespace sw
