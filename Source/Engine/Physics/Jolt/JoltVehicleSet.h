/**
 * @file JoltVehicleSet.h
 * @brief Jolt 씬의 바퀴 차 묶음입니다. `VehicleConstraint` 와 `WheeledVehicleController`(엔진, 자동 변속, 디퍼렌셜), 원기둥 쓸기 바퀴 충돌을 씁니다.
 * @details 차는 구속이면서 스텝 리스너다(바퀴 충돌과 서스펜션은 스텝 앞에서 푼다). 만들 때 둘 다 넣고 지울 때 둘 다 뺀다. 씬이 차체 바디를 검증해 넘기고,
 *          차체를 지우면 그 차도 지운다(`destroyAttachedTo`). 같은 입력과 같은 스텝이면 같은 결과가 나온다(같은 실행 안에서의 Jolt 결정성).
 *          Jolt 헤더를 include 하므로 `Physics/Jolt` 밖에서는 보지 않는다.
 */
#pragma once
#include "Core/Container/SlotHandleTable.h"
#include "Core/Container/span.h"
#include "Core/Foundation/Types.h"

#include "Engine/Physics/IPhysicsScene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>

namespace sw
{
    /**
     * @class JoltVehicleSet
     * @brief 한 Jolt 씬의 바퀴 차들입니다. 파일 머리말 참고. 게임 스레드에서만 쓴다.
     */
    class JoltVehicleSet
    {
    public:
        explicit JoltVehicleSet( JPH::PhysicsSystem& system )
            : _system{ system }
        {
        }
        ~JoltVehicleSet() { clear(); }
        JoltVehicleSet( const JoltVehicleSet& )            = delete;
        JoltVehicleSet& operator=( const JoltVehicleSet& ) = delete;

        /**
         * @brief 차를 만듭니다. 차체는 씬이 검증한 동적 바디입니다.
         * @param testerLayer 바퀴 충돌이 쓰는 차체의 오브젝트 레이어입니다.
         * @return 서술이 틀렸거나(바퀴 · 구동 차축) 차체를 잠그지 못하면 오류를 남기고 무효 핸들입니다.
         */
        [[nodiscard]] PhysicsVehicleHandle create( PhysicsBodyHandle chassis, const JPH::BodyID& chassisBodyID, JPH::ObjectLayer testerLayer, const PhysicsWheeledVehicleDesc& desc );
        /** @brief 차를 지웁니다. @return 지운 차의 차체입니다(씬이 깨운다). 없는 차면 무효 핸들입니다. */
        PhysicsBodyHandle destroy( PhysicsVehicleHandle vehicle );
        bool              isValid( PhysicsVehicleHandle vehicle ) const;
        /** @brief 운전 입력을 넣습니다. @return 입력이 있어 깨워야 할 차체입니다(자는 바디에는 구속이 돌지 않는다). 없으면 무효 핸들입니다. */
        PhysicsBodyHandle setInput( PhysicsVehicleHandle vehicle, float32 forward, float32 right, float32 brake, float32 handBrake );
        bool              getState( PhysicsVehicleHandle vehicle, PhysicsVehicleState& outState ) const;
        /** @brief 차체가 @p listBody 에 든 차를 지웁니다(바디를 지우기 전에 씬이 부른다). */
        void destroyAttachedTo( span<const PhysicsBodyHandle> listBody );
        /** @brief 모든 차를 시스템에서 빼고 지웁니다(씬이 바디를 지우기 전에 부른다). */
        void clear();

    private:
        /** @brief 바퀴 차 하나입니다. 구속(스텝 리스너이기도 합니다)과 바퀴 충돌 테스터, 차체를 담습니다. */
        struct Record
        {
            JPH::Ref<JPH::VehicleConstraint>      _pConstraint;
            JPH::Ref<JPH::VehicleCollisionTester> _pTester;
            PhysicsBodyHandle                     _chassis{};
            JPH::BodyID                           _chassisBodyID{};
        };

        /** @brief 구속을 시스템에서 제거합니다(구속과 스텝 리스너 모두). */
        void remove( Record& record );

        JPH::PhysicsSystem&     _system;
        SlotHandleTable<Record> _vehicles;
    };
} // namespace sw
