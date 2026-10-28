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
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/NetTypes.h"

#include "Engine/Character/PairedAnimation/PairedAnimationSession.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSnapshot.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class GameObjectManager;
    class INetPeers;
    class PairedAnimationSystem;
    class SkeletalAnimatorComponent;

    /** @brief 페어드 애니메이션 메시지 종류입니다. */
    struct NetPairedAnimationMessage
    {
        static constexpr uint8 kSessionState = NetKitMessageRange::kPairedAnimation + 0; ///< 서버 → 세션 상태(시작 · 늦은 참가). 신뢰, 순서 있음
        static constexpr uint8 kSessionEnd   = NetKitMessageRange::kPairedAnimation + 1; ///< 서버 → 세션 끝(id). 신뢰, 순서 있음 — 상태 메시지 뒤에 닿아야 한다
        static_assert( NetMessageRange::isInRange( kSessionEnd, NetKitMessageRange::kPairedAnimation ), "message kinds must stay inside the kit's range" );
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
        /** @brief 스냅숏 바이트의 상한입니다(역할 하나가 5 바이트 남짓 — 역할 수백 개까지). 넘으면 쓰지 않습니다. */
        static constexpr int32 kMaxSnapshotBytes = 2048;
        /** @brief 역할 수의 상한입니다. 깨진 바이트가 큰 수를 말해도 그만큼 잡지 않습니다. */
        static constexpr uint32 kMaxRoleCount = 64;

        string                         _assetPath{};
        vector<uint32>                 _listRoleNetID{};
        PairedAnimationSessionSnapshot _snapshot{};

        /** @brief 본문에 씁니다. 경로가 `constant::kMaxPathSize` 이상이거나 역할 수가 상한을 넘으면 쓰지 않고 false 입니다. */
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
        uint32 _stateSentCount{ 0 };    ///< 보낸 상태 메시지(연결 하나에 하나로 센다 — 모두에게 보내면 1)
        uint32 _endSentCount{ 0 };      ///< 보낸 끝 메시지
        uint32 _unreplicableCount{ 0 }; ///< 서버 — 복제할 수 없어 보내지 않은 세션(경로 없는 에셋, 표에 없는 참가자)
        uint32 _sendRejectedCount{ 0 }; ///< 서버 — 연결이 메시지를 거절한 횟수
        uint32 _appliedCount{ 0 };      ///< 클라이언트 — 복원한 세션
        uint32 _rejectedCount{ 0 };     ///< 클라이언트 — 복원이 거절된 상태(오류 로그)
        uint32 _endAppliedCount{ 0 };   ///< 클라이언트 — 끝 메시지로 중단한 세션
        uint32 _deferredCount{ 0 };     ///< 클라이언트 — 참가자가 클립을 읽는 중이라 보관해 둔 상태(정상 거절, 오류 아님)
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
    class SW_KIT_API PairedAnimationReplicationServer : public INetMessageHandler
    {
    public:
        PairedAnimationReplicationServer();

        /** @brief @p pManager 의 씬 오브젝트를 번호표(`NetObjectTable::captureSceneObjects`)에 잡고 시작합니다. 보낸 기록은 비웁니다. */
        void initialize( INetPeers* pHost, GameObjectManager* pManager );
        /** @brief 끝난 세션의 끝, 새 세션의 상태, 들어온 연결의 상태 순서로 보냅니다(게임 스레드). */
        void update();

        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kPairedAnimation; }
        /** @brief 받는 종류가 없습니다(연결 사건만 받습니다). */
        uint16          getMessageKindMask() const override { return 0; }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        /** @brief 다음 `update` 에서 재생 중인 세션마다 상태를 그 연결에 보냅니다. */
        void onConnectionOpened( int32 connectionID ) override;
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;

        /**
         * @brief 재생 중인 세션의 복제 상태를 만듭니다. 메시지와 롤백 소유 상태(`ReplicationServer::setOwnerState` 에 `PairedAnimationNetState::write` 의 바이트)가 함께 씁니다.
         * @details 에셋을 리소스 경로에서 읽지 않았거나, 참가자에게 번호(`NetObjectTable`)가 없거나, 재생 중이 아니면 false 이고 앞의 둘은 오류를 남깁니다.
         */
        [[nodiscard]] bool makeSessionState( const PairedAnimationSession& session, PairedAnimationNetState& outState ) const;

        const PairedAnimationReplicationStats& getStats() const { return _stats; }

    private:
        /** @brief 상태 메시지를 씁니다. 쓰지 못하면(경로 · 역할 수 상한) 오류를 남기고 false 입니다. */
        [[nodiscard]] bool writeStateMessage( const PairedAnimationNetState& state );
        void               sendEnds( const PairedAnimationSystem* pSystem );
        void               sendNewSessions( const PairedAnimationSystem& system );
        void               sendToJoining( const PairedAnimationSystem& system );
        bool               hasAnnounced( uint32 sessionID ) const;
        bool               hasRefused( uint32 sessionID ) const;

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
    class SW_KIT_API PairedAnimationReplicationClient : public INetMessageHandler
    {
    public:
        PairedAnimationReplicationClient();

        /** @brief @p pManager 의 씬 오브젝트를 번호표(`NetObjectTable::captureSceneObjects`)에 잡고 시작합니다. */
        void initialize( GameObjectManager* pManager );
        /**
         * @brief 복제 상태로 세션을 복원합니다(게임 스레드, 틱 밖). 받은 메시지와 롤백(되감은 뒤 소유 상태)이 함께 씁니다.
         * @details 같은 id 가 재생 중이면 먼저 중단하고 복원한다(서버 상태가 기준). 에셋을 읽지 못하면 `AssetNotLoaded`, 역할 수가 에셋과 다르면 `InvalidSnapshot`,
         *          번호의 오브젝트가 없으면 `ParticipantMissing` 이고 모두 오류를 남긴다. 그 밖은 `PairedAnimationSystem::restoreSession` 의 결과다.
         *          참가자가 클립을 아직 읽는 중이면(`ParticipantNotReady`, 정상 거절) 오류 없이 상태를 보관하고(같은 세션 id 의 옛 보관은 바꾼다) 그 결과를 돌려준다 —
         *          `retryPending` 이 준비된 뒤 적용한다.
         */
        [[nodiscard]] PairedAnimationStartResult applyState( const PairedAnimationNetState& state );

        /** @brief 보관한 상태를 기다린 프레임 상한입니다. 이 프레임 수를 넘게 준비되지 않으면 오류를 남기고 버립니다(무한히 기다리지 않는다). */
        static constexpr uint32 kMaxPendingFrames = 300;
        /**
         * @brief 보관한 상태를 다시 적용합니다(게임 스레드, 틱 밖 — 프레임에 한 번). 준비됐으면 복원하고, 아직이면 프레임을 세며 두고, 상한(`kMaxPendingFrames`)을 넘으면
         *        기다린 프레임 수와 함께 오류를 남기고 버립니다. 서버 상태가 기준이라 복원 시각은 보관한 상태의 시각이다(기다린 만큼 서버를 뒤따르다 서버 보정을 받는다).
         */
        void retryPending();
        /** @brief 준비를 기다리며 보관한 상태의 수입니다. */
        uint32 getPendingCount() const { return static_cast<uint32>( _listPending.size() ); }

        uint8  getMessageRangeBase() const override { return NetKitMessageRange::kPairedAnimation; }
        uint16 getMessageKindMask() const override
        {
            constexpr uint8 kBase = NetKitMessageRange::kPairedAnimation;
            return static_cast<uint16>( ( 1u << ( NetPairedAnimationMessage::kSessionState - kBase ) ) | ( 1u << ( NetPairedAnimationMessage::kSessionEnd - kBase ) ) );
        }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;

        const PairedAnimationReplicationStats& getStats() const { return _stats; }

    private:
        /** @brief 준비를 기다리는 상태 하나와 기다린 프레임 수입니다. */
        struct PendingState
        {
            PairedAnimationNetState _state{};
            uint32                  _waitedFrames{ 0 };
        };

        /** @brief 상태를 바로 복원합니다. 참가자가 준비되지 않았으면 오류 없이 `ParticipantNotReady` 를 돌려줍니다(보관은 부르는 쪽이 한다). */
        [[nodiscard]] PairedAnimationStartResult applyStateNow( const PairedAnimationNetState& state );
        /** @brief 세션 id 의 보관한 상태를 버립니다(더 새로운 상태가 왔거나 끝 메시지를 받았다). */
        void dropPending( uint32 sessionID );

        PairedAnimationReplicationStats _stats;
        vector<PendingState>            _listPending;
        GameObjectManager*              _pManager;
    };
} // namespace sw
