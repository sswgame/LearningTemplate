#include "pch.h"

#include "GameFramework/Base/Control/RemoteControllerComponent.h"

#include "Core/Network/BitStream.h"
#include "Core/Network/Replication/NetInputWindow.h"

#include "GameFramework/Base/Control/PawnComponent.h"

namespace sw
{
    SW_LOG_CALLER( "RemoteControllerComponent" );
} // namespace sw

namespace sw
{
    RemoteControllerComponent::RemoteControllerComponent()
        : _lastIntent{}
        , _pReceiveBuffer{ nullptr }
        , _nextInputTick{ 0 }
        , _missingTickCount{ 0 }
        , _bHasLastIntent{ false }
    {
    }

    void RemoteControllerComponent::produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent )
    {
        (void)context;
        (void)pawn;
        const uint32         tick   = _nextInputTick++;
        const NetInputEntry* pEntry = _pReceiveBuffer != nullptr ? _pReceiveBuffer->find( tick ) : nullptr;
        if ( pEntry != nullptr )
        {
            BitReader     reader( pEntry->_bytes.data(), static_cast<int32>( pEntry->_bytes.size() ) );
            ControlIntent received;
            if ( received.read( reader ) )
            {
                outIntent                    = received;
                _lastIntent                  = received;
                _lastIntent._buttonTriggered = 0;
                _bHasLastIntent              = true;
                setControlRotation( received._controlYaw, received._controlPitch );
                return;
            }
            SW_LOG_WARNING( "Remote control intent for tick %# is malformed (%# byte(s)) - repeating the last one", tick, static_cast<uint32>( pEntry->_bytes.size() ) );
        }
        // 못 받은 틱 — 마지막 의도를 되풀이한다(받은 적이 없으면 서 있는다).
        ++_missingTickCount;
        if ( _bHasLastIntent == false )
            return;
        outIntent = _lastIntent;
        setControlRotation( _lastIntent._controlYaw, _lastIntent._controlPitch );
    }
} // namespace sw
