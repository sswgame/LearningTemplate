/**
 * @file ShooterNetPawnComponent.h
 * @brief 슈터 플레이어 몸의 네트워크 이동입니다 — 서버 권위 + 내 폰만 예측 · 재조정 + 보정 녹이기(계획 `RollbackPrediction` R3 · R4, 결정 기록 5-12 `D7-8`).
 * @details 게임 세션(`GameNetSession`)이 열려 있을 때만 일한다. 혼자 하는 게임에서는 아무것도 하지 않아 몸 이동이 예전처럼 프레임마다 돈다.
 *          고정 틱(`_tickInterval`)으로 돈다 — 예측과 서버 시뮬레이션이 같은 걸음 크기여야 재조정이 맞는다. 켜지면 몸 이동을 네트워크 몫으로 돌린다(`setNetDriven`).
 *          - 서버: 이 몸의 조종 연결은 처음 붙은 클라이언트 하나다(아레나에 플레이어 몸이 하나라서 — 여럿은 몸 스폰과 함께 할 일). 틱마다 그 연결의 다음 입력 틱을 꺼내
 *            (`ReplicationServer::popInput`, 늦으면 지난 입력을 되풀이) `ShooterBodyMovementComponent::simulate` 로 움직이고, 상태를 소유자 상태(그 입력 틱과 한 쌍)와
 *            엔티티로 싣는다. 엔티티 자리는 관심 영역 정책(`InterestReplicationPolicy`)에 알린다.
 *          - 클라이언트: 틱마다 폰 의도를 입력으로 만들어 보내고(`ReplicationClient::sendInput`) 바로 같은 함수로 예측한다(`ClientPrediction::record`).
 *            새 소유자 상태가 오면 그 입력 틱의 기록과 비교해(`reconcile`) 다르면 남은 입력을 다시 시뮬레이션하고, 보정 전후 자리 차이를 `PredictionSmoother` 로 녹인다.
 *          점프 버튼은 그 프레임에 틱이 없으면 놓친다(눌림 판정은 프레임마다다) — 고정 틱 입력 모으기는 남은 일이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Replication/PredictionSmoother.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ClientPrediction.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/InterestReplicationPolicy.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

#include "Games/Shooter3D/ShooterBodyMovementComponent.h"

namespace sw
{
    /**
     * @class ShooterNetPawnComponent
     * @brief 플레이어 오브젝트에 두는 네트워크 이동입니다. 같은 오브젝트의 `ShooterBodyMovementComponent` · `ShooterPlayerComponent`(감독의 막는 상자)를 씁니다.
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Net Pawn", Tooltip = "Server-authoritative body movement with client prediction, reconciliation and correction smoothing" )
    class ShooterNetPawnComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 이 몸의 복제 엔티티 id 입니다. */
        static constexpr uint32 kPawnEntityID = 1;
        /** @brief 이 몸의 복제 엔티티 타입입니다. */
        static constexpr uint32 kPawnTypeID = 1;

        ShooterNetPawnComponent();
        ~ShooterNetPawnComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        bool   isActive() const { return _role != Role::None; }
        uint32 getCorrectionCount() const { return _prediction.getCorrectionCount(); }

        /** @brief 걸음 상태를 바이트로 씁니다(엔티티 · 소유자 상태의 몸). */
        static void               writeState( const ShooterMoveState& state, vector<uint8>& outBytes );
        [[nodiscard]] static bool readState( const vector<uint8>& bytes, ShooterMoveState& outState );
        /** @brief 걸음 입력을 바이트로 씁니다(`NetClientServerMessage::kMaxInputBytes` 안). */
        static void writeInput( const ShooterMoveInput& input, vector<uint8>& outBytes );
        /** @brief 입력을 읽습니다. 이동 길이가 1 을 넘으면(손상 · 속인 입력) 잘라 받지 않고 false 입니다. */
        [[nodiscard]] static bool readInput( const vector<uint8>& bytes, ShooterMoveInput& outInput );
        /** @brief 두 상태가 같다고 볼 만큼 가까운가입니다(재조정 판정 — 자리 `_reconcileTolerance` 안, 땅 여부가 같음). */
        bool isClose( const ShooterMoveState& left, const ShooterMoveState& right ) const;

    private:
        enum class Role : uint8
        {
            None = 0,
            Server,
            Client
        };

        void                           stepServer( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox );
        void                           stepClient( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox );
        ShooterBodyMovementComponent*  findMovement() const;
        const vector<ShooterArenaBox>* findArenaBoxes() const;

        PROPERTY( Category = "Network", DisplayName = "Tick Interval", Min = 0.001, Units = s, Tooltip = "Fixed step of the networked movement; server simulation and client prediction use the same step" )
        float32 _tickInterval;
        PROPERTY( Category = "Network", DisplayName = "Smoothing Half Life", Min = 0.001, Units = s, Tooltip = "Time for half of a prediction correction to fade out on screen" )
        float32 _smoothingHalfLife;
        PROPERTY( Category = "Network", DisplayName = "Smoothing Snap Distance", Min = 0.001, Units = m, Tooltip = "Corrections larger than this are applied at once instead of fading (teleport)" )
        float32 _smoothingSnapDistance;
        PROPERTY( Category = "Network", DisplayName = "Reconcile Tolerance", Min = 0.0, Units = m, Tooltip = "Predicted and server positions closer than this count as the same" )
        float32 _reconcileTolerance;
        PROPERTY( Category = "Network", DisplayName = "Interest Cell Size", Min = 0.001, Units = m, Tooltip = "Cell size of the server's interest grid" )
        float32 _interestCellSize;
        PROPERTY( Category = "Network", DisplayName = "Interest Enter Radius", Min = 0.0, Units = m, Tooltip = "Entities inside this radius of a client's pawn start replicating to it" )
        float32 _interestEnterRadius;
        PROPERTY( Category = "Network", DisplayName = "Interest Leave Radius", Min = 0.0, Units = m, Tooltip = "Entities stop replicating once outside this radius (not smaller than the enter radius)" )
        float32 _interestLeaveRadius;

        ReplicationServer                                    _server;
        ReplicationClient                                    _client;
        InterestReplicationPolicy                            _interest;
        Subscription                                         _routerSubscription; ///< 세션 라우터에 단 `_server` 또는 `_client` 의 등록(처리기보다 뒤에 선언 — 먼저 풀린다)
        ClientPrediction<ShooterMoveState, ShooterMoveInput> _prediction;
        PredictionSmoother                                   _smoother;
        vector<uint8>                                        _bytesScratch;
        float32                                              _accumulator;
        uint32                                               _tick;           ///< 서버 — 시뮬레이션 틱, 클라이언트 — 예측한 입력 틱
        uint32                                               _nextInputTick;  ///< 서버 — 조종 연결에서 다음에 꺼낼 입력 틱
        uint32                                               _reconciledTick; ///< 클라이언트 — 마지막으로 맞춘 소유자 상태의 입력 틱
        int32                                                _controllerID;   ///< 서버 — 이 몸을 조종하는 연결(-1 이면 없다)
        Role                                                 _role;
        uint8                                                _bJumpPending; ///< 클라이언트 — 틱이 없던 프레임에 눌린 점프를 다음 입력 틱까지 쌓아 둔다(눌림은 한 프레임만 참이라 놓치기 쉽다)
    };
} // namespace sw
