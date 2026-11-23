/**
 * @file IPhysicsCharacterScene.h
 * @brief 물리 씬의 캐릭터 무버 쪽 인터페이스입니다(`IPhysicsCharacterScene3D`, `IPhysicsCharacterScene2D`).
 * @details 언리얼의 `UCharacterMovementComponent` 가 쓰는 쓸기 이동, 유니티의 `CharacterController` 에 해당한다.
 *          핸들 · 스레드 · 단위 규칙은 `IPhysicsScene.h` 머리말과 같다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /** @brief 일괄 이동(`moveCharacters`)의 한 칸입니다 — 캐릭터 하나와 이번 스텝의 바닥 기준 속도(`moveCharacter` 의 @p velocity 와 같다). */
    template <typename TDimension>
    struct PhysicsCharacterMove
    {
        PhysicsCharacterHandle      _character{};
        typename TDimension::Vector _velocity{};
    };

    using PhysicsCharacterMove3D = PhysicsCharacterMove<PhysicsDimension3D>;
    using PhysicsCharacterMove2D = PhysicsCharacterMove<PhysicsDimension2D>;

    /** @brief 캡슐 캐릭터 무버를 다룹니다. */
    template <typename TDimension>
    class IPhysicsCharacterScene
    {
    public:
        using Vector         = typename TDimension::Vector;
        using CharacterDesc  = PhysicsCharacterDesc<TDimension>;
        using CharacterState = PhysicsCharacterState<TDimension>;
        using CharacterMove  = PhysicsCharacterMove<TDimension>;

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
        /**
         * @brief 캐릭터 여럿을 한 번에 @p deltaTime 만큼 움직입니다 — 칸마다 `moveCharacter` 와 같은 결과이고 차례도 목록 차례입니다(앞 캐릭터가 옮긴 뒤 다음이 본다).
         *        결과는 `getCharacterState` 로 읽습니다. 없는 캐릭터의 칸은 건너뜁니다.
         * @details 씬이 스텝마다 캐릭터 이동을 모아 한 번 부른다(`ScenePhysics::queueCharacterMove`, 감사 S-8) — 유니티 DOTS 캐릭터 컨트롤러 잡 · 언리얼 Mover 의
         *          일괄 갱신 자리다. 구현이 레인으로 나눠 돌 수 있게 한 호출에 모은다(지금 구현은 차례로 돈다).
         */
        virtual void moveCharacters( span<const CharacterMove> listMove, float32 deltaTime ) = 0;
        /**
         * @brief `moveCharacter` 와 같은 이동이되 닿은 동적 바디를 밀거나 디딘 바디를 누르지 않습니다 — 되감은 입력을 다시 돌리는 이동(네트워크 예측)이 세계를 여러 번 밀지 않게 한다.
         * @details 언리얼 `UCharacterMovementComponent` 가 저장한 이동을 다시 돌리는 동안(`bClientUpdating`) 물리 충격을 주지 않는 자리다.
         */
        virtual CharacterState replayCharacterMove( PhysicsCharacterHandle character, const Vector& velocity, float32 deltaTime ) = 0;
        virtual void           setCharacterPosition( PhysicsCharacterHandle character, const Vector& position )                   = 0;
        /**
         * @brief 지금 자리에서 닿은 것 · 디딘 바닥을 새로 찾고 속도를 지웁니다(옮기지 않는다). `setCharacterPosition` 뒤에 부르면 그 자리의 상태가 (자리, 세계)만의 함수가 된다.
         * @details 이동은 직전 이동의 접촉(디딤)을 이어 쓰므로, 상태를 되돌린 뒤 이것을 불러야 "되감아 다시 돌리기 = 이어 돌리기" 다(Jolt `CharacterVirtual::RefreshContacts`).
         */
        virtual void refreshCharacterContacts( PhysicsCharacterHandle character ) = 0;
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
