/**
 * @file ShooterNetPawnComponent.h
 * @brief 슈터 플레이어 몸의 네트워크 이동입니다 — 서버 권위 + 내 폰만 예측 · 재조정 + 보정 녹이기.
 * @details 이 컴포넌트의 월드에 넷 링크(`SceneNetLink` — 전역 세션 또는 가상 서버 하니스 월드가 올린다)가 올라 있을 때만 일한다. 혼자 하는 게임에서는 아무것도 하지 않아 몸 이동이 예전처럼 프레임마다 돈다.
 *          고정 틱(`_tickInterval`)으로 돈다 — 예측과 서버 시뮬레이션이 같은 걸음 크기여야 재조정이 맞는다. 켜지면 몸 이동을 네트워크 몫으로 돌린다(`setNetDriven`).
 *          **선의 입력은 폰 의도(`ControlIntent`) 하나다**(언리얼 `PlayerController` → 서버 RPC · 유니티 Netcode `ICommandData` 자리). 예전의 자기 입력 바이트(이동 · 달리기 · 점프)는 지웠다 —
 *          사격 · 무기 버튼과 시선이 몸 이동과 같은 길로 서버에 닿는다.
 *          - 서버: 이 몸의 조종 연결은 처음 붙은 클라이언트 하나다(아레나에 플레이어 몸이 하나라서 — 여럿은 몸 스폰과 함께 할 일). 틱마다 그 연결의 다음 입력 틱을 꺼내
 *            (`ReplicationServer::popIntent`, 늦으면 지난 의도를 되풀이) `ShooterBodyMovementComponent::simulate` 로 움직이고, 상태를 소유자 상태(그 입력 틱과 한 쌍)와
 *            엔티티로 싣는다. 꺼낸 의도는 원격 조종자(`RemoteControllerComponent`)에도 넣어 서버의 폰이 그 의도로 쏘고 무기를 바꾼다. 엔티티 자리는 관심 영역 정책(`InterestReplicationPolicy`)에 알린다.
 *          - 클라이언트: 틱마다 폰 의도를 보내고(`ReplicationClient::sendIntent`) 바로 같은 함수로 예측한다(`ClientPrediction::record`).
 *            새 소유자 상태가 오면 그 입력 틱의 기록과 비교해(`reconcile`) 다르면 남은 입력을 다시 시뮬레이션하고, 보정 전후 자리 차이를 `PredictionSmoother` 로 녹인다.
 *            발동 비트는 한 프레임만 참이다 — 입력 틱이 없던 프레임의 발동을 프레임마다 모아 다음 입력 틱에 싣는다.
 *          **명중은 서버가 정한다(랙 보정).** 서버는 틱마다 산 적의 자리를 기록하고(`LagCompensationHistory`), 서버의 플레이어가 쏘면 그 의도를 만들 때 클라이언트가 보던 서버 틱
 *          (`ReplicationServer::getClientViewTick`)으로 되감아 명중을 본다(`raycastRewound`). 맞은 몸은 되감은 차이만큼 광선을 옮겨 실제 히트박스(래그돌 바디 — 히트 존 배율)에 다시 묻는다.
 *          판정 결과(맞힌 발 수 · 체력)는 소유자 상태에 실어 클라이언트 HUD 가 서버의 값을 보인다(`ShooterPlayerComponent::applyServerReport`).
 *          **근접은 주장을 확인한다.** 클라이언트가 예측 스윕으로 맞힌 적을 의도 입력에 주장으로 싣고(`queueMeleeClaim`), 서버는 그 입력을 처리한 프레임의 자기 스윕 기록(`MeleeSweepHistory`)과
 *          클라이언트가 보던 틱으로 되감은 적 자리를 맞춰 인정하면 맞음을 전달한다. 휘두르지 않은 주장은 기록이 없어 거절된다.
 *          **적은 엔티티로 복제한다** — 서버가 산 적을 틱마다 싣고(자리는 관심 영역 정책에 알린다), 클라이언트의 감독이 받은 것을 보간해 복제 모습으로 그린다(`collectReplicatedEnemies`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Ray.h"
#include "Core/Network/Replication/PredictionSmoother.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "Engine/Character/Hit/MeleeSweepHistory.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ClientPrediction.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/InterestReplicationPolicy.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/LagCompensation.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"

#include "Games/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Shooter3D/ShooterGameModeComponent.h"

namespace sw
{
    struct CharacterRayHit;

    class GameObjectManager;
    class PawnComponent;
    class RemoteControllerComponent;
    class SceneNetLink;

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
        /** @brief 적의 복제 엔티티 타입입니다. */
        static constexpr uint32 kEnemyTypeID = 2;
        /** @brief 조종을 맡고 아직 입력을 받지 못했다. 클라이언트 입력 틱은 1 부터다. */
        static constexpr uint32 kInputTickUnknown = 0;

        ShooterNetPawnComponent();
        ~ShooterNetPawnComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        bool   isActive() const { return _role != Role::None; }
        bool   isServer() const { return _role == Role::Server; }
        bool   isClient() const { return _role == Role::Client; }
        uint32 getCorrectionCount() const { return _prediction.getCorrectionCount(); }

        /**
         * @brief 서버의 사격 판정입니다. 지금 폰 의도를 만들 때 클라이언트가 보던 틱으로 되감아 광선 @p ray 가 맞힌 몸을 찾고, 되감은 차이만큼 광선을 옮겨 실제 히트박스에 묻습니다(틱 뒤 · 게임 스레드).
         * @details 되감은 몸에 닿지 않으면(클라이언트 화면에 그 자리에 없었으면) 지금 자리에 있어도 맞지 않는다. 맞은 바디가 그 몸의 것이 아니면 맞지 않는다.
         * @param maxDistance 막는 상자 · 바닥까지의 거리입니다.
         * @return 맞았으면 true 이고 @p outHit 에 맞은 바디입니다(거리는 옮긴 광선 기준이며 옮긴 길이와 같은 방향의 값이다).
         */
        [[nodiscard]] bool raycastRewound( const GameObjectManager& manager, const Ray& ray, float32 maxDistance, uint32 layerMask, uint64 ignoreObjectID, CharacterRayHit& outHit );
        /** @brief 예측 스윕이 맞힌 적을 다음 입력 틱에 서버로 주장합니다(클라이언트, 틱 뒤). */
        void queueMeleeClaim( uint32 enemyEntityID );
        /** @brief 입력에 실어 보낸 맞음 주장 수입니다(클라이언트). */
        uint32 getSentClaimCount() const { return _sentClaimCount; }
        /** @brief 인정한 맞음 주장 수입니다(서버). */
        uint32 getConfirmedClaimCount() const { return _confirmedClaimCount; }
        /** @brief 거절한 맞음 주장 수입니다(서버 — 기록에 그 휘두름이 없거나 표적이 칼날 밖이거나 표적이 사라졌다). */
        uint32 getRejectedClaimCount() const { return _rejectedClaimCount; }
        /** @brief 렌더 시각의 적을 모읍니다(클라이언트 — 앞뒤 스냅샷을 보간한다). */
        void collectReplicatedEnemies( vector<ShooterReplicatedEnemy>& outListEnemy ) const;
        /** @brief 랙 보정으로 판정한 광선 수입니다(서버). */
        uint32 getRewoundShotCount() const { return _rewoundShotCount; }
        /** @brief 되감아 맞힌 광선 수입니다(서버). */
        uint32 getRewoundHitCount() const { return _rewoundHitCount; }
        /** @brief 마지막 판정이 되감은 서버 틱 수입니다(서버). 클라이언트의 보간 지연 + 왕복 지연만큼입니다. */
        float32 getLastRewindTicks() const { return _lastRewindTicks; }

        /** @brief 걸음 상태를 바이트로 씁니다(엔티티의 몸). */
        static void               writeState( const ShooterMoveState& state, vector<uint8>& outBytes );
        [[nodiscard]] static bool readState( const vector<uint8>& bytes, ShooterMoveState& outState );
        /** @brief 두 상태가 같다고 볼 만큼 가까운가입니다(재조정 판정 — 자리 `_reconcileTolerance` 안, 땅 여부가 같음). */
        bool isClose( const ShooterMoveState& left, const ShooterMoveState& right ) const;
        /** @brief 소유자 상태를 바이트로 씁니다 — 걸음 상태 뒤에 서버 판정 결과(맞힌 발 수 · 체력). */
        static void               writeOwnerState( const ShooterMoveState& state, uint32 confirmedHitCount, float32 health, vector<uint8>& outBytes );
        [[nodiscard]] static bool readOwnerState( const vector<uint8>& bytes, ShooterMoveState& outState, uint32& outConfirmedHitCount, float32& outHealth );

    private:
        enum class Role : uint8
        {
            None = 0,
            Server,
            Client
        };

        /** @brief 서버가 확인을 기다리는 맞음 주장 하나입니다. 그 프레임의 스윕이 기록될 때까지 두 프레임쯤 기다린다. */
        struct PendingClaim
        {
            MeleeHitClaim _claim{}; ///< `_frame` 은 그 입력을 꺼낸 장부 프레임이다(클라이언트가 보낸 입력 틱에서 옮겼다)
            float32       _viewTick{ 0.0f };
        };

        void                            stepServer( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox );
        void                            stepClient( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox );
        ShooterBodyMovementComponent*   findMovement() const;
        const PawnComponent*            findPawn() const;
        const ShooterGameModeComponent* findGameMode() const;
        /** @brief 이번 서버 틱의 산 적을 랙 보정 기록과 복제 엔티티에 적습니다. */
        void recordEnemies();
        /** @brief 기다린 맞음 주장 가운데 그 프레임의 스윕이 기록된 것을 확인합니다(틱 뒤 · 게임 스레드). */
        void resolveClaims();
        /** @brief 지금 폰 의도를 만들 때 클라이언트가 보던 서버 틱입니다(조종 시스템이 이번 프레임에 읽은 입력 틱의 스탬프). */
        float32                    findShotViewTick() const;
        RemoteControllerComponent* findRemoteController() const;
        /** @brief 서버의 폰을 원격 조종자에게 넘깁니다(틱 뒤 · 게임 스레드 — 조종자 오브젝트를 만든다). */
        void possessRemote();
        /** @brief 입력 틱이 없는 프레임의 발동까지 놓치지 않게 이번 프레임 폰 의도의 발동을 모읍니다(클라이언트, 프레임마다). */
        void collectPendingTriggers( const ShooterBodyMovementComponent& movement );
        /** @brief 이 입력 틱에 보낼 의도를 만들고 모은 발동을 비웁니다. 멈춘 몸(입력 초점이 없는 월드)은 시선만 남긴 빈 의도입니다. */
        ControlIntent                  takeNetIntent( const ShooterBodyMovementComponent& movement );
        const vector<ShooterArenaBox>* findArenaBoxes() const;
        /** @brief 이 컴포넌트의 월드(오브젝트 매니저)의 넷 링크입니다. 오브젝트가 매니저에 없으면 nullptr 입니다. */
        const SceneNetLink* findLink() const;

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

        ReplicationServer                                 _server;
        ReplicationClient                                 _client;
        InterestReplicationPolicy                         _interest;
        Subscription                                      _routerSubscription; ///< 세션 라우터에 단 `_server` 또는 `_client` 의 등록(처리기보다 뒤에 선언 — 먼저 풀린다)
        ClientPrediction<ShooterMoveState, ControlIntent> _prediction;
        PredictionSmoother                                _smoother;
        vector<uint8>                                     _bytesScratch;
        vector<MeleeHitClaim>                             _listClaim;        ///< 클라이언트 — 다음 입력에 실을 주장, 서버 — 이번 입력에서 꺼낸 주장
        vector<PendingClaim>                              _listPendingClaim; ///< 서버 — 확인을 기다리는 주장
        vector<LagRecord>                                 _listLagScratch;
        vector<uint32>                                    _listEnemyEntityID; ///< 서버 — 지난 틱에 복제한 적(사라지면 관심 영역에서도 뺀다)
        vector<uint32>                                    _listEntityScratch;
        LagCompensationHistory                            _lagHistory;       ///< 서버 — 틱마다 산 적의 자리
        TickRingBuffer<float32>                           _viewTickByInput;  ///< 서버 — 입력 틱마다 클라이언트가 보던 서버 틱
        GameObjectHandle                                  _remoteController; ///< 서버 — 폰을 쥔 원격 조종자 오브젝트
        float32                                           _accumulator;
        float32                                           _pendingTriggerYaw;   ///< 클라이언트 — 모은 발동 순간의 시선
        float32                                           _pendingTriggerPitch; ///< 클라이언트 — 모은 발동 순간의 시선
        uint32                                            _pendingTriggered;    ///< 클라이언트 — 다음 입력 틱에 실을 발동 비트
        uint32                                            _tick;                ///< 서버 — 시뮬레이션 틱, 클라이언트 — 예측한 입력 틱
        uint32                                            _nextInputTick;       ///< 서버 — 조종 연결에서 다음에 꺼낼 입력 틱
        uint32                                            _reconciledTick;      ///< 클라이언트 — 마지막으로 맞춘 소유자 상태의 입력 틱
        uint32                                            _rewoundShotCount;    ///< 서버 — 랙 보정으로 판정한 광선 수
        uint32                                            _rewoundHitCount;     ///< 서버 — 되감아 맞힌 광선 수
        float32                                           _lastRewindTicks;     ///< 서버 — 마지막 판정이 되감은 틱 수
        uint32                                            _sentClaimCount;      ///< 클라이언트 — 보낸 주장 수
        uint32                                            _confirmedClaimCount; ///< 서버 — 인정한 주장 수
        uint32                                            _rejectedClaimCount;  ///< 서버 — 거절한 주장 수
        int32                                             _controllerID;        ///< 서버 — 이 몸을 조종하는 연결(-1 이면 없다)
        Role                                              _role;
        uint8                                             _bPendingTriggerLook : 1; ///< 클라이언트 — `_pendingTriggerYaw` · `_pendingTriggerPitch` 가 채워졌다
        uint8                                             _bRemoteRequested    : 1; ///< 서버 — 원격 조종자 빙의를 틱 뒤로 예약했다
        uint8                                             _bClaimsScheduled    : 1; ///< 서버 — 주장 확인을 틱 뒤로 예약했다
        uint8                                             _reserved            : 5;
    };
} // namespace sw
