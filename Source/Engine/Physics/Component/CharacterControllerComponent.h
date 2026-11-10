/**
 * @file CharacterControllerComponent.h
 * @brief 캐릭터 컨트롤러(3D) — 캡슐 하나를 질의로 움직여 벽에 미끄러지고 · 턱을 오르고 · 가파른 비탈에 막힙니다(Jolt `CharacterVirtual`).
 */
#pragma once
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Physics/PhysicsField.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct CharacterCommand3D;
    template <typename TCommand>
    struct CharacterCommandPage;

    class ValidationContext;
} // namespace sw

namespace sw
{
    /**
     * @class CharacterControllerComponent
     * @brief 3D 캐릭터 컨트롤러입니다(유니티 CharacterController · 언리얼 CharacterMovement 의 충돌 부분). 이 컴포넌트의 월드 자리가 발입니다.
     * @details 시뮬레이션 바디가 아닙니다 — 고정 스텝마다 원하는 속도(`setMoveVelocity`) + 중력으로 캡슐을 쓸어 옮깁니다. 서 있으면 수직 속도를 지우고,
     *          `jump` 가 수직 속도를 줍니다. 자리는 마지막 두 스텝 사이를 보간해 씁니다. 코드가 트랜스폼을 옮기면 순간이동입니다.
     *          이동 요청은 어느 틱에서든 부를 수 있습니다(다음 물리 프레임에 든다).
     */
    REFLECT( Category = "Physics", DisplayName = "Character Controller", Tooltip = "Capsule character mover: slides on walls, climbs steps, blocked by steep slopes", Meta = "Icon=Character" )
    class SW_API CharacterControllerComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 입력 칸(`PhysicsPoseStore` 의 캐릭터 저장소)을 받습니다. */
        CharacterControllerComponent();
        /** @brief 입력 칸을 놓습니다. */
        ~CharacterControllerComponent() override;

        /** @brief 원하는 수평 이동 속도(월드, 미터/초)입니다. 다시 부를 때까지 유지합니다. Y 는 보지 않습니다(중력 · 점프가 정한다). */
        void setMoveVelocity( const float3& velocity );
        /** @brief 위로 @p speed(미터/초)를 줍니다. 다음 스텝에 듭니다. */
        void jump( float32 speed );
        /**
         * @brief 애니메이션 루트 모션의 이번 프레임 이동(월드, 미터)을 더합니다. 다음 물리 프레임의 스텝들이 나눠 움직입니다(벽에 막히고 턱을 오른다).
         * @details 수평(XZ)만 씁니다 — 수직은 중력 · 점프가 정합니다. 스텝이 없는 프레임이면 다음 프레임으로 넘깁니다. 아무 스레드에서 불러도 됩니다.
         */
        void addRootMotionDisplacement( const float3& worldDisplacement );
        /**
         * @brief 발사대 · 스프링이 쏩니다 — 수직은 그 속도로 덮고, 수평은 땅에 다시 닿을 때까지 이동 속도에 더합니다. 다음 스텝에 듭니다. 아무 스레드에서 불러도 됩니다.
         */
        void launch( const float3& velocity );
        /**
         * @brief 딛고 선 면의 속도(컨베이어 · 흐르는 물, 월드 m/s)를 이번 물리 프레임에 더합니다. 프레임마다 다시 불러야 이어집니다(여럿이면 합).
         * @details 아무 스레드에서 불러도 됩니다. 트랜스폼을 직접 옮기면 순간이동이라 벽을 지나므로, 면이 나르는 것은 이것으로 넘깁니다.
         */
        void addSurfaceVelocity( const float3& velocity );
        /**
         * @brief 이번 물리 프레임의 걷는 속도(요청 속도 · 루트 모션)에 곱할 이동 계수를 얹습니다(눈 · 모래 · 물 · 함몰 구역). 프레임마다 다시 불러야 이어지고, 여럿이면 가장 작은 값입니다.
         * @details 환경이 주는 이동 저항의 한 입력이다 — 표면(`GroundSurfaceComponent`)과 이펙트의 이동 제약(GameFramework `MovementConstraint`)이 각자 부른다.
         *          결정: 곱이 아니라 최솟값이다 — 물리 스텝이 없는 렌더 프레임이 끼면 같은 호출이 두 번 쌓이는데, 최솟값은 몇 번 불러도 같다(곱은 0.8 이 0.64 가 된다).
         *          면 속도 · 발사 · 관성은 곱하지 않는다(걷는 힘이 아니다). 아무 스레드에서 불러도 됩니다. 음수는 0 으로 둡니다.
         */
        void applyMoveScale( float32 scale );
        /** @brief 지난 물리 프레임에 걷는 속도에 곱한 계수입니다(진단 · 감시 값). */
        float32 getMoveScale() const { return _reportedMoveScale.load( std::memory_order_relaxed ); }
        /**
         * @brief 이번 게임 프레임에 받는 물리 필드(중력 배율 · 가속 · 흐름 · 저항 · 부력 · 마찰)를 얹습니다. 여러 지역이면 `PhysicsFieldSample::combine` 으로 합친다.
         * @details 물리 프레임이 시작할 때(`beginPhysicsFrame`) 모은 값을 그 프레임의 필드로 옮기고 비운다 — 프레임마다 다시 받아야 이어지고, 스텝이 없는 프레임이 끼어도
         *          한 프레임의 값이 두 번 쌓이지 않는다. 중립 필드면 이동은 필드가 없을 때와 같다. 아무 스레드에서 불러도 된다.
         */
        void applyPhysicsField( const PhysicsFieldSample& sample );
        /** @brief 지난 물리 프레임에 쓴 필드입니다(진단 · 감시 값). */
        const PhysicsFieldSample& getPhysicsField() const { return _frameField; }
        /** @brief 지난 스텝이 필드로 고른 이동 방식입니다(걷기 · 낙하 · 수영 · 부유). */
        CharacterMovementMode getMovementMode() const { return _movementMode; }
        /** @brief 마지막 스텝에 바닥을 딛고 있었는지입니다. */
        bool isGrounded() const { return _state._bGrounded; }
        /** @brief 마지막 스텝의 바닥 법선입니다. */
        float3 getGroundNormal() const { return _state._groundNormal; }
        /** @brief 마지막 스텝에서 실제로 난 속도입니다. */
        float3 getVelocity() const { return _state._velocity; }
        /** @brief 마지막 스텝 뒤의 발 자리입니다(보간 전). */
        float3 getSimulatedPosition() const { return _state._position; }
        /** @brief 캐릭터 핸들입니다(시작 전이면 무효). */
        PhysicsCharacterHandle getCharacterHandle() const { return _character; }
        /** @brief 마지막 스텝의 이동 결과 전부입니다(딛은 바디 · 실어 나른 속도 · 바닥 회전 포함). 이동 디버거가 읽는다. */
        const PhysicsCharacterState3D& getCharacterState() const { return _state; }
        /** @brief 조종이 요청한 수평 속도(`setMoveVelocity`)입니다 — 루트 모션 · 면 속도 · 공중 관성을 더하기 전 값이라 "요청 → 실제" 를 비교한다. 아무 스레드에서 읽어도 된다. */
        float3 getRequestedMoveVelocity() const;

        static constexpr float32 kFrictionResponse   = 10.0f; ///< 접지 1 일 때 걷는 속도가 요청으로 가는 빠르기(1/s) — 접지에 곱한다(빙판 0.05 면 0.5/s)
        static constexpr float32 kFieldGroundDamping = 4.0f;  ///< 디딘 채로 필드가 쌓은 수평 속도가 줄어드는 빠르기(1/s, 접지에 곱한다)
        static constexpr float32 kBodyDensity        = 1.0f;  ///< 캐릭터 몸의 밀도(물 = 1) — 유체 밀도가 이보다 크면 떠오른다

        /** @brief 스텝 기록 한 칸입니다(이동 디버거가 미끄러짐 · 벽 걸림 · 착지 판정을 전후로 본다). */
        struct StepRecord
        {
            float3                  _stepVelocity{}; ///< 이 스텝이 백엔드에 넘긴 속도(요청 + 루트 모션 + 면 + 공중 관성, 수직 포함)
            PhysicsCharacterState3D _result{};       ///< 그 결과
        };
        /** @brief 스텝 기록의 칸 수입니다(고정 용량). */
        static constexpr uint32 kStepHistoryCapacity = 8;
        /**
         * @brief 모든 캐릭터의 스텝 기록을 켜거나 끕니다(개발 명령 `movement.history on|off`). 꺼져 있으면 스텝마다 분기 하나뿐이고 칸을 만들지 않는다.
         * @details 언리얼 CharacterMovement 의 `p.VisualizeMovement` 처럼 전역 스위치다. 켜면 캐릭터마다 첫 스텝에 칸 `kStepHistoryCapacity` 개를 한 번 만든다.
         */
        static void setStepHistoryEnabled( bool bEnabled );
        static bool isStepHistoryEnabled();
        /** @brief 최근 스텝 기록(오래된 것부터)을 @p outListRecord 에 채웁니다(먼저 비운다). 기록이 꺼져 있었으면 비어 있다. 게임 스레드에서 물리 스텝 밖에 부른다. */
        void collectStepHistory( vector<StepRecord>& outListRecord ) const;
        /** @brief 상태 해시에 트랜스폼과 속도 · 접지를 접습니다. */
        void foldStateHash( StateHashBuilder& inoutBuilder ) const override;

        float32 getRadius() const { return _radius; }
        void    setRadius( float32 radius );
        float32 getHalfHeight() const { return _halfHeight; }
        void    setHalfHeight( float32 halfHeight );
        float32 getStepHeight() const { return _stepHeight; }
        void    setStepHeight( float32 stepHeight );
        float32 getMaxSlopeAngle() const { return _maxSlopeAngle; }
        void    setMaxSlopeAngle( float32 angle );

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount ) override;
        void endPhysicsFrame( ScenePhysics& physics, float32 alpha ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        /** @brief `_layer` 가 물리 레이어 표에 있는지 봅니다(`PhysicsNameValidation`). */
        void validateLayer( ValidationContext& context ) const;
        /** @brief 입력 칸을 잠그고 @p function( CharacterCommand3D& ) 을 부릅니다(아무 스레드). */
        template <typename TFunction>
        void editCommand( TFunction&& function ) const;

    private:
        PROPERTY( Category = "Character", Min = 0.01, Tooltip = "Capsule radius", Units = m )
        float32 _radius;
        PROPERTY( Category = "Character", Min = 0.0, Tooltip = "Half the cylinder part; height = 2 * (half height + radius)", Units = m )
        float32 _halfHeight;
        PROPERTY( Category = "Character", Min = 0.0, Tooltip = "Highest step the character walks up", Units = m )
        float32 _stepHeight;
        PROPERTY( Category = "Character", Min = 0.0, Max = 1.57, Tooltip = "Steepest walkable floor", Units = rad )
        float32 _maxSlopeAngle;
        PROPERTY( Category = "Character", Tooltip = "Multiplier on scene gravity (0 floats)" )
        float32 _gravityScale;
        PROPERTY( Category = "Character", Min = 0.0, Tooltip = "Mass used to push dynamic bodies", Units = kg )
        float32 _mass;
        PROPERTY( Category = "Character", Tooltip = "Collision layer name from the physics settings", Validate = validateLayer )
        hashed_string _layer;
        PROPERTY( Category = "Character", DisplayName = "Inner Body", Tooltip = "Keep a kinematic capsule body on the character so cloth and sensors feel it (it also pushes dynamic bodies harder)" )
        bool _bInnerBody;

        PhysicsCharacterHandle                    _character;
        PhysicsCharacterState3D                   _state;
        CharacterCommandPage<CharacterCommand3D>* _pCommandPage;                 ///< 입력 칸이 든 페이지 — 이동 요청 · 대기(`PhysicsPoseStore::getCharacterStore`)
        uint32                                    _commandSlot;                  ///< 입력 칸 번호(컴포넌트 수명 동안 그대로)
        ThreadRole                                _physicsStep;                  ///< 물리 스텝 역할 — 이번 프레임 속도 · 공중 속도는 prePhysicsStep 만 만진다(잠금이 아니라 분석용 능력)
        float3 _frameRootMotionVelocity           SW_GUARDED_BY( _physicsStep ); ///< 이번 물리 프레임의 스텝마다 더할 루트 모션 속도
        float3 _frameSurfaceVelocity              SW_GUARDED_BY( _physicsStep ); ///< 이번 물리 프레임의 면 속도
        float32 _frameMoveScale                   SW_GUARDED_BY( _physicsStep ); ///< 이번 물리 프레임의 이동 계수
        atomic<float32>                           _reportedMoveScale;            ///< 진단 · 감시 값이 읽는 사본(`getMoveScale`) — 물리 스텝이 쓰고 게임 스레드가 읽는다
        float3 _airVelocity                       SW_GUARDED_BY( _physicsStep ); ///< 발사 · 떠난 바닥의 수평 속도 — 땅에 다시 닿으면 0
        PhysicsFieldSample                        _frameField;                   ///< 이번 물리 프레임의 필드(`beginPhysicsFrame` 이 옮긴다)
        float3 _walkVelocity                      SW_GUARDED_BY( _physicsStep ); ///< 지난 스텝에 실제로 낸 걷는 속도 — 접지가 낮으면(빙판) 요청 속도로 천천히 간다
        float3 _fieldVelocity                     SW_GUARDED_BY( _physicsStep ); ///< 필드의 가속 · 흐름이 쌓은 수평 속도(당김 · 해류) — 디디면 접지만큼 줄어든다
        CharacterMovementMode                     _movementMode;                 ///< 지난 스텝의 이동 방식
        float3                                    _previousPosition;
        float3                                    _worldScale;    ///< 트랜스폼의 월드 배율(변경 번호가 바뀔 때만 다시 읽는다)
        quaternion                                _worldRotation; ///< 트랜스폼의 월드 회전(바닥이 돈 각을 얹어 쓴다). 변경 번호가 같으면 그대로다
        float32                                   _verticalSpeed;
        float32                                   _pendingGroundYaw; ///< 마지막 쓰기 뒤 디딘 바닥이 위 축 둘레로 돈 각(rad) — 다음 트랜스폼 쓰기에 얹는다
        vector<StepRecord>                        _listStepHistory;  ///< 스텝 기록 고리(`setStepHistoryEnabled` 가 켜졌을 때 처음 칸을 만든다 — 꺼져 있으면 비어 있다)
        uint32                                    _stepHistoryCount; ///< 기록한 스텝의 누계(고리의 다음 칸은 `% kStepHistoryCapacity`)
    };
} // namespace sw
