/**
 * @file PhysicsPoseStore.cpp
 * @brief 강체 칸 저장소 서비스의 수명과 등록입니다.
 */
#include "pch.h"

#include "Engine/Physics/Scene/PhysicsPoseStore.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    PhysicsPoseStore::PhysicsPoseStore()
        : _store3D{}
        , _store2D{}
        , _commandStore3D{}
        , _commandStore2D{}
        , _vehicleInputStore{}
    {
    }

    PhysicsPoseStore::~PhysicsPoseStore()
    {
        // 서비스 범위가 내려갈 때 칸이 남았으면 그 강체 · 컨트롤러 · 차가 새었다(`SceneTransformStorage` 와 같은 경고). 페이지는 그래도 놓는다.
        const uint32 liveSlotCount = _store3D.getLiveSlotCount() + _store2D.getLiveSlotCount() + _commandStore3D.getLiveSlotCount() + _commandStore2D.getLiveSlotCount() + _vehicleInputStore.getLiveSlotCount();
        if ( liveSlotCount != 0 )
            SW_LOG_WARNING_ID( "PhysicsPoseStore.LiveSlotsAtShutdown", "Physics pose store still has %# live slots at shutdown - a rigid body, character controller or vehicle component leaked",
                               liveSlotCount );
    }
} // namespace sw

namespace sw
{
    // 강체 칸 저장소 — 트랜스폼 칸 저장소와 같은 서비스 범위라 엔진 범위의 어느 컴포넌트보다 나중에 내려간다.
    SW_ENGINE_SERVICE_SUBSYSTEM( PhysicsPoseStore, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( PhysicsPoseStore ) );
} // namespace sw
