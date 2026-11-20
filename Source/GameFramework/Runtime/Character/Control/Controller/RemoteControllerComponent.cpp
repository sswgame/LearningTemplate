#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

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

    RemoteControllerComponent* RemoteControllerComponent::spawnForConnection( GameObjectManager& manager, const hashed_string& objectName, int32 connectionID, PawnComponent& pawn )
    {
        if ( connectionID < 0 )
        {
            SW_LOG_ERROR_ID( "RemoteController.SpawnNegativeConnection", "Remote controller '%#' not spawned: connection id %# is not a connection", objectName.c_str(), connectionID );
            return nullptr;
        }
        GameObject*                pObject = manager.createGameObject( objectName );
        RemoteControllerComponent* pRemote = pObject != nullptr ? pObject->addComponent<RemoteControllerComponent>() : nullptr;
        if ( pRemote == nullptr )
        {
            SW_LOG_ERROR_ID( "RemoteController.SpawnFailed", "Remote controller '%#' could not be created", objectName.c_str() );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            return nullptr;
        }
        pRemote->setInputConnection( connectionID );
        pRemote->possess( pawn );
        return pRemote;
    }

    void RemoteControllerComponent::onBeginPlay()
    {
        ControllerComponent::onBeginPlay();
        if ( getInputPeer() == 0 )
            SW_LOG_ERROR_ID( "RemoteController.NoConnection", "Remote controller on '%#' has no connection - spawn it with RemoteControllerComponent::spawnForConnection", getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
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
        if ( _received.hasNewest() && _received.getNewestTick() >= _nextInputTick )
        {
            const uint32 newestTick = _received.getNewestTick();
            const uint32 firstTick  = MathUtil::max( _nextInputTick, _received.computeOldestTick() );
            _missingTickCount += firstTick - _nextInputTick; // 고리에서 밀려난 틱
            _nextInputTick = newestTick + 1u;
            ControlIntent merged{};
            uint32        triggered    = 0;
            bool          bTriggerLook = false;
            float32       triggerYaw   = 0.0f;
            float32       triggerPitch = 0.0f;
            bool          bRead        = false;
            for ( uint32 tick = firstTick; tick <= newestTick; ++tick )
            {
                const ControlIntent* pReceived = _received.find( tick );
                if ( pReceived == nullptr )
                {
                    ++_missingTickCount;
                    continue;
                }
                merged = *pReceived;
                triggered |= pReceived->_buttonTriggered;
                if ( pReceived->_bTriggerLook )
                {
                    bTriggerLook = true;
                    triggerYaw   = pReceived->_triggerYaw;
                    triggerPitch = pReceived->_triggerPitch;
                }
                bRead = true;
                _received.remove( tick );
            }
            if ( bRead )
            {
                _lastIntent                  = merged;
                _lastIntent._buttonTriggered = 0;
                _lastIntent._bTriggerLook    = false;
                _lastIntent._triggerYaw      = 0.0f;
                _lastIntent._triggerPitch    = 0.0f;
                _bHasLastIntent              = true;
                merged._buttonTriggered      = triggered;
                merged._bTriggerLook         = bTriggerLook;
                merged._triggerYaw           = triggerYaw;
                merged._triggerPitch         = triggerPitch;
                outIntent                    = merged;
                setControlRotation( merged._controlYaw, merged._controlPitch );
                return;
            }
        }
        // 이 프레임에 새 틱이 없다 — 마지막 의도를 되풀이한다(받은 적이 없으면 서 있는다).
        if ( _bHasLastIntent == false )
            return;
        outIntent = _lastIntent;
        setControlRotation( _lastIntent._controlYaw, _lastIntent._controlPitch );
    }
} // namespace sw
