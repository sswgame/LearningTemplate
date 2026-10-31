/**
 * @file JoltSoftBodySet.h
 * @brief Jolt 씬의 소프트 바디(천 · 헤어 카드) 묶음입니다. `SoftBodySharedSettings` 에 정점 · 면을 넣고 늘어남 · 비틀림 · 굽힘 구속을 만듭니다.
 * @details 정점은 씬 스텝(`PhysicsSystem::Update`)이 강체와 함께 푼다. 고정 정점은 역질량 0 이라 구속이 움직이지 않고, `setPinnedTransform`
 *          이 위치를 직접 쓴다(Jolt 의 키네마틱 정점 방식). 몸 자리는 만든 때 그대로 둔다(`mUpdatePosition` 끔) — 정점이 질량 중심 기준이라
 *          몸이 정점을 따라 옮겨 다니면 고정 정점의 로컬 위치를 매번 다시 맞춰야 한다. 천이 멀리 가지 않는 쓰임(옷 · 깃발 · 커튼)이라 넓은 단계 상자가
 *          커지는 값은 작다. 소프트 바디는 접촉 이벤트 · 질의 결과의 바디 표에 들지 않는다(강체 이벤트를 내지 않는다).
 *          굽힘 구속은 거리형(`EBendType::Distance`, 공유 변 맞은편 두 정점 사이 거리)이다. 이면각형(`Dihedral`)은 쓰지 않는다 — 두 삼각형이 180° 로
 *          접혀 법선이 반대가 되면 각도 부호(`(n2 × n1) · e`)가 반올림 잡음으로 정해져 보정이 반복마다 뒤집히고, 바닥에 떨어져 접히는 천이
 *          수만 m 로 발산했다(Windows 에서 떨어뜨리는 자세 24 가지 중 4 가지, 리눅스 CI 는 기본 자세에서). 거리형은 접혀도 특이점이 없다.
 *          Jolt 헤더를 include 하므로 `Physics/Jolt` 밖에서는 보지 않는다.
 */
#pragma once
#include "Core/Container/SlotHandleTable.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Physics/IPhysicsScene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace sw
{
    class IPhysicsDebugRenderer;

    /**
     * @class JoltSoftBodySet
     * @brief 한 Jolt 씬의 소프트 바디들입니다. 파일 머리말 참고. 게임 스레드에서만 쓴다.
     */
    class JoltSoftBodySet
    {
    public:
        JoltSoftBodySet( JPH::PhysicsSystem& system, JPH::TempAllocator& tempAllocator )
            : _system{ system }
            , _tempAllocator{ tempAllocator }
        {
        }
        ~JoltSoftBodySet() { clear(); }
        JoltSoftBodySet( const JoltSoftBodySet& )            = delete;
        JoltSoftBodySet& operator=( const JoltSoftBodySet& ) = delete;

        /** @brief 소프트 바디를 만들어 시스템에 넣습니다. 서술이 틀렸으면(정점 · 삼각형 · 질량 · 인덱스 · 스킨 구속) 오류를 남기고 무효 핸들입니다. */
        [[nodiscard]] PhysicsSoftBodyHandle create( const PhysicsSoftBodyDesc& desc );
        void                                destroy( PhysicsSoftBodyHandle softBody );
        bool                                isValid( PhysicsSoftBodyHandle softBody ) const;
        bool                                getVertices( PhysicsSoftBodyHandle softBody, vector<float3>& outListPosition ) const;
        void                                setPinnedTransform( PhysicsSoftBodyHandle softBody, const float3& position, const quaternion& rotation );
        void                                setSkinningPose( PhysicsSoftBodyHandle softBody, const vector<float4x4>& listJointWorld, bool bHardSkinAll );
        /** @brief 소프트 바디마다 늘어남 구속을 선으로 냅니다(씬의 `drawDebug`). */
        void drawDebug( IPhysicsDebugRenderer& renderer ) const;
        /** @brief 모든 소프트 바디를 시스템에서 빼고 지웁니다(씬이 바디를 지우기 전에 부른다). */
        void clear();

    private:
        /**
         * @brief 소프트 바디 하나입니다.
         * @details Jolt 는 정점을 질량 중심 기준으로 든다. 몸 자리를 고정해(`mUpdatePosition` 끔) 질량 중심 변환이 만든 때 그대로이고, 고정 정점은
         *          그 변환의 역으로 로컬 위치를 만든다.
         */
        struct Record
        {
            JPH::BodyID    _bodyID{};
            vector<uint32> _listPinnedVertex;    ///< 고정 정점 인덱스(서술자 순서)
            vector<float3> _listPinnedRestLocal; ///< 고정 정점의 서술자 로컬 위치(`_listPinnedVertex` 와 같은 순서)
            uint32         _jointCount{ 0 };     ///< 스킨 구속이 기대하는 뼈 수(`_listInverseBind` 의 수 — 0 이면 스킨 구속이 없다)
        };

        /** @brief 소프트 바디를 시스템에서 빼고 지웁니다. */
        void remove( const Record& record );

        JPH::PhysicsSystem&     _system;
        JPH::TempAllocator&     _tempAllocator;
        SlotHandleTable<Record> _softBodies;
    };
} // namespace sw
