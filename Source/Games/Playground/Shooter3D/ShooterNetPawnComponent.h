/**
 * @file ShooterNetPawnComponent.h
 * @brief 슈터 플레이어 몸의 네트워크 이동입니다 — 서버 권위 + 내 폰만 예측 · 재조정 + 보정 녹이기.
 * @details 이 컴포넌트의 월드에 넷 링크(`SceneNetLink` — 전역 세션 또는 가상 서버 하니스 월드가 올린다)가 올라 있을 때만 일한다. 혼자 하는 게임에서는 아무것도 하지 않아 몸 이동이 예전처럼 프레임마다 돈다.
 *          고정 틱(`_tickInterval`)으로 돈다 — 예측과 서버 시뮬레이션이 같은 걸음 크기여야 재조정이 맞는다. 켜지면 몸 이동을 네트워크 몫으로 돌린다(`setNetDriven`).
 *          **선의 입력은 폰 의도(`ControlIntent`) 하나다**(언리얼 `PlayerController` → 서버 RPC · 유니티 Netcode `ICommandData` 자리). 예전의 자기 입력 바이트(이동 · 달리기 · 점프)는 지웠다 —
 *          사격 · 무기 버튼과 시선이 몸 이동과 같은 길로 서버에 닿는다.
 *          - 서버: **접속마다 몸 하나다**(언리얼 DM 의 접속마다 폰 · 유니티 NGO 플레이어 프리팹). 자리 0 은 씬의 플레이어 몸이고 나머지 자리(`_listGuestSpawn`)는 접속이 앉을 때 그 몸의 설정을 옮겨
 *            세우고 나가면 거둔다(`ServerPawn`). 걸음 · 스폰은 남의 오브젝트라 모두 틱 뒤(게임 스레드)에 한다. 틱마다 자리의 연결에서 다음 입력 틱을 꺼내
 *            (`ReplicationServer::popNextIntent`, 온 만큼 흐른 시간의 몫 안에서 — 오지 않았으면 그 자리) `ShooterBodyMovementComponent::simulate` 로 움직이고, 상태를 소유자 상태(그 입력 틱과 한 쌍)와
 *            엔티티로 싣는다. 꺼낸 의도는 원격 조종자(`RemoteControllerComponent`)에도 넣어 폰의 시선 · 감시 값이 그 의도를 따른다(사격 · 무기는 걸음이 한다). 엔티티 자리는 관심 영역 정책(`InterestReplicationPolicy`)에 알린다.
 *          - 클라이언트: 틱마다 폰 의도를 보내고(`ReplicationClient::sendIntent`) 바로 같은 함수로 예측한다(`ClientPrediction::record`).
 *            새 소유자 상태가 오면 그 입력 틱의 기록과 비교해(`reconcile`) 다르면 남은 입력을 다시 시뮬레이션하고, 보정 전후 자리 차이를 `PredictionSmoother` 로 녹인다.
 *            발동은 한 프레임의 사건이다 — 입력 틱이 없던 프레임의 사건을 프레임마다 모아 다음 입력 틱에 싣는다(사건이 자기 시선 · 보던 시각을 든다).
 *          **이동과 발동은 명령 하나의 한 걸음이다** — 서버 · 예측 모두 이동한 자리에서 바로 무기 · 근접을 판정한다(`ShooterPlayerComponent::stepNetCommand`). 합친 폰 의도로 쏘지 않는다.
 *          **명중은 서버가 정한다(랙 보정).** 서버는 틱마다 산 적의 자리를 기록하고(`LagCompensationHistory`), 서버의 플레이어가 쏘면 발동이 들고 온 보던 서버 틱
 *          (`ControlTrigger::getViewTick` — 서버가 자른 값)으로 되감아 명중을 본다(`raycastRewound`). 히트박스(래그돌 바디 — 히트 존 배율)는 바디마다 과거 자세에서 지금 자세로 광선을 옮겨 묻는다(포즈까지 되감는다).
 *          판정 결과(맞힌 발 수 · 체력)는 소유자 상태에 실어 클라이언트 HUD 가 서버의 값을 보인다(`ShooterPlayerComponent::applyServerReport`).
 *          **근접은 주장을 확인한다.** 클라이언트가 예측 스윕으로 맞힌 적을 의도 입력에 주장으로 싣고(`queueMeleeClaim`), 서버는 그 입력을 처리한 프레임의 자기 스윕 기록(`MeleeSweepHistory`)과
 *          클라이언트가 보던 틱으로 되감은 적 자리를 맞춰 인정하면 맞음을 전달한다. 휘두르지 않은 주장은 기록이 없어 거절된다.
 *          **남의 몸도 엔티티로 복제한다** — 클라이언트는 자기 것이 아닌 몸(`ShooterPawnNetState`)을 보간해 몸 프리팹의 모습에 둔다. 다른 플레이어도 서버의 랙 보정 기록(캡슐)으로 맞고, 쓰러지면 잠시 뒤
 *          자기 자리에서 다시 태어난다. 매치는 정해진 횟수(`ShooterGameModeComponent::getMatchDownLimit`)만큼 쓰러진 사람의 판을 끝낸다(점수는 계정마다).
 *          **적은 엔티티로 복제한다** — 서버가 산 적을 틱마다 싣고(자리는 관심 영역 정책에 알린다), 클라이언트의 감독이 받은 것을 보간해 복제 모습으로 그린다(`collectReplicatedEnemies`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
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

#include "Games/Playground/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Playground/Shooter3D/ShooterGameModeComponent.h"

namespace sw
{
    struct CharacterRayHit;

    class GameObject;
    class GameObjectManager;
    class INetPeers;
    class PawnComponent;
    class RemoteControllerComponent;
    class ShooterPlayerComponent;
} // namespace sw

namespace sw
{
    /**
     * @brief 적 하나의 엔티티 상태입니다. 칸 선언(`Replicated` · 범위 · `Step`)이 곧 바이트다 — 쓰기 · 읽기 짝을 손으로 맞추지 않는다(`ReplicatedLayout`).
     * @details 양자화는 범위 + 간격(`Step`)이고 비트 수는 그 둘에서 나온다 — 언리얼 Iris 의 범위 양자화 실수 서술자(범위 + 비트 수)와 같은 자리이되, 간격을 적어 정밀도가 단위(m · 라디안)로 읽힌다.
     *          자리 · 요는 클라이언트 보간 모습에만 쓰므로 양자화한다(명중은 서버의 랙 보정 기록이 정한다). 칸을 바꾸면 와이어 버전이 바뀐다(`PlaygroundGame::onRegisterReplicatedTypes`).
     */
    REFLECT()
    struct ShooterEnemyNetState
    {
        REFLECT_BODY();

        PROPERTY( Replicated, Min = -4096, Max = 4096, Step = 0.01 )
        float3 _position{ 0.0f, 0.0f, 0.0f }; ///< 발(m, 1 cm 간격)

        PROPERTY( Replicated, Min = -3.2, Max = 3.2, Step = 0.001 )
        float32 _yaw{ 0.0f }; ///< 라디안. 쓰는 쪽이 [-π, π] 로 감는다

        PROPERTY( Replicated, Min = 0, Max = 4 )
        uint8 _phase{ 0 }; ///< `ShooterEnemyPhase`(Rising ~ Dying). 끝 값은 cpp 의 static_assert 가 열거와 맞춘다

        PROPERTY( Replicated )
        uint8 _kindIndex{ 0 }; ///< 적 종류(`ShooterGameModeComponent::kEliteKindIndex` 는 정예)

        PROPERTY( Replicated )
        uint32 _appearanceSeed{ 0 };

        PROPERTY( Replicated )
        uint8 _teleportCount{ 0 }; ///< 순간 이동 번호 — 다른 두 표본은 잇지 않는다(`NetSampleRatio`)
    };

    /** @brief 플레이어 몸 하나의 엔티티 상태입니다(남에게 보이는 모습). 보간해 그리기만 하므로 양자화한다 — 판정은 서버의 랙 보정 기록이 한다. */
    REFLECT()
    struct ShooterPawnNetState
    {
        REFLECT_BODY();

        PROPERTY( Replicated, Min = -4096, Max = 4096, Step = 0.01 )
        float3 _position{ 0.0f, 0.0f, 0.0f }; ///< 발(절대, m, 1 cm 간격)

        PROPERTY( Replicated, Min = -3.2, Max = 3.2, Step = 0.001 )
        float32 _yaw{ 0.0f }; ///< 보는 요(라디안). 쓰는 쪽이 [-π, π] 로 감는다

        PROPERTY( Replicated, Min = 0, Max = 2 )
        uint8 _weaponIndex{ 0 }; ///< 무기 슬롯. 범위는 cpp 의 static_assert 가 `ShooterPlayerComponent::kWeaponCount` 와 맞춘다

        PROPERTY( Replicated, Min = 0, Max = 1 )
        uint8 _bAlive{ SW_TRUE };

        PROPERTY( Replicated, Min = 0, Max = 1 )
        uint8 _bOnGround{ SW_TRUE };

        PROPERTY( Replicated )
        uint8 _teleportCount{ 0 }; ///< 순간 이동 · 다시 태어남 번호 — 다른 두 표본은 잇지 않는다(`NetSampleRatio`)
    };

    /**
     * @brief 받는 클라이언트가 조종하는 몸의 소유자 상태입니다 — 걸음 상태와 서버 판정 결과(맞힌 발 수 · 처치 수 · 체력)를 한 선언으로 싣는다.
     * @details 예전에는 걸음 상태 뒤에 두 값을 손으로 이어 쓰고 읽었다(순서가 어긋나도 컴파일됐다). 체력은 HUD 가 서버 값을 그대로 보이므로 양자화하지 않는다.
     */
    REFLECT()
    struct ShooterOwnerNetState
    {
        REFLECT_BODY();

        PROPERTY( Replicated )
        ShooterMoveState _move{};

        PROPERTY( Replicated )
        uint32 _confirmedHitCount{ 0 };

        PROPERTY( Replicated )
        uint32 _killCount{ 0 }; ///< 서버가 센 이 사람의 처치 수(적 · 다른 플레이어)

        PROPERTY( Replicated )
        uint32 _pawnEntityID{ 0 }; ///< 이 사람의 몸 엔티티 — 클라이언트가 남의 몸과 가린다

        PROPERTY( Replicated, Min = 0, Max = 1 )
        uint8 _bOut{ SW_FALSE }; ///< 매치에서 정해진 횟수만큼 쓰러졌다 — 이 사람의 판이 끝났다

        PROPERTY( Replicated, Min = -100000, Max = 100000 )
        float32 _health{ 0.0f }; ///< 범위는 NaN · 무한대를 읽기에서 거절하는 경계다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShooterNetPawnComponent
     * @brief 플레이어 오브젝트에 두는 네트워크 이동입니다. 같은 오브젝트의 `ShooterBodyMovementComponent` · `ShooterPlayerComponent`(감독의 막는 상자)를 씁니다.
     */
    REFLECT( Category = "Shooter", DisplayName = "Shooter Net Pawn", Tooltip = "Server-authoritative body movement with client prediction, reconciliation and correction smoothing" )
    class ShooterNetPawnComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( ShooterNetPawnComponent );

        /** @brief 자리 0 의 몸 엔티티 id 입니다. 자리 n 의 몸은 이 수 + n 이고 적은 `kEnemyEntityIDBase` 부터다. */
        static constexpr uint32 kPawnEntityID = 1;
        /** @brief 한 서버의 자리 상한입니다(몸 엔티티 id 를 적과 나누는 폭). */
        static constexpr uint32 kMaxSeat           = 8;
        static constexpr uint32 kEnemyEntityIDBase = kPawnEntityID + kMaxSeat;
        /** @brief 플레이어 몸의 복제 엔티티 타입입니다. 복제 타입 번호는 게임(Playground)에서 하나다 — 1 ~ 3 은 허브 서버(`HubEntityType`)의 것이고 사격장은 16 부터 쓴다. */
        static constexpr uint32 kPawnTypeID = 16;
        /** @brief 적의 복제 엔티티 타입입니다. */
        static constexpr uint32 kEnemyTypeID = 17;
        /** @brief 소유자 상태(`ShooterOwnerNetState`)의 복제 스키마를 핸드셰이크 목록에 올리는 번호입니다. 엔티티 타입이 아니며 위 둘과 겹치지 않는다. */
        static constexpr uint32 kOwnerStateSchemaID = 18;
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
         * @brief 서버의 사격 판정입니다. 발동이 들고 온 보던 시각 @p viewTick 으로 되감아 광선 @p ray 가 맞힌 몸을 찾습니다 — 히트박스 바디마다 광선을 과거 자세에서 지금 자세로 옮겨 실제 바디에 묻습니다.
         * @details 되감은 몸에 닿지 않으면(클라이언트 화면에 그 자리에 없었으면) 지금 자리에 있어도 맞지 않는다. 맞은 바디가 그 몸의 것이 아니면 맞지 않는다.
         *          다른 플레이어의 몸은 되감은 캡슐이 곧 맞음 상자다 — 맞으면 @p outHit 의 오브젝트가 그 몸이다.
         * @param shooter 쏜 몸입니다(그 자리의 연결이 보던 틱으로 되감는다).
         * @param maxDistance 막는 상자 · 바닥까지의 거리입니다.
         * @return 맞았으면 true 이고 @p outHit 에 맞은 바디입니다(거리는 옮긴 광선 기준이며 옮긴 길이와 같은 방향의 값이다).
         */
        [[nodiscard]] bool raycastRewound( const GameObjectManager& manager, const GameObject& shooter, const Ray& ray, float32 maxDistance, uint32 layerMask, uint64 ignoreObjectID, ControlViewTick viewTick, CharacterRayHit& outHit );
        /**
         * @brief 서버의 근접 판정 가운데 다른 플레이어 몫입니다 — 이번 프레임의 칼날(@p listPrevious → @p listCurrent 의 점마다, 그리고 지금 칼날)이 닿은 몸을 모읍니다(씬 자리, 틱 뒤 · 게임 스레드).
         * @details 사격과 같은 되감은 캡슐이다(휘두른 클라이언트가 보던 틱). 적은 물리 히트박스(`MeleeHit::sweep`)가 맡으므로 들지 않는다. 같은 몸이 겹쳐 들 수 있다.
         */
        void collectRewoundMelee( const GameObjectManager& manager, const GameObject& shooter, const vector<float3>& listPrevious, const vector<float3>& listCurrent, float32 radius, ControlViewTick viewTick, vector<GameObjectHandle>& outListTarget ) const;
        /** @brief 몸 @p pawnObject 를 조종하는 계정의 매치 점수를 순위표에 냅니다(서버, 틱 뒤 · 게임 스레드). 판이 끝날 때 플레이어가 부릅니다 — 클라이언트 · 매치가 아닌 판은 아무 일도 없다. */
        void reportMatchScore( const GameObject& pawnObject );
        /** @brief 매치 서버의 판이면 true 입니다(활성 미니게임이 매치이고 이 월드가 서버다). */
        bool isMatchServer() const;
        /** @brief 몸 @p pawnObject 를 조종하는 연결이 있으면 true 입니다(서버). */
        bool hasController( const GameObject& pawnObject ) const;
        /** @brief 판을 두는 몸을 모읍니다 — 서버는 연결이 조종하는 몸들(하나도 없으면 씬의 몸), 그 밖에는 씬의 몸 하나입니다. */
        void collectPlayerObjects( vector<GameObjectHandle>& outListObject ) const;
        /** @brief 지금 보이는 남의 몸 수입니다(클라이언트). */
        uint32 getRemotePawnCount() const { return static_cast<uint32>( _listRemotePawn.size() ); }
        /** @brief @p from 에서 가장 가까운 남의 몸의 발입니다(클라이언트). 보이는 남이 없으면 비어 있습니다. */
        optional<AbsolutePosition> findNearestRemotePawn( const AbsolutePosition& from ) const;
        /** @brief 입력 틱 @p commandTick 의 예측 스윕이 맞힌 적을 다음 입력에 실어 서버로 주장합니다(클라이언트, 틱 뒤). */
        void queueMeleeClaim( uint32 enemyEntityID, uint32 commandTick );
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
        float64 getLastRewindTicks() const { return _lastRewindTicks; }

        /** @brief 두 상태가 같다고 볼 만큼 가까운가입니다(재조정 판정 — 자리 `_reconcileTolerance` 안, 땅 여부가 같음). */
        bool isClose( const ShooterMoveState& left, const ShooterMoveState& right ) const;
        /** @brief 소유자 상태를 바이트로 씁니다. 칸이 범위 밖(NaN 포함)이면 false 입니다. */
        [[nodiscard]] static bool writeOwnerState( const ShooterOwnerNetState& state, vector<uint8>& outBytes );
        /** @brief 소유자 상태를 읽습니다. 깨졌거나 범위 밖이면 false 입니다. */
        [[nodiscard]] static bool readOwnerState( const vector<uint8>& bytes, ShooterOwnerNetState& outState );

    private:
        enum class Role : uint8
        {
            None = 0,
            Server,
            Client
        };

        /** @brief 한 자리가 확인을 기다리는 맞음 주장의 상한입니다. 넘는 주장은 거절한다(보내는 쪽이 줄을 늘릴 수 없다). */
        static constexpr size_t kMaxPendingClaim = 64;

        /** @brief 서버 — 자리 하나입니다. 자리 0 의 몸은 씬의 플레이어, 나머지는 접속이 앉을 때 세운 오브젝트다. */
        struct ServerPawn
        {
            GameObjectHandle        _object{};           ///< 몸. 세우기 전이거나 거뒀으면 비었다
            GameObjectHandle        _remoteController{}; ///< 폰을 쥔 원격 조종자 오브젝트
            vector<MeleeHitClaim>   _listPendingClaim{}; ///< 확인을 기다리는 맞음 주장 — 그 걸음 안팎의 입력을 모두 처리한 뒤 본다
            TickRingBuffer<float64> _viewTickByInput{};  ///< 입력 틱마다 클라이언트가 보던 서버 틱(서버가 자른 값)
            optional<uint32>        _lastInputTick{};    ///< 이번 서버 틱에 꺼낸 마지막 입력 틱
            uint64                  _accountID{ 0 };     ///< 조종 연결의 주체(접속 티켓의 계정, 0 이면 없다)
            uint64                  _leftAccountID{ 0 }; ///< 나간 계정 — `finishLeftPlayer` 가 점수 · 빈 자리를 처리한다
            uint32                  _nextInputTick{ 0 }; ///< 마지막으로 꺼낸 입력 틱의 다음(`kInputTickUnknown` 이면 아직)
            int32                   _connectionID{ constant::invalid_index::kInt32 };
            uint8                   _bLeft{ SW_FALSE };          ///< 연결이 떠났고 나감 처리를 기다린다(그 사이 새 연결을 앉히지 않는다)
            uint8                   _bBodyRequested{ SW_FALSE }; ///< 이 연결의 몸 · 원격 조종자를 세워 봤다(실패해도 되풀이하지 않는다)
            uint8                   _bScoreReported{ SW_FALSE }; ///< 이 계정의 매치 점수를 냈다(한 사람에 한 번)
        };

        /** @brief 클라이언트 — 남의 몸 모습 하나입니다. */
        struct RemotePawn
        {
            GameObjectHandle _object{};
            AbsolutePosition _position{};
            uint32           _entityID{ 0 };
            uint8            _weaponIndex{ 0 };
            uint8            _bArmed{ SW_FALSE }; ///< 몸의 손에 무기 아이템을 쥐였다
            uint8            _bSeen{ SW_FALSE };  ///< 이번 프레임의 보이는 목록에 있다
        };

        /** @brief 의도 하나로 몸을 한 걸음 옮깁니다(서버). */
        void stepMovement( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox, const ControlIntent& intent ) const;
        /** @brief 클라이언트의 이번 프레임 걸음들과 남의 몸 · 화면 시각입니다(틱 뒤 · 게임 스레드) — 서버와 같은 자리에서 같은 순서로 돈다. */
        void runClientFrame( GameObjectManager& manager );
        void stepClient( GameObjectManager& manager, ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox );
        // 서버(틱 뒤 · 게임 스레드 — 여러 몸을 옮기고 세우고 거둔다)
        void runServerFrame( GameObjectManager& manager );
        void stepServer( GameObjectManager& manager, const vector<ShooterArenaBox>& listBox );
        /** @brief 끊긴 연결의 자리를 비우고 새 연결을 빈 자리에 앉힙니다. */
        void assignSeats( INetPeers& peers );
        void stepServerPawn( GameObjectManager& manager, ServerPawn& pawn, const vector<ShooterArenaBox>& listBox );
        /** @brief 몸의 엔티티 · 관심 자리를 싣습니다(`endTick` 앞). 판을 두지 않는 몸은 싣지 않습니다. */
        void publishPawnStates( const GameObjectManager& manager );
        /** @brief 이번 틱에 입력이 온 자리에 소유자 상태를 보냅니다(`endTick` 뒤). */
        void sendOwnerStates( const GameObjectManager& manager );
        /** @brief 나간 자리를 정리하고 새로 앉은 자리의 몸 · 원격 조종자를 세웁니다. */
        void syncServerPawnList( GameObjectManager& manager );
        /** @brief 자리 @p seatIndex 의 몸을 씬의 플레이어 설정으로 세웁니다. 세우지 못하면 오류를 남기고 빈 핸들입니다. */
        GameObjectHandle spawnServerBody( GameObjectManager& manager, size_t seatIndex ) const;
        /** @brief 자리의 폰을 원격 조종자에게 넘깁니다(있던 조종자면 새 연결이 이어 맡는다). */
        void possessRemote( GameObjectManager& manager, ServerPawn& pawn ) const;
        /** @brief 나간 사람의 판을 끝냅니다 — 점수를 내고, 빈 자리를 맡기고, 몸을 거두거나 처음으로 되돌립니다. 매치에 아무도 남지 않았으면 판을 처음부터 다시 엽니다. */
        void finishLeftPlayer( GameObjectManager& manager, size_t seatIndex );
        bool hasConnectedPlayer() const;
        /** @brief 판을 두는 자리입니다 — 연결이 조종하거나, 아무도 없는 서버의 씬 몸이다. */
        bool              isPlaying( const ServerPawn& pawn ) const;
        uint32            makePawnEntityID( const ServerPawn& pawn ) const { return kPawnEntityID + static_cast<uint32>( &pawn - _listServerPawn.data() ); }
        const ServerPawn* findServerPawn( GameObjectHandle object ) const;
        // 클라이언트
        /** @brief 받은 남의 몸을 모습에 옮기고, 새로 보이는 것을 세우고, 사라진 것을 거둡니다(틱 뒤 · 게임 스레드). */
        void                            syncRemotePawnList( GameObjectManager& manager );
        void                            clearRemotePawnList( GameObjectManager& manager );
        const ShooterPlayerComponent*   findPlayer() const;
        ShooterBodyMovementComponent*   findMovement() const;
        const PawnComponent*            findPawn() const;
        const ShooterGameModeComponent* findGameMode() const;
        /** @brief 역할을 놓고 몸을 혼자 하는 길로 되돌립니다(게임 스레드) — 플레이를 마칠 때, 또는 세션이 닫혀 처리기 등록이 풀렸을 때. */
        void stopRole();
        /** @brief 이번 서버 틱의 산 적 · 산 플레이어를 랙 보정 기록에, 적을 복제 엔티티에 적습니다. */
        void recordBodies( const GameObjectManager& manager );
        /** @brief 기다린 맞음 주장 가운데 그 프레임의 스윕이 기록된 것을 확인합니다. */
        void resolveClaims( GameObjectManager& manager, ServerPawn& pawn );
        /** @brief 매칭에 이 맵의 자리 수를 알립니다 — 넘는 사람을 같은 서버에 배정하지 않는다. */
        void reportSeatCapacity();
        /** @brief 계정 @p accountID 의 매치 점수를 정의의 순위표에 냅니다(서버 권위 제출 — `ILeaderboardScoreSender`). 매치가 아니거나 이미 냈으면 아무 일도 없다. */
        void reportMatchScore( ServerPawn& pawn, const ShooterPlayerComponent& player, uint64 accountID );
        /** @brief 적 @p object 의 히트박스 바디 자세를 지금 물리 씬에서 읽습니다(절대 자리, 에셋 바디 순서). 바디가 아직 없으면 비웁니다. */
        static void collectBodyPoses( const GameObjectManager& manager, const GameObject& object, vector<LagBodyPose>& outListBody );
        /** @brief 되감은 적 하나의 바디들에 광선을 물어 가장 가까운 맞음을 찾습니다. */
        [[nodiscard]] static bool         raycastRewoundBodies( const GameObjectManager& manager, const GameObject& object, const LagRecord& past, const Ray& ray, float32 maxDistance, uint32 layerMask, CharacterRayHit& outHit );
        static RemoteControllerComponent* findRemoteController( const GameObjectManager& manager, const ServerPawn& pawn );
        /** @brief 입력 틱이 없는 프레임의 발동까지 놓치지 않게 이번 프레임 폰 의도의 발동을 모읍니다(클라이언트, 프레임마다). */
        void collectPendingTriggers( const ShooterBodyMovementComponent& movement );
        /** @brief 이 입력 틱에 보낼 의도를 만들고 모은 발동을 비웁니다. 멈춘 몸(입력 초점이 없는 월드)은 시선만 남긴 빈 의도입니다. */
        ControlIntent                  takeNetIntent( const ShooterBodyMovementComponent& movement );
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
        PROPERTY( Category = "Network", DisplayName = "Guest Spawns", Units = m, Tooltip = "Feet position of each further player of a server; the scene body is the first seat, so the server seats one more than this list" )
        vector<float3> _listGuestSpawn;
        PROPERTY( Category = "Network", DisplayName = "Pawn Hit Height", Min = 0.001, Units = m, Tooltip = "Top of a player's hit capsule above the feet (server rewind)" )
        float32 _pawnHitHeight;

        ReplicationServer                                 _server;
        ReplicationClient                                 _client;
        InterestReplicationPolicy                         _interest;
        Subscription                                      _routerSubscription; ///< 세션 라우터에 단 `_server` 또는 `_client` 의 등록(처리기보다 뒤에 선언 — 먼저 풀린다)
        ClientPrediction<ShooterMoveState, ControlIntent> _prediction;
        PredictionSmoother                                _smoother;
        vector<uint8>                                     _bytesScratch;
        vector<MeleeHitClaim>                             _listClaim;      ///< 클라이언트 — 다음 입력에 실을 주장, 서버 — 이번 입력에서 꺼낸 주장
        vector<ServerPawn>                                _listServerPawn; ///< 서버 — 자리마다 하나(자리 0 이 씬의 몸)
        vector<RemotePawn>                                _listRemotePawn; ///< 클라이언트 — 보이는 남의 몸
        vector<int32>                                     _listConnectionScratch;
        vector<LagRecord>                                 _listLagScratch;
        vector<uint32>                                    _listEnemyEntityID; ///< 서버 — 지난 틱에 복제한 적(사라지면 관심 영역에서도 뺀다)
        vector<uint32>                                    _listEntityScratch;
        LagCompensationHistory                            _lagHistory; ///< 서버 — 틱마다 산 적 · 산 플레이어의 자리
        float32                                           _accumulator;
        vector<ControlTrigger>                            _listPendingTrigger;  ///< 클라이언트 — 다음 입력 틱에 실을 발동 사건
        uint32                                            _tick;                ///< 서버 — 시뮬레이션 틱, 클라이언트 — 예측한 입력 틱
        uint32                                            _pawnEntityID;        ///< 클라이언트 — 서버가 알려 준 내 몸의 엔티티(0 이면 아직)
        uint32                                            _reconciledTick;      ///< 클라이언트 — 마지막으로 맞춘 소유자 상태의 입력 틱
        uint32                                            _rewoundShotCount;    ///< 서버 — 랙 보정으로 판정한 광선 수
        uint32                                            _rewoundHitCount;     ///< 서버 — 되감아 맞힌 광선 수
        float64                                           _lastRewindTicks;     ///< 서버 — 마지막 판정이 되감은 틱 수
        uint32                                            _sentClaimCount;      ///< 클라이언트 — 보낸 주장 수
        uint32                                            _confirmedClaimCount; ///< 서버 — 인정한 주장 수
        uint32                                            _rejectedClaimCount;  ///< 서버 — 거절한 주장 수
        int32                                             _pendingStepCount;    ///< 이번 프레임 틱 뒤에 돌 고정 틱 수
        Role                                              _role;
        uint8                                             _bSeatsReported : 1; ///< 서버 — 자리 수를 매칭에 알렸다
        [[maybe_unused]] uint8                            _reserved       : 7;
    };
} // namespace sw
