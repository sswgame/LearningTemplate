/**
 * @file PairedAnimationReplication.h
 * @brief 권위 서버가 시작한 페어드 애니메이션 세션(`PairedAnimationSystem`)을 클라이언트에 복제합니다.
 * @details 이 기능은 키트 `GF_NetPairedAnimation` 이 제공하며 자기 메시지 영역(`NetKitMessageRange::kPairedAnimation`)을 쓴다. 권위 방식과 상관없이 `NetEndpoint` 위에 얹는다.
 *
 *          - **시작은 서버가 결정한다.** 서버는 재생 중인 세션을 처음 본 갱신에 상태 메시지 하나(`kSessionState`)를 모두에게 보낸다. 메시지는 에셋 경로,
 *            역할마다 참가 오브젝트 번호, 세션 스냅숏(`PairedAnimationSession::captureSnapshot` — 서버의 실제 진입 시각)이다. 클라이언트는 그것으로
 *            바인딩을 만들어 `PairedAnimationSystem::restoreSession` 을 부른다. 시작 틱만 보내고 클라이언트가 진입 시각을 추정하는 길(`makeAtElapsedTime`)은 쓰지 않는다
 *            — 서버의 진입은 프레임 단위라 추정은 최대 한 프레임 틀린다.
 *          - **늦은 참가.** 연결이 열리면 다음 갱신에서 재생 중인 세션마다 그때의 상태를 그 연결에만 보낸다(같은 메시지).
 *          - **끝.** 서버에서 세션이 끝나거나(`Finished` · `Aborted`) 목록에서 빠지면 끝 메시지(`kSessionEnd`)를 보내고, 클라이언트는 아직 재생 중이면 중단한다
 *            (남은 역할은 각자 `exitState` 로 블렌드 — 클립이 끝난 세션과 같은 규칙이다).
 *          - **오브젝트 번호.** 씬의 번호표(`NetObjectTable` — 파괴 복제와 같은 표)의 번호다. 씬 오브젝트는 이름 해시, 플레이 중에 생긴 오브젝트(투사체)는
 *            서버가 매기고 게임의 스폰 복제가 클라이언트에 묶은 생성 번호다. 번호가 없는 오브젝트(묶지 않은 생성 오브젝트, 이름이 겹친 씬 오브젝트)가 참가한 세션은
 *            복제할 수 없어 서버가 오류를 남기고 보내지 않는다 — 다른 오브젝트에 조용히 묶지 않는다.
 *          - **롤백.** 소유 상태(`ReplicationServer::setOwnerState`)에 세션을 실으려면 게임이 `PairedAnimationNetState::write` 의 바이트를 싣고, 되감은 뒤
 *            `PairedAnimationReplicationClient::applyState` 를 부른다. 같은 id 가 재생 중이면 중단하고 다시 복원한다(서버 상태가 기준이다).
 *          - 트랜스폼은 서버 권위다. 복원은 정렬(`snap` · `warp`)을 하지 않고 트랜스폼 복제가 맞춘다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Message/NetMessageTable.h"
#include "Core/Network/NetTypes.h"

#include "Engine/Character/PairedAnimation/PairedAnimationSession.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSnapshot.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/NetObjectID.h"
#include "Engine/Network/NetMessageBody.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class GameObjectManager;
    class INetPeers;
    class PairedAnimationSystem;
    class SkeletalAnimatorComponent;

    /** @brief 페어드 애니메이션 선 형식의 상한입니다. 세션 상태 칸 선언(`PROPERTY( MaxCount )`)이 쓴다. */
    struct PairedAnimationNetLimit
    {
        static constexpr int32 kMaxAssetPathBytes = static_cast<int32>( constant::file::kMaxPathSize ) - 1; ///< 에셋 경로 바이트 상한(끝 0 자리 제외)
        static constexpr int32 kMaxRoleCount      = PairedAnimationSessionSnapshot::kMaxRoleCount;          ///< 역할 수 상한 — 스냅숏의 역할 상한과 같다
        static constexpr int32 kMaxSnapshotBytes  = 2048;                                                   ///< 스냅숏 바이트 상한(역할 하나가 9 바이트 남짓 — 역할 수백 개까지)
    };

    /**
     * @brief 세션 상태 메시지(`NetPairedAnimationMessage::SessionState`) · 롤백 소유 상태의 바이트입니다. 칸 선언(`Replicated` · `MaxCount`)이 곧 바이트다(`ReplicatedLayout`).
     * @details 예전에는 `PairedAnimationNetState::write` · `read` 가 같은 차례 · 상한을 손으로 맞췄다. 스냅숏은 엔진 형식(`PairedAnimationSessionSnapshot::writeBytes` — 그쪽도 칸 선언)의 바이트로 싣는다.
     *          칸을 바꾸면 `SessionState` 의 본문 판을 올린다 — 손 본문이라 판이 스키마 해시를 따라가지 않는다.
     */
    REFLECT()
    struct PairedAnimationSessionStateBody
    {
        REFLECT_BODY();

        PROPERTY( Replicated, MaxCount = PairedAnimationNetLimit::kMaxAssetPathBytes )
        string _assetPath{};

        PROPERTY( Replicated, MaxCount = PairedAnimationNetLimit::kMaxRoleCount )
        vector<NetObjectID> _listRoleNetID{}; ///< 에셋의 역할 순서, 0 은 바인딩 없음

        PROPERTY( Replicated, MaxCount = PairedAnimationNetLimit::kMaxSnapshotBytes, Min = 0, Max = 255 )
        vector<uint8> _snapshotBytes{}; ///< `PairedAnimationSessionSnapshot::writeBytes`
    };

    /** @brief 세션 끝(서버 → 클라이언트) 본문입니다. */
    REFLECT()
    struct PairedAnimationSessionEndBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _sessionID{ 0 };
    };

    /**
     * @brief 페어드 애니메이션 키트의 메시지 표입니다. 종류 · 마스크 · 와이어 버전은 표에서 나온다(`NetMessageTable`).
     * @details 세션 상태는 롤백 소유 상태에도 실리는 공개 형식(`PairedAnimationNetState::write`)이라 손 본문이고 바이트는 칸 선언(`PairedAnimationSessionStateBody`)이다.
     *          세션 끝은 고정 배치형(`NetMessageBodyDef`)이다.
     */
    struct NetPairedAnimationMessage
    {
        static constexpr uint32 kHoldMilliseconds = 5000;                                                                                                                                                      ///< 참가자가 클립을 읽는 동안 상태를 기다리는 시간. 끝은 같은 세션의 상태를 앞지르지 않는다
        using SessionState                        = NetHeldMessageDef<NetMessageDef<0, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered>, kHoldMilliseconds>;                                    ///< 세션 상태(시작 · 늦은 참가). 세션 번호(보류의 열쇠) 뒤를 칸 선언(`PairedAnimationSessionStateBody`)으로 쓴다
        using SessionEnd                          = NetHeldMessageDef<NetMessageBodyDef<1, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, PairedAnimationSessionEndBody>, kHoldMilliseconds>; ///< 세션 끝 — 상태 메시지 뒤에 닿아야 한다
        using Table                               = NetWorldMessageTable<NetKitMessageRange::kPairedAnimation, SessionState, SessionEnd>;

        static constexpr uint8 kSessionState = Table::getKindOf<SessionState>();
        static constexpr uint8 kSessionEnd   = Table::getKindOf<SessionEnd>();
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PairedAnimationNetState
     * @brief 세션 하나의 복제 상태입니다 — 에셋 경로, 역할마다 참가 오브젝트 번호(에셋의 역할 순서, 0 은 바인딩 없음), 세션 스냅숏.
     */
    struct SW_KIT_API PairedAnimationNetState
    {
        string                         _assetPath{};
        vector<NetObjectID>            _listRoleNetID{};
        PairedAnimationSessionSnapshot _snapshot{};

        /**
         * @brief 본문(`PairedAnimationSessionStateBody`)에 씁니다. 경로가 비었으면 쓰지 않고 false, 경로 · 역할 수 · 스냅숏이 상한(`PairedAnimationNetLimit`)을 넘으면
         *        false 이고 @p writer 에 쓰다 만 비트가 남습니다(메시지를 버린다).
         */
        [[nodiscard]] bool write( BitWriter& writer ) const;
        /** @brief `write` 가 쓴 본문을 읽습니다. 깨졌으면 false 이고 @p outState 는 비웁니다. 값이 에셋에 맞는지는 복원이 봅니다. */
        [[nodiscard]] static bool read( BitReader& reader, PairedAnimationNetState& outState );
    };
} // namespace sw

namespace sw
{
    /** @brief 보낸 · 받은 횟수입니다. 테스트와 확인에 씁니다. */
    struct PairedAnimationReplicationStats
    {
        uint32 _stateSentCount{ 0 };      ///< 보낸 상태 메시지(연결 하나에 하나로 센다 — 모두에게 보내면 1)
        uint32 _endSentCount{ 0 };        ///< 보낸 끝 메시지
        uint32 _unreplicableCount{ 0 };   ///< 서버 — 복제할 수 없어 보내지 않은 세션(경로 없는 에셋, 표에 없는 참가자)
        uint32 _sendRejectedCount{ 0 };   ///< 서버 — 연결이 메시지를 거절한 횟수
        uint32 _appliedCount{ 0 };        ///< 클라이언트 — 복원한 세션
        uint32 _rejectedCount{ 0 };       ///< 클라이언트 — 복원이 거절된 상태(오류 로그)
        uint32 _endAppliedCount{ 0 };     ///< 클라이언트 — 끝 메시지로 중단한 세션
        uint32 _deliveryFailedCount{ 0 }; ///< 서버 — 클라이언트가 참가자를 기다리다 잇지 못했다고 알려 온 메시지
    };
} // namespace sw

namespace sw
{
    /**
     * @class PairedAnimationReplicationServer
     * @brief 서버 쪽 — 재생 중인 세션을 처음 본 갱신에 상태를, 끝난 세션에 끝을 보내고, 들어온 연결에는 재생 중인 세션의 지금 상태를 보냅니다.
     * @code
     *     server.initialize( &host, &manager );
     *     _subscription = router.registerHandler( &server ); // 연결 사건(늦은 참가). 구독이 사라지면 뗀다
     *     server.update();                   // 세션 갱신(프레임 시스템 단계) 뒤 — 같은 프레임에 보낸다
     * @endcode
     */
    class SW_KIT_API PairedAnimationReplicationServer : public NetWorldMessageHandler
    {
    public:
        PairedAnimationReplicationServer();

        /** @brief @p pManager 의 씬 오브젝트를 번호표(`NetObjectTable::captureSceneObjects`)에 잡고 시작합니다. 보낸 기록은 비웁니다. */
        void initialize( INetPeers* pHost, GameObjectManager* pManager );
        /** @brief 끝난 세션의 끝, 새 세션의 상태, 들어온 연결의 상태 순서로 보냅니다(게임 스레드). */
        void update();

        uint8  getMessageRangeBase() const override { return NetPairedAnimationMessage::Table::kRangeBase; }
        uint32 getWireVersion() const override { return NetPairedAnimationMessage::Table::computeWireVersion(); } // 보내기만 한다 — 보내는 표의 판을 낸다
        /** @brief 받는 종류가 없습니다(월드에 들고 남만 받습니다). */
        uint16                  getMessageKindMask() const override { return 0; }
        NetMessageDirectionMask getMessageDirectionMask() const override { return NetMessageDirectionMask{}; }
        NetHandleResult         handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 다음 `update` 에서 재생 중인 세션마다 상태를 그 연결에 보냅니다. */
        void onPeerEnteredWorld( int32 connectionID ) override;
        void onPeerLeftWorld( int32 connectionID ) override;

        /**
         * @brief 재생 중인 세션의 복제 상태를 만듭니다. 메시지와 롤백 소유 상태(`ReplicationServer::setOwnerState` 에 `PairedAnimationNetState::write` 의 바이트)가 함께 씁니다.
         * @details 에셋을 리소스 경로에서 읽지 않았거나, 참가자에게 번호(`NetObjectTable`)가 없거나, 재생 중이 아니면 false 이고 앞의 둘은 오류를 남깁니다.
         */
        [[nodiscard]] bool makeSessionState( const PairedAnimationSession& session, PairedAnimationNetState& outState ) const;

        /** @brief 클라이언트가 참가자를 기다리다 세션을 잇지 못했습니다 — 세고 알린다. */
        void                                   onMessageDeliveryFailed( int32 connectionID, uint8 kind, uint64 key, NetDeliveryFailure reason ) override;
        const PairedAnimationReplicationStats& getStats() const { return _stats; }

    private:
        /** @brief 상태 메시지를 씁니다. 쓰지 못하면(경로 · 역할 수 상한) 오류를 남기고 false 입니다. */
        [[nodiscard]] bool writeStateMessage( const PairedAnimationNetState& state );
        void               sendEnds( const PairedAnimationSystem* pSystem );
        void               sendNewSessions( const PairedAnimationSystem& system );
        void               sendToJoining( const PairedAnimationSystem& system );
        bool               hasAnnounced( uint32 sessionID ) const;
        bool               hasRefused( uint32 sessionID ) const;

    private:
        vector<uint32>                  _listAnnouncedID; ///< 상태를 보냈고 아직 끝을 보내지 않은 세션
        vector<uint32>                  _listRefusedID;   ///< 복제할 수 없어 보내지 않은 세션(오류를 한 번만 남긴다)
        vector<int32>                   _listJoiningConnection;
        PairedAnimationReplicationStats _stats;
        NetMessageWriter                _writer;
        INetPeers*                      _pHost;
        GameObjectManager*              _pManager;
    };
} // namespace sw

namespace sw
{
    /**
     * @class PairedAnimationReplicationClient
     * @brief 클라이언트 쪽 — 받은 상태로 세션을 복원하고, 끝 메시지에 중단합니다. 메시지는 게임 프레임 앞(`GameNetSession::update`, 게임 스레드 · 틱 밖)에서 받습니다.
     * @code
     *     client.initialize( &manager );
     *     _subscription = router.registerHandler( &client );
     * @endcode
     */
    class SW_KIT_API PairedAnimationReplicationClient : public NetTableHandler<PairedAnimationReplicationClient, NetPairedAnimationMessage::Table, NetPairedAnimationMessage::SessionState, NetPairedAnimationMessage::SessionEnd>
    {
    public:
        PairedAnimationReplicationClient();

        /** @brief @p pManager 의 씬 오브젝트를 번호표(`NetObjectTable::captureSceneObjects`)에 잡고 시작합니다. */
        void initialize( GameObjectManager* pManager );
        /**
         * @brief 복제 상태로 세션을 복원합니다(게임 스레드, 틱 밖). 받은 메시지와 롤백(되감은 뒤 소유 상태)이 함께 씁니다.
         * @details 같은 id 가 재생 중이면 먼저 중단하고 복원한다(서버 상태가 기준). 에셋을 읽지 못하면 `AssetNotLoaded`, 역할 수가 에셋과 다르면 `InvalidSnapshot`,
         *          번호의 오브젝트가 없으면 `ParticipantMissing` 이고 모두 오류를 남긴다. 그 밖은 `PairedAnimationSystem::restoreSession` 의 결과다.
         *          참가자가 클립을 아직 읽는 중이면 오류 없이 `ParticipantNotReady` 다 — 메시지는 라우터가 보류했다가 다시 준다(`NetHeldMessageDef`).
         */
        [[nodiscard]] PairedAnimationStartResult applyState( const PairedAnimationNetState& state );

        // 표의 메시지
        NetHandleResult receive( NetPairedAnimationMessage::SessionState, const NetMessageContext& context, BitReader& body );
        NetHandleResult receive( NetPairedAnimationMessage::SessionEnd, const NetMessageContext& context, const PairedAnimationSessionEndBody& body );
        /** @brief 보류의 열쇠는 세션 번호다 — 같은 세션의 상태 · 끝끼리 순서를 지킨다. */
        uint64 readHoldKey( NetPairedAnimationMessage::SessionState, BitReader& body ) const;
        uint64 readHoldKey( NetPairedAnimationMessage::SessionEnd, BitReader& body ) const;

        const PairedAnimationReplicationStats& getStats() const { return _stats; }

    private:
        PairedAnimationReplicationStats _stats;
        GameObjectManager*              _pManager;
    };
} // namespace sw
