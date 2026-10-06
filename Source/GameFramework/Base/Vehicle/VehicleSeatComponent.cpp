#include "pch.h"

#include "GameFramework/Base/Vehicle/VehicleSeatComponent.h"

#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Control/ControlSystem.h"
#include "GameFramework/Base/Control/PawnComponent.h"
#include "GameFramework/Base/Vehicle/MountUtil.h"

namespace sw
{
    VehicleSeatComponent::VehicleSeatComponent()
        : _socketName{}
        , _riderPoseParameter{}
        , _seatOffset{ 0.0f, 1.0f, 0.0f }
        , _exitOffset{ -1.5f, 0.0f, 0.0f }
        , _enterDistance{ 3.0f }
        , _bDriverSeat{ true }
        , _occupant{}
        , _occupantController{}
        , _previousVehicleController{}
    {
    }

    void VehicleSeatComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<VehicleSeatComponent>( this ); // 빈 좌석 찾기 · 탑승자의 좌석 찾기가 씬을 훑지 않는다
    }

    void VehicleSeatComponent::onUnregister( GameObjectManager& manager )
    {
        // 탈것이 사라진다 — 앉은 이는 지금 자리에서 내린다. 조종 시스템이 없으면 씬을 비우는 중이다(시스템이 먼저 떨어진다) — 탑승자도 사라지므로 끈만 버린다.
        const bool     bClearing = ControlSystem::find( manager ) == nullptr;
        GameObject*    pOccupant = _occupant.isValid() && bClearing == false ? manager.resolveGameObject( _occupant ) : nullptr;
        PawnComponent* pRider    = pOccupant != nullptr ? pOccupant->getComponent<PawnComponent>() : nullptr;
        if ( pRider != nullptr )
            (void)MountUtil::dismount( *pRider, true );
        _occupant                  = GameObjectHandle{};
        _occupantController        = ComponentHandle{};
        _previousVehicleController = ComponentHandle{};
        manager.getComponentRegistry().remove<VehicleSeatComponent>( this );
        Component::onUnregister( manager );
    }
} // namespace sw
