#include "pch.h"

#include "GameFramework/Base/Character/Control/Controller/RemoteControllerComponent.h"

#include "GameFramework/Base/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    RemoteControllerComponent::RemoteControllerComponent()
        : _received{}
        , _lastIntent{}
        , _nextInputTick{ 0 }
        , _missingTickCount{ 0 }
        , _bHasLastIntent{ false }
    {
        _received.initialize( kReceiveCapacity );
    }

    void RemoteControllerComponent::receiveIntent( uint32 tick, const ControlIntent& intent )
    {
        if ( tick < _nextInputTick )
            return; // 이미 지난 틱이다 — 되풀이로 메웠다
        _received.acquire( tick ) = intent;
    }

    void RemoteControllerComponent::clearReceived()
    {
        _received.reset();
        _lastIntent     = ControlIntent{};
        _bHasLastIntent = false;
    }

    void RemoteControllerComponent::produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent )
    {
        (void)context;
        (void)pawn;
        const uint32         tick      = _nextInputTick++;
        const ControlIntent* pReceived = _received.find( tick );
        if ( pReceived != nullptr )
        {
            outIntent                    = *pReceived;
            _lastIntent                  = *pReceived;
            _lastIntent._buttonTriggered = 0;
            _bHasLastIntent              = true;
            setControlRotation( pReceived->_controlYaw, pReceived->_controlPitch );
            _received.remove( tick );
            return;
        }
        // 못 받은 틱 — 마지막 의도를 되풀이한다(받은 적이 없으면 서 있는다).
        ++_missingTickCount;
        if ( _bHasLastIntent == false )
            return;
        outIntent = _lastIntent;
        setControlRotation( _lastIntent._controlYaw, _lastIntent._controlPitch );
    }
} // namespace sw
