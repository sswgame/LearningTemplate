/**
 * @file IPhysicsCharacterScene.h
 * @brief 물리 씬의 캐릭터 무버 쪽 인터페이스입니다(`IPhysicsCharacterScene3D`, `IPhysicsCharacterScene2D`).
 * @details 언리얼의 `UCharacterMovementComponent` 가 쓰는 쓸기 이동, 유니티의 `CharacterController` 에 해당한다.
 *          핸들 · 스레드 · 단위 규칙은 `IPhysicsScene.h` 머리말과 같다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /** @brief 캡슐 캐릭터 무버를 다룹니다. */
    template <typename TDimension>
    class IPhysicsCharacterScene
    {
    public:
        using Vector         = typename TDimension::Vector;
        using CharacterDesc  = PhysicsCharacterDesc<TDimension>;
        using CharacterState = PhysicsCharacterState<TDimension>;

        virtual ~IPhysicsCharacterScene() = default;

        virtual PhysicsCharacterHandle createCharacter( const CharacterDesc& desc )         = 0;
        virtual void                   destroyCharacter( PhysicsCharacterHandle character ) = 0;
        /**
         * @brief 캐릭터를 @p velocity 로 @p deltaTime 만큼 움직입니다. 벽에서 미끄러지고, 턱(`_stepHeight`)을 오르고, 가파른 비탈(`_maxSlopeAngle`)은
         *        벽처럼 막습니다. 중력은 넣지 않으므로 호출하는 쪽이 속도에 더해야 합니다. 스텝을 기다리지 않고 바로 움직입니다.
         * @details 움직이는 바닥(Kinematic · 동적 바디) 위에 서 있으면 그 바닥의 속도(선속도와 회전이 발 밑에 만드는 속도)를 구현이 알아서 더하므로
         *          @p velocity 는 바닥 기준 속도다. 딛고 선 바디는 상태의 `_groundBody` 로 나간다.
         */
        virtual CharacterState moveCharacter( PhysicsCharacterHandle character, const Vector& velocity, float32 deltaTime ) = 0;
        virtual void           setCharacterPosition( PhysicsCharacterHandle character, const Vector& position )             = 0;
        /**
         * @brief 캐릭터를 지우지 않고 캡슐 크기를 바꿉니다 — 자리 · 디딘 바닥 · 속 바디(`_bInnerBody`)의 핸들과 접촉이 이어집니다.
         * @details 배율이 바뀐 컨트롤러가 쓴다(다시 만들면 속 바디의 쌍마다 끝 → 시작이 난다, 감사 S-4). 커진 캡슐이 벽에 묻혀도 바꾼다 — 다음 이동이 밀어낸다.
         *          언리얼 `UCapsuleComponent::SetCapsuleSize`, 유니티 `CharacterController.radius` · `height`, Jolt `CharacterVirtual::SetShape` 자리다.
         *          지을 수 없는 크기(0 이하)거나 없는 캐릭터면 오류를 남기고 false — 부르는 쪽이 캐릭터를 놓는다.
         */
        [[nodiscard]] virtual bool setCharacterSize( PhysicsCharacterHandle character, float32 radius, float32 halfHeight ) = 0;
        virtual bool               getCharacterState( PhysicsCharacterHandle character, CharacterState& outState ) const    = 0;
    };

    using IPhysicsCharacterScene3D = IPhysicsCharacterScene<PhysicsDimension3D>;
    using IPhysicsCharacterScene2D = IPhysicsCharacterScene<PhysicsDimension2D>;
} // namespace sw
