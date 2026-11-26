/**
 * @file TickedFloorStep.h
 * @brief 네트 걸음 동안 틱 함수 바닥(`InterpToMovementComponent`)의 물리 바디를 그 명령의 틱 자세에 두는 장치입니다.
 * @details 한 프레임에 명령이 여럿이거나 되감아 다시 돌리면 바닥은 명령마다 다른 자세여야 한다 — 프레임에 한 번 옮기는 바닥은 둘째 명령부터 서버 · 예측이 갈린다.
 *          걸음 앞에는 바디를 앞 틱의 자세에 두고 이 틱까지 가는 속도(직선 · 각)를 준다 — 캐릭터 이동이 디딘 바디의 속도를 실어 나르므로(`moveCharacter`) 탄 채로 같은 만큼 가고,
 *          떠나면 그 속도가 관성으로 남는다. 걸음 뒤에는 바디를 이 틱의 자세에 두어 접촉이 새 자리를 본다. 물리 질의는 옮긴 자세를 바로 본다(Jolt `SetPositionAndRotation` 이
 *          브로드페이즈 경계를 함께 고친다). 사라질 때 모든 바디의 자세 · 속도를 되돌려 물리 프레임(트랜스폼을 따라가는 `moveKinematic`)은 이 장치를 모른다.
 *          언리얼 CharacterMovement 의 `MovementBase`(서버 · 클라이언트가 같은 시각의 바닥 위에서 움직임을 푼다) · 물리 예측 무버 자리다(기억에 의존).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/IPhysicsSceneFwd.h"
#include "Engine/Physics/PhysicsTypes.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class InterpToMovementComponent;

    /**
     * @brief 입력 틱에서 그 명령을 풀 바닥(서버) 틱을 내는 차이입니다 — 서버가 정해 소유자 상태로 보내고 예측이 그대로 씁니다. 클라이언트가 고르지 못한다.
     */
    struct SW_GF_API TickedFloorOffset
    {
        /** @brief 다시 맞추는 어긋남(틱)입니다 — 흔들림에 몰려 온 입력은 이 안이라 차이를 그대로 두고, 시계가 벌어졌을 때만 옮긴다. */
        static constexpr int64 kMaxDrift = 8;

        /** @brief 서버가 이 입력 틱에 쓸 차이로 고칩니다 — 지금 차이가 @p serverTick 과 `kMaxDrift` 안이면 그대로입니다(명령마다 바닥이 한 틱씩 간다). */
        static void update( uint32 serverTick, uint32 inputTick, int32& inoutOffset, uint8& inoutbKnown );
        /** @brief 입력 틱 @p inputTick 의 바닥 틱입니다. 아직 모르면 없음입니다. */
        [[nodiscard]] static optional<int64> findTick( int32 offset, uint8 bKnown, uint32 inputTick );
    };

    /**
     * @class TickedFloorStep
     * @brief 게임 스레드, 틱 밖(물리 스텝 밖)의 한 블록에서만 씁니다.
     */
    class SW_GF_API TickedFloorStep
    {
    public:
        /** @brief @p manager 의 씬에서 키네마틱 강체를 가진 틱 함수 바닥을 모두 잡고 지금 자세 · 속도를 기억합니다. @p stepSeconds 는 네트 걸음 하나의 길이입니다(틱 × 이 값이 바닥의 시각). */
        TickedFloorStep( GameObjectManager& manager, float32 stepSeconds );
        /** @brief 잡은 바디의 자세 · 속도를 되돌립니다. */
        ~TickedFloorStep();

        TickedFloorStep( const TickedFloorStep& )            = delete;
        TickedFloorStep& operator=( const TickedFloorStep& ) = delete;

        /** @brief 틱 @p tick 의 명령을 풀기 전입니다 — 바디를 (틱 − 1) 자세에 두고 이 틱까지의 속도를 줍니다. */
        void beginTick( int64 tick );
        /** @brief 틱 @p tick 의 명령을 푼 뒤입니다 — 바디를 이 틱의 자세에 둡니다. */
        void endTick( int64 tick );

    private:
        struct Floor
        {
            const InterpToMovementComponent* _pFloor{ nullptr };
            PhysicsBodyHandle                _body{};
            float3                           _savedPosition{};
            quaternion                       _savedRotation{};
            float3                           _savedLinearVelocity{};
            float3                           _savedAngularVelocity{};
        };

        void place( const Floor& floor, int64 tick ) const;

    private:
        vector<Floor>    _listFloor;
        IPhysicsScene3D* _pScene;
        float64          _stepSeconds;
    };
} // namespace sw
