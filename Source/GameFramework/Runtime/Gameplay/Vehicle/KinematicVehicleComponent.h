/**
 * @file KinematicVehicleComponent.h
 * @brief 바디 없는 탈것(카트 · 썰매 · 수레 · 호버 · 광산 수레) 폰의 이동입니다. 의도를 `KinematicVehicleMotor` 에 전달하고 루트를 옮깁니다.
 *        땅 높이는 물리 광선, 표면(구름 저항 · 그립)은 공통 표면 질의(`SurfaceSample` — 표면 종류 표)에서 읽습니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Movement/KinematicVehicleMotor.h"

namespace sw
{
    struct ControlIntent;
    struct SurfaceKindTable;

    /**
     * @class KinematicVehicleComponent
     * @brief 탈것 오브젝트(폰, 좌석, 이 컴포넌트)의 이동입니다. 의도는 "가고 싶은 월드 방향"(조종 요 기준 이동 축)이고, 차 기준으로 투영해서 앞 성분은 가속, 브레이크, 후진으로,
     *        옆 성분은 조향(썰매 · 보드는 기울임)으로 바꿉니다(`toVehicleInput`). 말과 같은 규칙이므로 플레이어가 W 로 몰든 AI 가 목적지로 몰든 같은 궤적이 나옵니다.
     * @details 수치는 튜닝 데이터(`_tuning`, `*.vehicle.xml` — 프리셋 · 변형은 `_base` 상속, 배치는 변형 파일을 가리킨다)이고 이 컴포넌트에 속도 칸을 두지 않는다.
     *          튜닝 · 표면 종류 표 · (레일 모듈이면) 레일 곡선이 없으면 오류를 남기고 움직이지 않는다 — 다른 값으로 대신 달리지 않는다.
     *          버튼은 폰 스키마의 이름이다(`_driftButton` 은 누름, `_boostButton` 과 `_jumpButton` 은 발동). 고정 스텝은 모터 안의 `FixedStepTimer` 가 센다.
     *          땅은 씬 물리의 아래 방향 광선으로 구하고(`_bUsePhysicsGround`), 끄거나 맞지 않으면 시작 높이의 평면(표면 없음 — 구름 저항 0)이다. 벽이나 차끼리의 충돌은 처리하지 않는다(모터와 같음).
     *          모터가 낸 일(드리프트, 미니터보, 니트로, 레일 끝)은 틱마다 `getFrameEvents` 로 꺼내 볼 수 있으며, 다음 틱에 비운다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Kinematic Vehicle", Tooltip = "Control intent -> KinematicVehicleMotor: drive, slope, rolling and air resistance, handling modules, without a physics body" )
    class SW_GF_API KinematicVehicleComponent : public Component
    {
    public:
        REFLECT_BODY();

        KinematicVehicleComponent();
        ~KinematicVehicleComponent() override;

        /** @brief 튜닝을 읽고 모터를 지금 루트의 위치와 요에서 시작하며, 버튼 인덱스와 레일을 찾습니다. PrePhysics 에서 틱합니다. */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

        /**
         * @brief 의도를 모터 입력으로 바꿉니다. 의도의 월드 이동을 차 방향(@p vehicleYaw)으로 투영해서, 앞 성분은 가속(음수이면 브레이크나 후진), 옆 성분은 조향으로 씁니다.
         * @param driftIndex 폰 스키마의 드리프트 버튼 인덱스입니다. 없으면 -1입니다.
         * @param boostIndex 폰 스키마의 부스트 버튼 인덱스입니다. 없으면 비어 있습니다.
         * @param jumpIndex 폰 스키마의 점프 버튼 인덱스입니다. 없으면 비어 있습니다.
         */
        static KinematicVehicleInput toVehicleInput( const ControlIntent& intent, float32 vehicleYaw, optional<int32> driftIndex, optional<int32> boostIndex, optional<int32> jumpIndex );

        const KinematicVehicleMotor&         getMotor() const { return _motor; }
        KinematicVehicleMotor&               getMotor() { return _motor; }
        const vector<KinematicVehicleEvent>& getFrameEvents() const { return _listFrameEvent; }
        /** @brief 튜닝 · 땅 · 레일을 갖춰 달리는 중인지입니다(시작이 오류였으면 false). */
        bool isReady() const { return _bReady; }

    private:
        /** @brief 씬 물리의 아래 방향 광선으로 높이를, 표면 질의로 표면 종류를 답하는 땅입니다. 광선이 맞지 않으면 시작 높이의 평면(표면 없음)입니다. */
        class PhysicsGround final : public IVehicleGround
        {
        public:
            PhysicsGround();
            float32        sampleHeight( float32 x, float32 z ) const override;
            VehicleSurface sampleSurface( float32 x, float32 z ) const override;

            GameObjectManager*                 _pManager;       ///< 이 차의 씬(등록된 동안). nullptr 이면 평면이다
            shared_ptr<const SurfaceKindTable> _pSurfaceTable;  ///< 표면 종류 표(시작에서 받는다)
            uint64                             _ownerID;        ///< 광선이 건너뛸 이 차의 오브젝트
            float32                            _fallbackHeight; ///< 맞지 않을 때의 높이
            float32                            _probeTop;       ///< 광선을 쏘는 높이(차 위)
        };

        /** @brief 레일 모듈이면 `_rail` 오브젝트의 스플라인 월드 곡선을 모터에 겁니다. 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool attachRail();

    private:
        PROPERTY( Category = "Vehicle", DisplayName = "Tuning", Required, Tooltip = "Vehicle tuning data (*.vehicle.xml): mass, drive, resistances, handling modules" )
        AssetRef<KinematicVehicleSettings> _tuning;
        PROPERTY( Category = "Vehicle", DisplayName = "Rail", Tooltip = "Object with the spline the rail module rides (rail tuning only)" )
        GameObjectHandle _rail;
        PROPERTY( Required, Category = "Vehicle", DisplayName = "Drift Button", Tooltip = "Intent button held to drift" )
        hashed_string _driftButton;
        PROPERTY( Required, Category = "Vehicle", DisplayName = "Boost Button", Tooltip = "Intent button that fires a nitro" )
        hashed_string _boostButton;
        PROPERTY( Required, Category = "Vehicle", DisplayName = "Jump Button", Tooltip = "Intent button that hops" )
        hashed_string _jumpButton;
        PROPERTY( Category = "Vehicle", DisplayName = "Use Physics Ground", Tooltip = "Ground height and surface from a downward physics ray (off: flat at the start height, no surface)" )
        bool _bUsePhysicsGround;

        KinematicVehicleMotor         _motor;
        PhysicsGround                 _ground;
        vector<KinematicVehicleEvent> _listFrameEvent; ///< 이번 틱에 모터가 낸 일
        optional<int32>               _driftIndex;
        optional<int32>               _boostIndex;
        optional<int32>               _jumpIndex;
        bool                          _bReady;
    };
} // namespace sw
