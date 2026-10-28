/**
 * @file ArcadeVehicleComponent.h
 * @brief 아케이드 차 폰의 이동입니다. 의도를 `ArcadeVehicleMotor`(가속, 조향, 드리프트, 부스트, 점프)에 전달합니다. 물리 엔진 없이 루트를 옮기고, 땅은 물리 광선으로 측정합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/Movement/ArcadeVehicleMotor.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ControlIntent;

    /**
     * @class ArcadeVehicleComponent
     * @brief 차 오브젝트(폰, 좌석, 이 컴포넌트)의 이동입니다. 의도는 "가고 싶은 월드 방향"(조종 요 기준 이동 축)이고, 차 기준으로 투영해서 앞 성분은 가속, 브레이크, 후진으로,
     *        옆 성분은 조향으로 바꿉니다(`toVehicleInput`). 말과 같은 규칙이므로 플레이어가 W 로 몰든 AI 가 목적지로 몰든 같은 궤적이 나옵니다.
     * @details 버튼은 폰 스키마의 이름이다(`_driftButton` 은 누름, `_boostButton` 과 `_jumpButton` 은 발동). 고정 스텝은 모터 안의 `FixedStepTimer` 가 센다.
     *          땅은 씬 물리의 아래 방향 광선으로 구하고(`_bUsePhysicsGround`), 없거나 맞지 않으면 시작 높이의 평면을 쓴다. 벽이나 차끼리의 충돌은 처리하지 않는다(모터와 같음).
     *          모터가 낸 일(드리프트, 미니터보, 니트로)은 틱마다 `getFrameEvents` 로 꺼내 볼 수 있으며, 다음 틱에 비운다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Arcade Vehicle", Tooltip = "Control intent -> ArcadeVehicleMotor: throttle, steering, drift, boost, jump without a physics body" )
    class SW_GF_API ArcadeVehicleComponent : public Component
    {
    public:
        REFLECT_BODY();

        ArcadeVehicleComponent();
        ~ArcadeVehicleComponent() override = default;

        /** @brief 모터를 지금 루트의 위치와 요에서 시작하고, 버튼 인덱스를 찾습니다. PrePhysics 에서 틱합니다. */
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /**
         * @brief 의도를 모터 입력으로 바꿉니다. 의도의 월드 이동을 차 방향(@p vehicleYaw)으로 투영해서, 앞 성분은 가속(음수이면 브레이크나 후진), 옆 성분은 조향으로 씁니다.
         * @param driftIndex 폰 스키마의 드리프트 버튼 인덱스입니다. 없으면 -1입니다.
         * @param boostIndex 폰 스키마의 부스트 버튼 인덱스입니다. 없으면 -1입니다.
         * @param jumpIndex 폰 스키마의 점프 버튼 인덱스입니다. 없으면 -1입니다.
         */
        static ArcadeVehicleInput toVehicleInput( const ControlIntent& intent, float32 vehicleYaw, int32 driftIndex, int32 boostIndex, int32 jumpIndex );

        const ArcadeVehicleMotor&         getMotor() const { return _motor; }
        ArcadeVehicleMotor&               getMotor() { return _motor; }
        const vector<ArcadeVehicleEvent>& getFrameEvents() const { return _listFrameEvent; }
        float32                           getMaxSpeed() const { return _maxSpeed; }
        void                              setMaxSpeed( float32 maxSpeed ) { _maxSpeed = maxSpeed; }
        void                              setAcceleration( float32 acceleration ) { _acceleration = acceleration; }

    private:
        /** @brief 씬 물리의 아래 방향 광선으로 답하는 땅입니다. 물리 씬이 없거나 광선이 맞지 않으면 시작 높이를 씁니다. */
        class PhysicsGround final : public IVehicleGround
        {
        public:
            PhysicsGround();
            float32 sampleHeight( float32 x, float32 z ) const override;

            const GameObjectManager* _pManager;       ///< 이 차의 씬(등록된 동안)
            float32                  _fallbackHeight; ///< 맞지 않을 때의 높이
            float32                  _probeTop;       ///< 광선을 쏘는 높이(차 위)
        };

        /** @brief 컴포넌트의 설정 필드를 모터 설정으로 바꿉니다. */
        ArcadeVehicleSettings makeSettings() const;

    private:
        PROPERTY( Category = "Vehicle", DisplayName = "Drift Button", Tooltip = "Intent button held to drift" )
        hashed_string _driftButton;
        PROPERTY( Category = "Vehicle", DisplayName = "Boost Button", Tooltip = "Intent button that fires a nitro" )
        hashed_string _boostButton;
        PROPERTY( Category = "Vehicle", DisplayName = "Jump Button", Tooltip = "Intent button that hops" )
        hashed_string _jumpButton;
        PROPERTY( Category = "Vehicle", DisplayName = "Max Speed", Min = 0.0, Units = "m/s" )
        float32 _maxSpeed;
        PROPERTY( Category = "Vehicle", DisplayName = "Acceleration", Min = 0.0, Units = "m/s2" )
        float32 _acceleration;
        PROPERTY( Category = "Vehicle", DisplayName = "Brake Deceleration", Min = 0.0, Units = "m/s2" )
        float32 _brakeDeceleration;
        PROPERTY( Category = "Vehicle", DisplayName = "Reverse Max Speed", Min = 0.0, Units = "m/s" )
        float32 _reverseMaxSpeed;
        PROPERTY( Category = "Vehicle", DisplayName = "Steer Rate", Min = 0.0, Tooltip = "Turn rate at low speed", Units = "rad/s" )
        float32 _steerRate;
        PROPERTY( Category = "Vehicle", DisplayName = "Steer Rate At Max Speed", Min = 0.0, Units = "rad/s" )
        float32 _steerRateAtMaxSpeed;
        PROPERTY( Category = "Vehicle", DisplayName = "Use Physics Ground", Tooltip = "Ground height from a downward physics ray (off: flat at the start height)" )
        bool _bUsePhysicsGround;

        ArcadeVehicleMotor         _motor;
        PhysicsGround              _ground;
        vector<ArcadeVehicleEvent> _listFrameEvent; ///< 이번 틱에 모터가 낸 일
        int32                      _driftIndex;
        int32                      _boostIndex;
        int32                      _jumpIndex;
    };
} // namespace sw
