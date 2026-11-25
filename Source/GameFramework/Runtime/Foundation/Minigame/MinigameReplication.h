/**
 * @file MinigameReplication.h
 * @brief 서버가 들어간 하위 레벨 미니게임을 클라이언트에 알립니다 — 목록 복제와 준비 확인(`MinigameFlow` 가 쥐고 돌린다).
 * @details 하위 레벨은 프로세스마다 더한다(`SceneSubLevelSubsystem`). 서버만 더하면 서버에는 서 있는 바닥 · 벽이 클라이언트에는 없어, 그 자리를 걷는 예측이 어긋나고 늦은 참가자는 영영 보지 못한다.
 *          언리얼 서버가 스트리밍 레벨을 클라이언트에 알리고 보임을 되받는(`ClientUpdateLevelStreamingStatus` · `ServerUpdateLevelVisibility`) 자리다.
 *          - **목록.** 서버는 들어간 하위 레벨 전부를 아래부터 순서대로 싣는다(바뀔 때마다 전부 — 몇 개뿐이다). 월드에 들어온 연결에는 비었어도 한 번 보낸다 — 클라이언트는 받기 전에는 자기 월드가 다 섰는지 모른다.
 *          - **이름만 싣는다.** 맵 경로는 선에 없다 — 받은 쪽이 자기 팩의 미니게임 정의를 이름으로 읽는다(서버가 임의의 파일을 열게 하지 못한다). 이름은 정해진 글자만 받는다.
 *          - **준비 확인.** 모두가 맵 문서를 다 읽을 때까지 서버도 짓지 않고 기다렸다가 함께 짓는다(`SubLevelBuildStart::OnRelease`). 읽지 못한 연결 · 떠난 연결은 기다리지 않는다.
 *            답하지 않는 연결 하나가 서버를 붙잡지 못하게 기다림에는 한도가 있다(엔진 설정 `_minigame._subLevelReadySeconds`) — 늦은 클라이언트는 자기 준비가 끝나면 짓는다.
 *          - 씬이 바뀌면 월드 세대가 바뀌어 목록은 비고 다시 시작한다(`NetWorldMessageTable`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Message/NetMessageTable.h"
#include "Core/Network/NetTypes.h"

#include "Engine/Scene/SceneSubLevelTypes.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class INetPeers;

    /** @brief 하위 레벨 미니게임 복제의 메시지 표입니다. */
    struct MinigameNetMessage
    {
        using State = NetMessageDef<0, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered>;                                                  ///< 서버가 들어간 하위 레벨 목록(전부)
        using Ready = NetLimitedMessageDef<NetMessageDef<1, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered>, NetRequestLimit::kCommand>; ///< 그 번호의 맵을 다 읽었다 · 읽지 못했다
        using Table = NetWorldMessageTable<NetMessageRange::kRuntime, State, Ready>;

        static constexpr uint32 kMaxEntry      = 16; ///< 목록의 상한 — 쌓는 깊이 한도보다 넉넉하다
        static constexpr int32  kMaxNameLength = 64; ///< 미니게임 이름의 상한(바이트)

        /** @brief 미니게임 이름으로 받을 수 있는 글이면 true 입니다(`[a-z0-9_]`, 1 ~ `kMaxNameLength`) — 이름은 파일 경로가 된다. */
        [[nodiscard]] static SW_GF_API bool isValidName( string_view name );
    };

    /** @brief 서버가 들어간 하위 레벨 하나입니다. */
    struct MinigameNetEntry
    {
        string            _name;                  ///< 미니게임 이름(하위 레벨 이름)
        SubLevelPlacement _placement{};           ///< 절대 자리
        uint32            _entryID{ 0 };          ///< 들어갈 때마다 새로 매긴다 — 같은 미니게임에 다시 들어간 것을 가른다
        uint8             _bReleased{ SW_FALSE }; ///< 짓는다
    };

    /**
     * @class MinigameReplicationServer
     * @brief 서버 쪽 — 하위 레벨 목록을 보내고, 월드에 있는 연결의 준비를 모읍니다.
     */
    class SW_GF_API MinigameReplicationServer : public NetTableHandler<MinigameReplicationServer, MinigameNetMessage::Table, MinigameNetMessage::Ready>
    {
    public:
        MinigameReplicationServer();

        void initialize( INetPeers* pPeers );
        /**
         * @brief 들어간 하위 레벨을 목록 끝에 더합니다. 짓기를 이미 시작했으면 @p bReleased 다.
         * @param deadlineNanoseconds 준비를 기다리는 끝 시각(`MonotonicClock`)입니다.
         * @return 이름이 받을 수 없는 글이거나, 이미 있거나, 목록이 찼으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool addEntry( string_view name, const SubLevelPlacement& placement, bool bReleased, int64 deadlineNanoseconds );
        /** @brief 나온 하위 레벨을 뺍니다. 없으면 할 일이 없습니다. */
        void removeEntry( string_view name );
        /** @brief 월드에 있는 연결이 모두 답했거나 기다림이 끝났으면 true 입니다. */
        [[nodiscard]] bool isReady( string_view name, int64 nowNanoseconds ) const;
        /** @brief 짓기를 시작했다고 알립니다. */
        void releaseEntry( string_view name );
        /** @brief 목록이 바뀌었으면 모두에게, 새로 들어온 연결에는 늘 보냅니다(게임 스레드). */
        void update();

        NetHandleResult receive( MinigameNetMessage::Ready, const NetMessageContext& context, BitReader& body );
        void            onPeerEnteredWorld( int32 connectionID ) override;
        void            onPeerLeftWorld( int32 connectionID ) override;

        const vector<MinigameNetEntry>& getEntries() const { return _listEntry; }

    private:
        /** @brief 항목 하나의 준비입니다(`_listEntry` 와 같은 차례). */
        struct Pending
        {
            vector<int32> _listConnection;           ///< 아직 답하지 않은 연결
            int64         _deadlineNanoseconds{ 0 }; ///< 이때까지만 기다린다
        };

        void writeState();

    private:
        vector<MinigameNetEntry> _listEntry;
        vector<Pending>          _listPending;
        vector<int32>            _listJoiningConnection;
        NetMessageWriter         _writer;
        INetPeers*               _pPeers;
        uint32                   _nextEntryID;
        uint8                    _bChanged; ///< 목록이 마지막으로 보낸 뒤 바뀌었다
    };

    /**
     * @class MinigameReplicationClient
     * @brief 클라이언트 쪽 — 받은 목록을 들고, 준비를 알립니다. 세우는 일은 `MinigameFlow` 가 합니다.
     */
    class SW_GF_API MinigameReplicationClient : public NetTableHandler<MinigameReplicationClient, MinigameNetMessage::Table, MinigameNetMessage::State>
    {
    public:
        MinigameReplicationClient();

        void initialize( INetPeers* pPeers );
        /** @brief 항목 @p entryID 의 맵을 다 읽었다(@p bLoaded) · 읽지 못했다고 서버에 알립니다. */
        void sendReady( uint32 entryID, bool bLoaded );

        NetHandleResult receive( MinigameNetMessage::State, const NetMessageContext& context, BitReader& body );
        /** @brief 새 월드입니다 — 앞 월드의 목록을 잊습니다. 서버가 처음부터 보낸다. */
        void onWorldOpened( uint32 generation ) override;

        /** @brief 이 월드의 목록을 받았으면 true 입니다. */
        bool                            isKnown() const { return _bKnown == SW_TRUE; }
        const vector<MinigameNetEntry>& getEntries() const { return _listEntry; }

    private:
        vector<MinigameNetEntry> _listEntry;
        NetMessageWriter         _writer;
        INetPeers*               _pPeers;
        uint8                    _bKnown;
    };
} // namespace sw
