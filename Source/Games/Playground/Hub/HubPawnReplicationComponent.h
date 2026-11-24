/**
 * @file HubPawnReplicationComponent.h
 * @brief 허브에 접속한 계정마다 폰을 세우고 그 움직임을 맞춥니다 — 서버 권위, 자기 폰(걷기 · 말)은 예측 · 재조정 · 보정 녹이기, 남의 폰과 말은 보간.
 * @details 허브 오브젝트(`HubWorldComponent`)에 함께 붙는다. 넷 링크가 걸리고 허브 링크(서버 `IHubServer` · 클라이언트 `HubClientLink`)가 선 뒤에만 일한다 — 혼자 하는 허브는 예전 그대로다.
 *          **선의 입력은 조종 의도(`ControlIntent`) 하나와 그 의도를 낸 폰의 엔티티 id 다** — 자리 · 속도 · 명중을 클라이언트가 보내지 않는다(언리얼 `ServerMove` 와 달리 클라이언트의 끝 자리도 싣지 않는다).
 *          - 서버: 들인 아바타(`IHubServer::collectAvatars`)마다 폰 프리팹을 세우고 원격 조종자(`RemoteControllerComponent::spawnForConnection`)로 소유 연결을 준다. 연결이 나가면 내리게 하고 거둔다.
 *            걸음은 도착한 입력만큼, 흐른 시간의 몫 안에서 돈다(`ReplicationServer::popNextIntent` — 속도 치트 · 끊김). 튜닝은 씬의 플레이어 몸에서 옮긴다(데이터가 한 벌).
 *            끝 자리는 허브 규칙이 다시 본다(`IHubServer::moveAvatar` — 경계 · 속도 한도). 받지 않으면 폰을 앞 자리로 되돌린다.
 *          - 클라이언트: 씬의 플레이어 폰이 자기 폰이다. 고정 틱마다 의도를 보내고 같은 함수(`simulateTick`)로 예측하며, 소유자 상태가 예측과 다르면 되감아 남은 입력을 다시 돌린다(`ClientPrediction`).
 *          - 말: 컨트롤러 없이 루트를 옮기는 탈것이라 걸음이 순수 함수다(`MountMovementComponent::simulate`) — 탄 쪽이 자기 폰처럼 예측한다. 타고 내리는 것은 서버만 정한다: 클라이언트의 상호작용 버튼은
 *            의도로 서버의 폰에 닿고, 서버 상태의 탈것 번호가 바뀌면 클라이언트가 자리를 맞춘다(언리얼 `Possess` → `ClientRestart`). 타지 않은 말은 서버 권위 + 보간이다.
 *          - 남: 폰 · 말은 엔티티(`HubPawnPose`)로 오고 렌더 시각의 앞뒤 스냅샷을 보간해 프리팹 모습에 둔다. 캡슐은 켜 둔다(막히는 느낌이 서버와 같게 — 자리는 보간 지연만큼 과거다).
 *          고정 틱은 **실제 시간**으로 센다 — 게임 시간은 배율 · 정지 · 시나리오의 고정 프레임이 걸려 서버와 다르게 흐른다.
 *          **자리는 모두 절대다** — 선의 상태 · 모습, 예측 기록, 허브 규칙(`moveAvatar` · 경계), 관심 영역. 씬의 몸(물리 · 트랜스폼)에 닿는 자리에서만 그 월드의 원점으로 옮긴다
 *          (`toLocalState` · `toAbsoluteState`). 서버와 클라이언트의 원점이 달라도, 예측 도중 원점이 옮겨져도 기록과 서버 상태가 같은 자리를 말한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Network/Replication/PredictionSmoother.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Character/Hit/MeleeSweepHistory.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ClientPrediction.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/InterestReplicationPolicy.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/MountMovementComponent.h"

#include "Games/Playground/Hub/HubNetLink.h"

namespace sw
{
    struct NetLinkValue;
    struct WorldOrigin;

    class ControlSystem;
    class GameObject;
    class GameObjectManager;
    class HubWorldComponent;
    class PawnComponent;

    /**
     * @brief 자기 폰 하나의 이동 상태입니다(자리는 절대) — 걷는 몸, 탄 말, 지금 어느 쪽인가. 소유자 상태의 바이트이자 예측 기록의 상태다(`ReplicatedLayout`, 양자화하지 않는다).
     * @details 칸을 바꾸면 와이어 버전이 바뀐다(`PlaygroundGame::onRegisterReplicatedTypes`).
     */
    REFLECT()
    struct HubPawnState
    {
        REFLECT_BODY();

        PROPERTY( Replicated )
        CharacterMoveState _walk{};

        PROPERTY( Replicated )
        MountMoveState _ride{}; ///< `_mountNumber` 가 0 이면 뜻이 없다

        PROPERTY( Replicated, Min = 0, Max = 8 )
        uint8 _mountNumber{ 0 }; ///< 탄 말의 번호(`_listMount` 차례 + 1). 0 이면 걷는다. 범위는 cpp 의 static_assert 가 `kMaxMount` 와 맞춘다

        PROPERTY( Replicated )
        int32 _floorTickOffset{ 0 }; ///< 움직이는 바닥의 틱 − 입력 틱. 서버가 연결마다 정한다 — 예측 · 다시 돌리기가 서버와 같은 틱의 바닥 위에서 걷는다

        PROPERTY( Replicated, Min = 0, Max = 1 )
        uint8 _bFloorTickKnown{ SW_FALSE }; ///< `_floorTickOffset` 이 정해졌다(아니면 바닥을 놓지 않는다)
    };

    /** @brief 남에게 보이는 폰 · 말 하나의 엔티티 상태입니다. 보간해 그리기만 하므로 양자화한다. */
    REFLECT()
    struct HubPawnPose
    {
        REFLECT_BODY();

        PROPERTY( Replicated, Min = -32768, Max = 32768, Step = 0.01 )
        float3 _position{}; ///< 폰은 루트(발 · 탔으면 안장), 말은 루트(절대, m, 1 cm 간격)

        PROPERTY( Replicated, Min = -3.2, Max = 3.2, Step = 0.001 )
        float32 _yaw{ 0.0f }; ///< 라디안, [-π, π] 로 감은 값

        PROPERTY( Replicated, Min = 0, Max = 8 )
        uint8 _mountNumber{ 0 }; ///< 폰 — 탄 말의 번호(0 이면 걷는다)

        PROPERTY( Replicated, Min = 0, Max = 4 )
        uint8 _gait{ 0 }; ///< 말 — `MountGait`

        PROPERTY( Replicated )
        uint8 _teleportCount{ 0 }; ///< 순간 이동 · 타고 내림 번호 — 다른 두 표본은 잇지 않는다(`NetSampleRatio`)

        PROPERTY( Replicated, Min = 0, Max = 16 )
        uint8 _floorNumber{ 0 }; ///< 폰 — 디딘 움직이는 바닥의 번호(`_listFloor` 차례 + 1, 0 이면 없다). 0 이 아니면 `_position` · `_yaw` 는 그 바닥 기준이다
    };

    /** @brief 입력 틱 하나입니다 — 의도와 그 의도를 낸 폰의 엔티티 id. 버튼 비트는 폰 스키마의 차례라 다른 폰의 것이면 움직임 없이(시선만) 쓴다. */
    struct HubPawnInput
    {
        ControlIntent _intent{};
        uint32        _pawnEntityID{ 0 };
        uint32        _inputTick{ 0 }; ///< 이 입력의 틱 — 상태의 `_floorTickOffset` 을 더하면 바닥의 틱이다
    };
} // namespace sw

namespace sw
{
    class TickedFloorStep;

    /**
     * @class HubPawnReplicationComponent
     * @brief 허브의 폰 복제입니다. 규칙은 파일 머리말을 보세요.
     */
    REFLECT( Category = "Hub", DisplayName = "Hub Pawn Replication", Tooltip = "A server-authoritative pawn per connected account; the own pawn and its horse are predicted, the others are interpolated" )
    class HubPawnReplicationComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( HubPawnReplicationComponent );

        /** @brief 소유자 상태(`HubPawnState`)의 복제 스키마를 핸드셰이크에 올리는 번호입니다. 허브의 MMO 엔티티 종류(`HubEntityType`)와 겹치지 않는다. */
        static constexpr uint32 kOwnerStateSchemaID = 4;
        static constexpr uint32 kPawnTypeID         = 5;  ///< 폰 엔티티(`HubPawnPose`, id 는 아바타 번호)
        static constexpr uint32 kMountTypeID        = 6;  ///< 말 엔티티(`HubPawnPose`)
        static constexpr uint32 kMaxMount           = 8;  ///< 씬에 둘 수 있는 말 수(`_mountNumber` 의 Max)
        static constexpr uint32 kMaxFloor           = 16; ///< 씬에 둘 수 있는 움직이는 바닥 수(`_floorNumber` 의 Max)
        /** @brief 말 n 의 엔티티 id 는 이 수 + n 입니다. 아바타 번호(`HubWorld::kPlacementIDBase` 아래) 위다. */
        static constexpr uint32 kMountEntityIDBase = 200000;

        HubPawnReplicationComponent();
        ~HubPawnReplicationComponent() override;

        void onEndPlay() override;
        /** @brief 넷 걸음을 틱 뒤(게임 스레드)로 미룹니다 — 여러 폰의 물리 이동과 스폰이다(`GameModeComponent` 의 규칙과 같은 자리). */
        void onTick( TickContext& context ) override;

        /** @brief 클라이언트가 자기 폰을 예측하고 있으면 1 입니다. */
        int32 getPredictedValue() const { return _localAvatarID != 0 ? 1 : 0; }
        /** @brief 예측이 서버와 달라 되감아 다시 돌린 횟수입니다. */
        uint32 getCorrectionCount() const { return _prediction.getCorrectionCount(); }

        /** @brief 소유자 상태를 바이트로 씁니다. 칸이 선언 범위 밖(NaN 포함)이면 false 입니다. */
        [[nodiscard]] static bool writeState( const HubPawnState& state, vector<uint8>& outBytes );
        /** @brief 소유자 상태를 읽습니다. 깨졌거나 범위 밖이면 false 이고 @p outState 는 그대로입니다. */
        [[nodiscard]] static bool readState( const vector<uint8>& bytes, HubPawnState& outState );
        /** @brief 절대 자리의 상태를 원점 @p origin 인 씬의 상태로 옮깁니다(몸에 넣기 전). 타지 않았으면 말 상태는 뜻이 없어 그대로입니다. */
        [[nodiscard]] static HubPawnState toLocalState( const WorldOrigin& origin, const HubPawnState& state );
        /** @brief 씬의 상태를 절대 자리의 상태로 옮깁니다(몸에서 읽은 뒤). */
        [[nodiscard]] static HubPawnState toAbsoluteState( const WorldOrigin& origin, const HubPawnState& state );
        /** @brief 두 상태가 같다고 볼 만큼 가까운가입니다 — 같은 몸(탈것 번호)이고 자리가 @p tolerance 안이며 이동 방식 · 걸음새가 같다. */
        static bool isClose( const HubPawnState& left, const HubPawnState& right, float32 tolerance );
        /** @brief 지금 디딘 바닥에서 자기 폰이 밀린 거리(m)입니다 — 바닥 위에서 걷지 않는데 이 값이 자라면 실려 가지 못한 것이다(감시 값). 바닥 위가 아니면 0 입니다. */
        float32 getFloorSlip() const { return _floorSlip; }
        /** @brief 자기 폰이 디딘 움직이는 바닥의 번호입니다(0 이면 없다). */
        int32 getFloorNumber() const { return static_cast<int32>( _floorNumber ); }
        /** @brief 그 바닥 기준의 자기 폰 자리입니다(`getFloorNumber` 가 0 이면 뜻이 없다). */
        const float3& getFloorLocalPosition() const { return _floorLocalPosition; }
        /** @brief 움직이는 바닥 위에 보이는 첫 남의 폰이 디딘 바닥의 번호입니다(0 이면 없다). */
        int32 getRemoteFloorNumber() const;
        /** @brief 그 남의 폰을 그린 자리입니다(바닥 기준). */
        float3 getRemoteFloorLocalPosition() const;

    private:
        enum class Role : uint8
        {
            None = 0,
            Server,
            Client
        };

        /** @brief 서버 — 접속 하나의 폰입니다. */
        struct ServerPawn
        {
            GameObjectHandle _object{};
            GameObjectHandle _controller{};       ///< 원격 조종자 오브젝트
            float3           _acceptedPosition{}; ///< 허브 규칙이 받은 마지막 자리(절대) — 받지 않은 걸음은 여기로 되돌린다
            uint32           _avatarID{ 0 };
            int32            _connectionID{ constant::invalid_index::kInt32 };
            uint8            _bSeen{ SW_FALSE };  ///< 이번 프레임의 접속 목록에 있다
            uint8            _teleportCount{ 0 }; ///< 규칙이 받은 자리로 되돌리거나 타고 내릴 때마다 오른다
            uint8            _mountNumber{ 0 };   ///< 마지막으로 낸 탈것 번호(바뀌면 순간 이동이다)
            int64            _floorTick{ 0 };     ///< 마지막 명령이 푼 바닥 틱(바닥 기준 자리를 이 자세로 잰다)
            int32            _floorTickOffset{ 0 };
            uint8            _bFloorTickKnown{ SW_FALSE };
        };

        /** @brief 클라이언트 — 남의 폰 모습 하나입니다. */
        struct RemotePawn
        {
            GameObjectHandle _object{};
            float3           _floorLocalPosition{}; ///< 그린 바닥 기준의 자리(`_floorNumber` 일 때)
            uint32           _entityID{ 0 };
            uint8            _floorNumber{ 0 };  ///< 디딘 바닥의 번호(0 이면 없다)
            uint8            _bSeen{ SW_FALSE }; ///< 이번 프레임의 보이는 목록에 있다
        };

        /** @brief 바닥 위에 보이는 첫 남의 폰입니다. 없으면 nullptr 입니다. */
        const RemotePawn* findRemoteRider() const;
        /** @brief 넷 걸음 한 프레임입니다(틱 뒤 · 게임 스레드). */
        void runNetFrame( GameObjectManager& manager );
        /** @brief 허브 링크가 섰으면 역할에 맞는 복제기를 한 번 세웁니다. */
        void startRole( const NetLinkValue& link, const HubWorldComponent& hub );
        /** @brief 복제기를 떼고 움직이던 몸을 되돌립니다. 두 번 불러도 됩니다. */
        void stopRole( GameObjectManager& manager );
        /** @brief 지난 프레임 뒤 흐른 실제 시간(초)입니다. */
        float32 consumeRealSeconds();
        /** @brief 쌓인 시간에서 이번 프레임에 돌 고정 틱 수를 냅니다. 따라잡지 못할 만큼 밀린 시간은 버린다. */
        int32 consumeTicks( float32 seconds );

        // 서버
        void runServerFrame( GameObjectManager& manager, const HubWorldComponent& hub, const NetLinkValue& link, float32 seconds );
        void syncServerPawnList( GameObjectManager& manager, const HubWorldComponent& hub );
        void spawnServerPawn( GameObjectManager& manager, const HubWorldComponent& hub, const HubAvatarSession& session );
        void removeServerPawn( GameObjectManager& manager, const ServerPawn& pawn );
        void stepServerPawn( GameObjectManager& manager, const HubWorldComponent& hub, ServerPawn& pawn );
        void publishPoses( GameObjectManager& manager );
        /** @brief 탄 말만 넷이 움직이게 하고 나머지는 말 AI 에게 돌려줍니다. */
        void syncServerMountList( GameObjectManager& manager );

        // 클라이언트
        void runClientFrame( GameObjectManager& manager, const HubWorldComponent& hub, float32 seconds );
        /** @brief 씬의 플레이어 폰을 자기 폰으로 잡고 예측을 시작합니다. 아직 몸이 서지 않았으면 false 이고 다음 프레임에 다시 합니다. */
        [[nodiscard]] bool startPrediction( GameObjectManager& manager, uint32 avatarID );
        void               stepClient( GameObjectManager& manager, const HubWorldComponent& hub, GameObject& pawnObject );
        /** @brief 서버가 정한 탈것 번호에 로컬 좌석을 맞춥니다. */
        void syncLocalMount( GameObjectManager& manager, GameObject& pawnObject, uint8 mountNumber );
        void collectPendingTriggers( const PawnComponent& controlledPawn, uint32 pawnEntityID );
        /** @brief 이 입력 틱에 보낼 입력을 만들고 모은 발동을 비웁니다. */
        HubPawnInput takeInput( const PawnComponent& controlledPawn, uint32 pawnEntityID );
        void         applyDisplay( GameObjectManager& manager, GameObject& pawnObject );
        void         updateRemotePawnList( GameObjectManager& manager );

        // 공용
        /** @brief 한 틱의 다음 상태입니다(절대 → 절대) — 서버 · 예측 · 다시 돌리기가 이 한 함수다(끝 자리는 허브 경계 안으로 자른다). */
        HubPawnState simulateTick( GameObjectManager& manager, const HubWorldComponent& hub, GameObject& pawnObject, const HubPawnState& state, const HubPawnInput& input, uint32 avatarID,
                                   CharacterMotionPass pass ) const;
        HubPawnState captureState( GameObjectManager& manager, const GameObject& pawnObject ) const;
        /** @brief 상태를 몸(걷는 몸 또는 탄 말)에 옮깁니다. */
        void        applyState( GameObjectManager& manager, GameObject& pawnObject, const HubPawnState& state ) const;
        GameObject* spawnPawnObject( GameObjectManager& manager, const utf8* pName, const float3& localPosition ) const;
        /** @brief 이 폰이 운전석에 앉은 말의 번호입니다(0 이면 앉지 않았다). */
        uint8                   findMountNumber( const GameObjectManager& manager, const GameObject& pawnObject ) const;
        MountMovementComponent* findMount( GameObjectManager& manager, uint8 mountNumber ) const;
        /** @brief 이 폰이 디딘 움직이는 바닥의 번호입니다(0 이면 없다). */
        uint8 findFloorNumber( const GameObjectManager& manager, const GameObject& pawnObject ) const;
        /** @brief 바닥 @p floorNumber 의 틱 @p tick 자세입니다. 없는 바닥이면 false 입니다. */
        [[nodiscard]] bool computeFloorPose( const GameObjectManager& manager, uint8 floorNumber, float64 tick, float3& outPosition, quaternion& outRotation ) const;
        /** @brief 네트 틱 @p netTick 을 씬의 바닥이 읽는 조종 틱으로 알립니다. */
        void publishFloorTick( ControlSystem& control, int64 netTick ) const;
        /** @brief 클라이언트가 지금 그리는 바닥 틱(소수)입니다. */
        float64                   computeDisplayFloorTick() const;
        void                      updateFloorSlip( GameObjectManager& manager, const GameObject& pawnObject, bool bWalking );
        static uint32             makePawnEntityID( uint32 avatarID, uint8 mountNumber ) { return mountNumber != 0 ? kMountEntityIDBase + mountNumber - 1u : avatarID; }
        [[nodiscard]] static bool writePose( const HubPawnPose& pose, vector<uint8>& outBytes );

        PROPERTY( Required, Category = "Network", DisplayName = "Pawn Prefab", Tooltip = "Body spawned for each connected account (on the server) and for each other player (on a client)" )
        AssetRef<PrefabAsset> _pawnPrefab;
        PROPERTY( Category = "Network", DisplayName = "Local Pawn", Tooltip = "The player object of this scene: a client predicts it; spawned pawns copy its movement tuning" )
        GameObjectHandle _localPawn;
        PROPERTY( Category = "Network", DisplayName = "Mounts", Tooltip = "Horses of this scene (at most 8), in an order the server and the clients share" )
        vector<GameObjectHandle> _listMount;
        PROPERTY( Category = "Network", DisplayName = "Moving Floors", Tooltip = "Floors that move as a function of the tick (at most 16), in an order the server and the clients share" )
        vector<GameObjectHandle> _listFloor;
        PROPERTY( Category = "Network", DisplayName = "Tick Interval", Min = 0.001, Units = s, Tooltip = "Fixed step of the networked movement; the server simulation and the client prediction use the same step" )
        float32 _tickInterval;
        PROPERTY( Category = "Network", DisplayName = "Smoothing Half Life", Min = 0.001, Units = s, Tooltip = "Time for half of a prediction correction to fade out on screen" )
        float32 _smoothingHalfLife;
        PROPERTY( Category = "Network", DisplayName = "Smoothing Snap Distance", Min = 0.001, Units = m, Tooltip = "Corrections larger than this are applied at once instead of fading (teleport)" )
        float32 _smoothingSnapDistance;
        PROPERTY( Category = "Network", DisplayName = "Reconcile Tolerance", Min = 0.0, Units = m, Tooltip = "Predicted and server positions closer than this count as the same" )
        float32 _reconcileTolerance;
        PROPERTY( Category = "Network", DisplayName = "Spawn Search Radius", Min = 0.0, Units = m, Tooltip = "A new pawn whose spot is blocked stands on a free spot within this distance" )
        float32 _spawnSearchRadius;
        PROPERTY( Category = "Network", DisplayName = "Interest Cell Size", Min = 0.001, Units = m, Tooltip = "Cell size of the server's interest grid" )
        float32 _interestCellSize;
        PROPERTY( Category = "Network", DisplayName = "Interest Enter Radius", Min = 0.0, Units = m, Tooltip = "Pawns and horses inside this radius of a client's pawn start replicating to it" )
        float32 _interestEnterRadius;
        PROPERTY( Category = "Network", DisplayName = "Interest Leave Radius", Min = 0.0, Units = m, Tooltip = "They stop replicating once outside this radius (not smaller than the enter radius)" )
        float32 _interestLeaveRadius;

        ReplicationServer                            _server;
        ReplicationClient                            _client;
        InterestReplicationPolicy                    _interest;
        Subscription                                 _routerSubscription; ///< 라우터에 단 `_server` 또는 `_client` 의 등록(처리기보다 뒤에 선언 — 먼저 풀린다)
        ClientPrediction<HubPawnState, HubPawnInput> _prediction;
        PredictionSmoother                           _smoother;
        vector<ServerPawn>                           _listServerPawn;
        vector<RemotePawn>                           _listRemotePawn;
        vector<HubAvatarSession>                     _listSession;       ///< 서버 — 이번 프레임의 들인 아바타(다시 쓴다)
        vector<MeleeHitClaim>                        _listClaimScratch;  ///< 서버 — 허브에는 근접 주장이 없다(꺼내는 형식이 요구한다)
        vector<uint32>                               _listEntityScratch; ///< 클라이언트 — 보이는 엔티티
        vector<uint8>                                _bytesScratch;
        TickedFloorStep*                             _pFloorStep;           ///< 이번 넷 프레임의 바닥 장치(프레임 동안만, 소유하지 않음)
        float3                                       _floorLocalPosition;   ///< 클라이언트 — 디딘 바닥 기준의 자리(밀림 감시)
        float32                                      _floorSlip;            ///< 클라이언트 — 디딘 바닥에서 밀린 거리
        int32                                        _floorTickOffset;      ///< 클라이언트 — 서버가 정해 준 바닥 틱 차이
        float3                                       _previousPosition;     ///< 클라이언트 — 자기 폰의 지난 틱 자리(절대, 그리기 보간)
        int64                                        _lastFrameNanoseconds; ///< 지난 넷 걸음 프레임의 실제 시각(0 이면 아직 없다)
        float32                                      _accumulator;
        vector<ControlTrigger>                       _listPendingTrigger;  ///< 클라이언트 — 다음 입력 틱에 실을 발동 사건
        uint32                                       _pendingPawnEntityID; ///< 클라이언트 — 모은 발동을 낸 폰(폰이 바뀌면 모은 것을 버린다)
        uint32                                       _stride;              ///< 이 컴포넌트의 틱 하나가 월드의 서버 틱 몇 개인지
        uint32                                       _tick;                ///< 서버 — 월드의 서버 시계에서 낸 시뮬레이션 틱, 클라이언트 — 예측한 입력 틱
        uint32                                       _reconciledTick;      ///< 클라이언트 — 마지막으로 맞춘 소유자 상태의 입력 틱
        uint32                                       _localAvatarID;       ///< 클라이언트 — 예측하는 자기 아바타(0 이면 아직 없다)
        Role                                         _role;
        uint8                                        _floorNumber;     ///< 클라이언트 — 디딘 바닥(밀림 감시)
        uint8                                        _bFloorTickKnown; ///< 클라이언트 — `_floorTickOffset` 을 받았다
        uint8                                        _bStartTried : 1; ///< 지금 링크로 복제기를 세워 봤다(실패해도 다시 하지 않는다 — 오류는 한 번 알린다)
        [[maybe_unused]] uint8                       _reserved    : 7;
    };
} // namespace sw
