/**
 * @file SequenceReplication.h
 * @brief 서버의 시퀀스 트리거(`SequenceTriggerComponent`)가 튼 컷씬 재생을 클라이언트에 복제합니다.
 * @details 이 기능은 키트 `GF_NetSequence` 가 제공하며 자기 메시지 영역(`NetKitMessageRange::kSequence`)을 쓴다. 꼴은 `GF_NetPairedAnimation` 과 같다(시작 · 늦은 참가 · 끝).
 *
 *          - **판정은 서버가 한다.** 트리거 계약의 진입은 모든 월드가 내지만 시퀀스 트리거는 클라이언트 월드에서 틀지 않는다(`SequenceTriggerComponent` 머리말 — 언리얼 `HasAuthority`).
 *            서버는 갱신마다 트리거 등록 칸에서 발동 수가 오른 트리거를 찾아 시작 메시지(재생 번호, 디렉터 · 활성자 오브젝트 번호, 대상, 서버의 지금 프레임)를 보낸다.
 *          - **대상.** `Everyone` 은 연결된 모두, `Activator` 는 활성자 폰을 가진 연결 하나(`PawnComponent::findInputConnection` — 언리얼 `Client` RPC 의 소유 연결)다.
 *            가진 연결이 없는 폰(리슨 서버 호스트의 폰)이면 원격에 보낼 곳이 없다. 여러 플레이어의 `Activator` 재생은 함께 돈다 — 서버는 플레이어마다 시간만 재는 디렉터를 두고
 *            (`SequenceTriggerComponent::getPlays`), 클라이언트에는 씬의 디렉터 번호를 보내 자기 월드의 디렉터 하나로 튼다.
 *          - **시각.** 시작은 서버 틱을 싣고, 받은 클라이언트는 그 뒤 흐른 시간만큼 바로 진행한다(`SequencePlayerComponent::advance` — 그 사이 이벤트도 쏜다). 지연 · 재전송만큼 늦게
 *            시작하면 지연이 다른 클라이언트끼리, 그리고 서버의 판(문 · 입력 막기)과 어긋난다.
 *          - **늦은 참가.** 클라이언트가 월드에 들어오면 다음 갱신에서 재생 중인 `Everyone` 재생마다 그때의 프레임으로 시작을 그 연결에만 보낸다 — 클라이언트는 그 프레임부터 튼다
 *            (`SequencePlayerComponent::playFromFrame`, 지난 이벤트는 쏘지 않는다).
 *          - **끝.** 서버의 디렉터가 재생을 멈추면(정상 끝 · 건너뛰기 · 멈춤) 끝 메시지를 대상에게 보내고, 클라이언트는 아직 재생 중이면 끝으로 건너뛴다(`skipToEnd`).
 *          - **건너뛰기는 서버가 검증한다.** 클라이언트의 로컬 건너뛰기(`Cutscene.Skip`)는 그 클라이언트에서 바로 끝나고, `Activator` 재생이면 요청(재생 번호)을 서버에 보낸다.
 *            서버는 그 재생이 살아 있고 대상이 `Activator` 이며 요청한 연결이 그 폰을 가진 연결일 때만 서버의 재생을 끝낸다 — 다른 연결 · 모르는 번호 · `Everyone` 재생의 요청은
 *            셈만 하고 버린다(클라이언트가 보낸 값을 믿지 않는다). 클라이언트가 재생을 시작하게 하는 메시지는 없다.
 *          - **오브젝트 번호.** 씬의 번호표(`NetObjectTable`)다 — 디렉터 · 활성자에 번호가 없으면 서버가 오류를 남기고 보내지 않는다(다른 오브젝트에 조용히 묶지 않는다).
 *          - **진행 플래그.** 서버만 적는다(트리거의 `_audience` 가 판 상태 · 플레이어 상태를 고른다). 클라이언트 월드는 틀기만 하고 플래그를 적지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Message/NetMessageTable.h"
#include "Core/Network/NetTypes.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/NetObjectID.h"
#include "Engine/Net/NetMessageBody.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"
#include "GameFramework/Runtime/Character/Camera/SequenceTriggerComponent.h"

namespace sw
{
    class GameObjectManager;
    class INetPeers;

    /** @brief 재생 시작(서버 → 클라이언트) 본문입니다. 서버가 튼 재생 하나와 서버의 지금 프레임입니다. */
    REFLECT()
    struct SequenceNetStartBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _playID{ 0 }; ///< 서버가 매긴 재생 번호. 서버가 씬마다 1 부터 하나씩 올린다(앞 씬의 것은 라우터가 월드 세대로 가른다)
        PROPERTY()
        NetObjectID _director{ 0 }; ///< 시퀀스 플레이어를 가진 오브젝트
        PROPERTY()
        NetObjectID _activator{ 0 }; ///< 틀게 한 활성자(폰)
        PROPERTY()
        uint32 _frame{ 0 }; ///< 서버의 지금 프레임 — 늦은 참가는 여기부터 튼다
        PROPERTY()
        uint32 _serverTick{ 0 }; ///< `_frame` 이던 서버 틱(`SceneNetLink::getServerTick`) — 받는 쪽이 그 뒤 흐른 만큼 따라잡는다
        PROPERTY( Min = 0, Max = 1 )
        uint8 _audience{ 0 }; ///< `SequenceTriggerAudience`
    };

    /** @brief 재생 끝(서버 → 클라이언트) 본문입니다. */
    REFLECT()
    struct SequenceNetEndBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _playID{ 0 };
    };

    /** @brief 건너뛰기 요청(클라이언트 → 서버) 본문입니다. 서버가 요청한 연결로 검증한다. */
    REFLECT()
    struct SequenceNetSkipRequestBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _playID{ 0 };
    };

    /** @brief 컷씬 재생 키트의 메시지 표입니다. 종류 · 마스크 · 와이어 버전은 표에서 나온다(`NetMessageTable`). 본문은 모두 고정 배치형이다. */
    struct NetSequenceMessage
    {
        using Start       = NetMessageBodyDef<0, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, SequenceNetStartBody>;
        using End         = NetMessageBodyDef<1, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, SequenceNetEndBody>; ///< 시작 뒤에 닿아야 한다(같은 순서 채널)
        using SkipRequest = NetMessageBodyDef<2, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered, SequenceNetSkipRequestBody>;
        using Table       = NetWorldMessageTable<NetKitMessageRange::kSequence, Start, End, SkipRequest>;

        static constexpr uint8 kStart       = Table::getKindOf<Start>();
        static constexpr uint8 kEnd         = Table::getKindOf<End>();
        static constexpr uint8 kSkipRequest = Table::getKindOf<SkipRequest>();
    };
} // namespace sw

namespace sw
{
    /** @brief 보낸 · 받은 횟수입니다. 테스트 · 확인 · 감시 값에 씁니다. */
    struct SequenceReplicationStats
    {
        uint32 _startSentCount{ 0 };     ///< 서버 — 보낸 시작(연결 하나에 하나로 센다)
        uint32 _endSentCount{ 0 };       ///< 서버 — 보낸 끝(연결 하나에 하나로 센다)
        uint32 _unreplicableCount{ 0 };  ///< 서버 — 번호가 없어 보내지 않은 재생
        uint32 _sendRejectedCount{ 0 };  ///< 서버 — 연결이 메시지를 거절한 횟수
        uint32 _skipAcceptedCount{ 0 };  ///< 서버 — 검증을 지나 서버 재생을 끝낸 건너뛰기 요청
        uint32 _skipRejectedCount{ 0 };  ///< 서버 — 버린 건너뛰기 요청(모르는 번호 · 다른 연결 · 전원 재생)
        uint32 _startAppliedCount{ 0 };  ///< 클라이언트 — 튼 재생
        uint32 _startRejectedCount{ 0 }; ///< 클라이언트 — 틀지 못한 시작(번호 · 플레이어 없음, 로컬 폰이 아닌 활성자)
        uint32 _endAppliedCount{ 0 };    ///< 클라이언트 — 끝 메시지로 끝낸 재생
        uint32 _skipRequestCount{ 0 };   ///< 클라이언트 — 서버에 보낸 건너뛰기 요청
    };
} // namespace sw

namespace sw
{
    /**
     * @class SequenceReplicationServer
     * @brief 서버 쪽 — 새로 튼 재생의 시작을 대상에게, 끝난 재생의 끝을, 들어온 연결에는 재생 중인 전원 재생을 보내고, 건너뛰기 요청을 검증합니다.
     * @code
     *     server.initialize( &peers, &manager );
     *     _subscription = router.registerHandler( &server ); // 연결 사건 · 건너뛰기 요청
     *     server.update();                                   // 트리거 · 디렉터 틱 뒤(마지막 틱 그룹) — 같은 프레임에 보낸다
     * @endcode
     */
    class SW_KIT_API SequenceReplicationServer : public NetTableHandler<SequenceReplicationServer, NetSequenceMessage::Table, NetSequenceMessage::SkipRequest>
    {
    public:
        SequenceReplicationServer();

        /** @brief @p pManager 의 씬 오브젝트를 번호표에 잡고, 지금 트리거들의 발동 수를 기준으로 시작합니다. 보낸 기록은 비웁니다. */
        void initialize( INetPeers* pHost, GameObjectManager* pManager );
        /** @brief 끝난 재생의 끝, 들어온 연결의 시작, 새 재생의 시작 순서로 보냅니다(게임 스레드). */
        void update();

        NetHandleResult receive( NetSequenceMessage::SkipRequest, const NetMessageContext& context, const SequenceNetSkipRequestBody& body );
        /** @brief 다음 `update` 에서 재생 중인 전원 재생마다 시작을 그 연결에 보냅니다. */
        void onPeerEnteredWorld( int32 connectionID ) override;
        void onPeerLeftWorld( int32 connectionID ) override;

        /** @brief 살아 있는(끝을 보내지 않은) 재생 수입니다. */
        uint32                          getActivePlayCount() const { return static_cast<uint32>( _listPlay.size() ); }
        const SequenceReplicationStats& getStats() const { return _stats; }

    private:
        /** @brief 서버가 보낸 재생 하나입니다. */
        struct Play
        {
            uint32                  _playID{ 0 };
            GameObjectHandle        _director{}; ///< 서버에서 이 재생을 도는 디렉터(플레이어만의 디렉터일 수 있다)
            GameObjectHandle        _activator{};
            NetObjectID             _directorNetID{ 0 }; ///< 클라이언트가 틀 씬 디렉터의 번호(트리거의 `getSequence`)
            NetObjectID             _activatorNetID{ 0 };
            int32                   _targetConnection{ constant::invalid_index::kInt32 }; ///< `Activator` 재생의 대상 연결(없으면 원격에 보낼 곳이 없다)
            SequenceTriggerAudience _audience{ SequenceTriggerAudience::Everyone };
        };
        /** @brief 트리거 하나의 마지막으로 본 발동 수입니다. */
        struct SeenTrigger
        {
            TComponentHandle<SequenceTriggerComponent> _trigger{};
            uint32                                     _activationCount{ 0 };
        };

        void sendEnds();
        void sendToJoining();
        void sendNewPlays();
        /** @brief 트리거의 새 재생 @p triggerPlay 를 보낼 재생으로 만듭니다. 디렉터 · 활성자에 번호가 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool makePlay( const SequenceTriggerComponent& trigger, const SequenceTriggerPlay& triggerPlay, Play& outPlay );
        /** @brief 새 재생 하나의 시작을 대상에게 보내고 살아 있는 재생에 더합니다. */
        void startPlay( const SequenceTriggerComponent& trigger, const SequenceTriggerPlay& triggerPlay );
        /** @brief 재생의 시작 메시지를 씁니다(프레임은 서버 디렉터의 지금 프레임). 디렉터가 재생 중이 아니면 false 입니다. */
        [[nodiscard]] bool writeStart( const Play& play );
        /** @brief 쓴 메시지를 재생의 대상에게 보냅니다. 보낸 연결 수를 돌려줍니다. */
        uint32 sendToAudience( const Play& play );
        Play*  findPlay( uint32 playID );

    private:
        vector<Play>             _listPlay;
        vector<SeenTrigger>      _listSeenTrigger;
        vector<int32>            _listJoiningConnection;
        SequenceReplicationStats _stats;
        NetMessageWriter         _writer;
        INetPeers*               _pHost;
        GameObjectManager*       _pManager;
        uint32                   _nextPlayID;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SequenceReplicationClient
     * @brief 클라이언트 쪽 — 시작을 받아 그 프레임부터 틀고, 끝에 끝으로 건너뛰며, 로컬에서 먼저 끝난 `Activator` 재생(건너뛰기)을 서버에 요청합니다.
     * @details 메시지는 게임 프레임 앞(`GameNetSession::update`, 게임 스레드 · 틱 밖)에서 받는다. `update` 는 세션 컴포넌트의 틱 뒤 작업이 부른다.
     */
    class SW_KIT_API SequenceReplicationClient : public NetTableHandler<SequenceReplicationClient, NetSequenceMessage::Table, NetSequenceMessage::Start, NetSequenceMessage::End>
    {
    public:
        SequenceReplicationClient();

        void initialize( INetPeers* pHost, GameObjectManager* pManager );
        /** @brief 로컬에서 먼저 끝난 `Activator` 재생마다 건너뛰기 요청을 한 번 보냅니다(게임 스레드, 틱 밖). */
        void update();

        /** @brief 서버의 새 월드에 들어왔습니다 — 앞 월드의 재생 번호를 잊습니다(서버는 씬마다 1 부터 매긴다). */
        void            onWorldOpened( uint32 generation ) override;
        NetHandleResult receive( NetSequenceMessage::Start, const NetMessageContext& context, const SequenceNetStartBody& body );
        NetHandleResult receive( NetSequenceMessage::End, const NetMessageContext& context, const SequenceNetEndBody& body );

        /** @brief 서버가 끝을 보내지 않은 재생 수입니다. */
        uint32                          getActivePlayCount() const { return static_cast<uint32>( _listPlay.size() ); }
        const SequenceReplicationStats& getStats() const { return _stats; }

    private:
        struct Play
        {
            uint32                  _playID{ 0 };
            GameObjectHandle        _director{};
            SequenceTriggerAudience _audience{ SequenceTriggerAudience::Everyone };
            uint8                   _bSkipRequested{ SW_FALSE }; ///< 로컬에서 먼저 끝나 서버에 요청을 보냈다
        };

    private:
        vector<Play>             _listPlay;
        SequenceReplicationStats _stats;
        NetMessageWriter         _writer;
        INetPeers*               _pHost;
        GameObjectManager*       _pManager;
    };
} // namespace sw
