#include "pch.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/ControlSystem.h"
#include "GameFramework/Base/Actor/Control/Controller/ControllerComponent.h"
#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"

namespace sw
{
    namespace
    {
        struct PawnComponentInternal
        {
            /** @brief 조종 회전 오프셋 고정소수점의 한 라디안입니다(0.00001 rad 간격 — 프레임 사이에 쌓이는 반동은 수 라디안을 넘지 않는다). */
            static constexpr float32 kRotationOffsetUnitsPerRadian = 100000.0f;

            static int32 findName( const vector<hashed_string>& listName, const hashed_string& name )
            {
                for ( size_t nameIndex = 0; nameIndex < listName.size(); ++nameIndex )
                {
                    if ( listName[nameIndex] == name )
                        return static_cast<int32>( nameIndex );
                }
                return -1;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PawnComponent::PawnComponent()
        : _listButton{}
        , _listAnalog{}
        , _moveAction{ "Move" }
        , _lookAction{ "Look" }
        , _upAction{}
        , _inputLayer{}
        , _aiControllerPrefab{}
        , _maxPitch{ 1.4f }
        , _autoPossess{ PawnAutoPossess::None }
        , _bLockMouse{ false }
        , _intent{}
        , _controller{}
        , _yawOffsetUnits{ 0 }
        , _pitchOffsetUnits{ 0 }
        , _requestedYawUnits{ 0 }
        , _requestedPitchUnits{ 0 }
        , _bRotationRequested{ false }
        , _inputPeer{ 0 }
        , _bAutoPossessDone{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void PawnComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<PawnComponent>( this );
        (void)ControlSystem::ensureFor( manager );
    }

    void PawnComponent::onUnregister( GameObjectManager& manager )
    {
        // 시스템이 없으면 씬을 비우는 중이다(시스템이 먼저 떨어진다) — 조종자도 곧 지워지므로 끈만 버린다.
        ControlSystem* pSystem = ControlSystem::find( manager );
        if ( pSystem != nullptr && _controller.isValid() )
        {
            ControllerComponent* pController = static_cast<ControllerComponent*>( manager.resolveComponent( _controller ) );
            if ( pController != nullptr )
            {
                pController->unpossess();
                // 자동 빙의가 이 폰을 위해 세운 조종자는 폰과 함께 간다(지연 삭제 — 지금 지우는 중인 목록을 건드리지 않는다).
                if ( pController->isSpawnedForPawn() && pController->getOwner() != nullptr )
                    manager.destroyObject( pController->getOwner() );
            }
        }
        _controller = ComponentHandle{};
        manager.getComponentRegistry().remove<PawnComponent>( this );
        ControlSystem::releaseIfUnused( manager );
        Component::onUnregister( manager );
    }

    int32 PawnComponent::findButton( const hashed_string& name ) const
    {
        return PawnComponentInternal::findName( _listButton, name );
    }

    int32 PawnComponent::findAnalog( const hashed_string& name ) const
    {
        return PawnComponentInternal::findName( _listAnalog, name );
    }

    void PawnComponent::addControlRotationOffset( float32 deltaYaw, float32 deltaPitch )
    {
        const float32 scale = PawnComponentInternal::kRotationOffsetUnitsPerRadian;
        _yawOffsetUnits.fetch_add( static_cast<int32>( deltaYaw * scale ), std::memory_order_relaxed );
        _pitchOffsetUnits.fetch_add( static_cast<int32>( deltaPitch * scale ), std::memory_order_relaxed );
    }

    void PawnComponent::requestControlRotation( float32 yaw, float32 pitch )
    {
        const float32 scale = PawnComponentInternal::kRotationOffsetUnitsPerRadian;
        _requestedYawUnits.store( static_cast<int32>( MathUtil::wrapAngle( yaw ) * scale ), std::memory_order_relaxed );
        _requestedPitchUnits.store( static_cast<int32>( pitch * scale ), std::memory_order_relaxed );
        _yawOffsetUnits.store( 0, std::memory_order_relaxed );
        _pitchOffsetUnits.store( 0, std::memory_order_relaxed );
        _bRotationRequested.store( true, std::memory_order_release );
    }

    void PawnComponent::clearMotion()
    {
        const float32 yaw     = _intent._controlYaw;
        const float32 pitch   = _intent._controlPitch;
        _intent               = ControlIntent{};
        _intent._controlYaw   = yaw;
        _intent._controlPitch = pitch;
    }

    float2 PawnComponent::consumeControlRotationOffset()
    {
        const float32 scale      = PawnComponentInternal::kRotationOffsetUnitsPerRadian;
        const int32   yawUnits   = _yawOffsetUnits.exchange( 0, std::memory_order_relaxed );
        const int32   pitchUnits = _pitchOffsetUnits.exchange( 0, std::memory_order_relaxed );
        return float2{ static_cast<float32>( yawUnits ) / scale, static_cast<float32>( pitchUnits ) / scale };
    }

    bool PawnComponent::consumeControlRotationRequest( float2& outRotation )
    {
        if ( _bRotationRequested.exchange( false, std::memory_order_acquire ) == false )
            return false;
        const float32 scale = PawnComponentInternal::kRotationOffsetUnitsPerRadian;
        outRotation         = float2{ static_cast<float32>( _requestedYawUnits.load( std::memory_order_relaxed ) ) / scale,
                              static_cast<float32>( _requestedPitchUnits.load( std::memory_order_relaxed ) ) / scale };
        return true;
    }

    void PawnComponent::applyPendingControlRotation()
    {
        float2 requested{};
        if ( consumeControlRotationRequest( requested ) )
        {
            _intent._controlYaw   = requested._x;
            _intent._controlPitch = requested._y;
        }
        const float2 offset   = consumeControlRotationOffset();
        _intent._controlYaw   = MathUtil::wrapAngle( _intent._controlYaw + offset._x );
        _intent._controlPitch = MathUtil::clamp( _intent._controlPitch + offset._y, -_maxPitch, _maxPitch );
    }
} // namespace sw
