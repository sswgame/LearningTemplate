/**
 * @file HousingReplication.h
 * @brief 하우징 부지의 서버 권위 배치입니다 — 클라이언트는 편집 요청만 보내고, 서버가 판정해 적용한 부지 상태를 모든 클라이언트에 방송합니다.
 * @details 키트 `GF_Housing` 의 메시지 영역(`NetKitMessageRange::kHousing`)을 쓴다. 언리얼 Fortnite 건축 · 발하임 · FFXIV 하우징처럼 배치는 서버가 정하고
 *          클라이언트는 결과를 받는다(기억에 의존한 비교) — 클라이언트 예측은 없다(건축은 틱 단위 반응이 필요 없고, 되돌림이 생기면 비용 · 받침이 함께 흔들린다).
 *          - **신뢰 경계**: 클라이언트가 보내는 것은 부지 id 해시 · 부품 id 해시 · 날 변환(조준 · 지면 투영) · 배치 id 뿐이다. 서버는 해시로 자기 부지 · 카탈로그를 찾고,
 *            스냅 · 겹침 · 받침 · 층 · 부지 범위 · 값(서버 지갑)을 자기 레이아웃으로 다시 판정한다. 스냅한 결과 · 비용 · 잔액은 받지 않는다. 날 값이 유한하지 않거나
 *            범위(`kMaxRawPosition` · `kMaxRawYaw`) 밖이면 깨진 메시지다(레이아웃의 칸 계산이 정수로 넘치지 않게). 글(부품 이름)은 받지 않는다 — 받은 글을 인턴하면 이름 표가 무한히 큰다.
 *          - **주인**: 부지마다 서버가 주인 연결을 적는다. 주인 없는 부지는 차지 요청(`Claim`)으로 얻고(연결 하나에 부지 하나 — FFXIV 개인 집), 주인이 아닌 연결의 편집은
 *            `NotOwner` 다. 연결이 닫히면 그 연결의 부지는 주인 없음으로 돌아가고 되돌리기 이력을 비운다(다음 주인이 앞 주인의 편집을 되돌리지 못한다).
 *          - **거리**: 부지의 `_maxReach` 가 0 보다 크면 서버는 요청자 자리를 클라이언트가 아니라 게임의 서버 쪽 위치 조회(`HousingServerConfig::_locateCaller` —
 *            서버의 아바타 자리)로 얻어 편집할 자리와 XZ 거리를 본다. 조회가 없거나 실패하면 거절한다(믿을 자리가 없다).
 *          - **요청률**: 연결마다 서버 틱당 `_maxRequestsPerTick` 개까지 줄에 넣고 넘치면 판정하지 않고 `RateLimited` 로 답한다.
 *          - **상태 방송**: 부지의 판(레이아웃 리비전) · 잔액 · 주인이 바뀐 틱에 그 부지 전체 상태를 연결마다 보낸다(주인 칸만 받는 연결마다 다르다 — 내 것 · 남의 것 · 없음).
 *            몸은 레이아웃의 상태 바이트(`HousingLayout::writeState`)라 받는 쪽이 세이브 읽기와 같은 검사(id 순서 · 카탈로그 · 규칙)를 거친다. 늦게 들어온 연결은
 *            다음 틱에 모든 부지의 상태를 받는다. 신뢰 순서 메시지 하나(64 KB)에 들어가지 않는 부지는 오류를 남기고 보내지 않는다.
 *            shortcut: 부지 전체를 보낸다(배치 약 1800 개까지) — 그보다 큰 부지가 생기면 배치 단위 차분을 더한다.
 *          스레드: 게임 스레드에서만 쓴다. `receive` 는 줄에만 넣고 레이아웃은 `update` 가 바꾼다(라우터의 poll 과 씬 틱 사이에 부지가 사라져도 남은 포인터를 쓰지 않는다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Message/NetMessageTable.h"
#include "Core/Network/NetTypes.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"
#include "GameFramework/Kits/Feature/World/Housing/Rule/HousingLayout.h"

namespace sw
{
    class INetPeers;
    class Wallet;

    /** @brief 하우징 키트의 메시지 표입니다. 종류 · 마스크 · 와이어 버전은 표에서 나온다(`NetMessageTable`). */
    struct NetHousingMessage
    {
        /** @brief 클라이언트 → 서버. 편집 요청 — 요청 번호(varuint) · 부지 id 해시(varuint) · 종류(3 비트) · 종류마다의 날 값. */
        using Edit = NetMessageDef<0, NetMessageDirection::ToServer, NetChannelType::ReliableOrdered>;
        /** @brief 서버 → 요청한 클라이언트. 판정 — 요청 번호 · 부지 id 해시 · 결과(8 비트 `HousingPlaceResult`) · 새 배치 id(varuint, 놓기가 아니면 0). */
        using Reply = NetMessageDef<1, NetMessageDirection::ToClient, NetChannelType::ReliableOrdered>;
        /** @brief 서버 → 클라이언트. 부지 상태 — 부지 id 해시 · 주인(2 비트 `HousingPlotOwner`) · 잔액(varint) · 레이아웃 상태 바이트(blob). */
        using PlotState = NetMessageDef<2, NetMessageDirection::ToClient, NetChannelType::ReliableOrdered>;
        using Table     = NetMessageTable<NetKitMessageRange::kHousing, Edit, Reply, PlotState>;

        static constexpr uint8 kEdit      = Table::getKindOf<Edit>();
        static constexpr uint8 kReply     = Table::getKindOf<Reply>();
        static constexpr uint8 kPlotState = Table::getKindOf<PlotState>();

        static constexpr float32 kMaxRawPosition = 1.0e5f; ///< 날 자리 |x| · |z| 의 상한(m). 넘으면 깨진 메시지다
        static constexpr float32 kMaxRawYaw      = 1.0e3f; ///< 날 요 |yaw| 의 상한(라디안). 넘으면 깨진 메시지다
    };

    /** @brief 편집 요청의 종류입니다. 값은 와이어 형식(`Edit` 의 3 비트)이라 바꾸면 `Edit` 의 본문 판을 올린다. */
    enum class HousingEditOp : uint8
    {
        Claim = 0, ///< 주인 없는 부지를 차지한다
        Place,
        Remove,
        Move,
        Undo,
        Redo,
        Count
    };

    /** @brief 받는 연결에서 본 부지의 주인입니다. 값은 와이어 형식(`PlotState` 의 2 비트)이다. */
    enum class HousingPlotOwner : uint8
    {
        None = 0,
        Self,
        Other,
        Count
    };

    /** @brief 편집 요청 하나입니다. 놓기는 `_candidate`, 치우기는 `_placementID`, 옮기기는 둘 다(부품 id 는 쓰지 않는다)를 씁니다. */
    struct HousingEditRequest
    {
        HousingEditOp    _op{ HousingEditOp::Claim };
        HousingCandidate _candidate{};
        uint32           _placementID{ 0 };
    };

    /** @brief `update` 한 번 동안 빌려 주는 부지 하나입니다. 레이아웃은 지갑이 묶여 있고(`bindWallet`) 둘 다 `update` 가 끝날 때까지 산다. */
    struct HousingNetPlotRef
    {
        HousingLayout* _pLayout{ nullptr };
        Wallet*        _pWallet{ nullptr };
        float3         _origin{}; ///< 부지 원점(월드) — 서버의 거리 검증
    };

    /** @brief 클라이언트가 받은 판정 하나입니다. `_plotIndex` 는 그 `update` 에 넘긴 목록의 자리입니다. */
    struct HousingEditReply
    {
        size_t             _plotIndex{ 0 };
        uint32             _requestID{ 0 };
        uint32             _placementID{ 0 };
        HousingPlaceResult _result{ HousingPlaceResult::Ok };
    };

    /** @brief 보내고 받은 셈입니다. 테스트 · 진단이 읽습니다. */
    struct HousingReplicationStats
    {
        uint64 _editCount{ 0 };         ///< 서버가 줄에 넣은 편집 요청
        uint64 _appliedCount{ 0 };      ///< 서버가 판정해 `Ok` 로 적용한 요청
        uint64 _rejectedCount{ 0 };     ///< 서버가 `Ok` 가 아닌 결과로 답한 요청(요청률 거절 포함)
        uint64 _stateSentCount{ 0 };    ///< 서버가 보낸 부지 상태 메시지
        uint64 _stateAppliedCount{ 0 }; ///< 클라이언트가 적용한 부지 상태
        uint32 _sendFailedCount{ 0 };   ///< 보내지 못한 메시지(신뢰 창이 차 연결이 닫혔거나 상태가 64 KB 를 넘었다)
    };

    /** @brief 서버가 아는 요청자의 월드 자리입니다(서버의 아바타). 모르면 false 입니다. */
    using HousingCallerLocator = Delegate<bool( int32 connectionID, float3& outWorldPosition )>;

    /** @brief 서버 설정입니다. 기본값이 없다 — `_maxRequestsPerTick` 이 0 이하면 `initialize` 가 오류로 멈춘다. */
    struct HousingServerConfig
    {
        int32                _maxRequestsPerTick{ 0 }; ///< 연결 하나가 서버 틱 하나에 줄에 넣을 수 있는 요청 수
        HousingCallerLocator _locateCaller{};          ///< `_maxReach` 가 0 보다 큰 부지가 있으면 필요하다
    };
} // namespace sw

namespace sw
{
    /**
     * @class HousingReplicationServer
     * @brief 서버 쪽 — 편집 요청을 줄에 넣고 `update` 에서 판정 · 적용 · 답하고, 바뀐 부지와 늦게 들어온 연결에 부지 상태를 보냅니다.
     * @code
     *     (void)server.initialize( &host, config );
     *     _subscription = router.registerHandler( &server );
     *     server.update( listPlot ); // 씬 틱 뒤(게임 스레드)
     * @endcode
     */
    class SW_KIT_API HousingReplicationServer : public NetTableHandler<HousingReplicationServer, NetHousingMessage::Table, NetHousingMessage::Edit>
    {
    public:
        HousingReplicationServer();

        /** @brief 설정을 받습니다. 요청 상한이 0 이하면 오류를 남기고 false 입니다(받지 않는다). */
        [[nodiscard]] bool initialize( INetPeers* pHost, const HousingServerConfig& config );
        void               setCallerLocator( const HousingCallerLocator& locator ) { _config._locateCaller = locator; }
        /** @brief 줄의 요청을 판정해 적용하고 답한 뒤, 바뀐 부지 · 늦게 들어온 연결에 상태를 보냅니다. */
        void update( const vector<HousingNetPlotRef>& listPlot );

        NetHandleResult receive( NetHousingMessage::Edit, const NetMessageContext& context, BitReader& body );
        void            onConnectionOpened( int32 connectionID ) override;
        void            onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;

        /** @brief 부지의 주인 연결입니다. 주인이 없거나 모르는 부지면 `constant::invalid_index::kInt32` 입니다. */
        int32                          findOwner( const hashed_string& plotID ) const;
        const HousingReplicationStats& getStats() const { return _stats; }

    private:
        struct PlotEntry
        {
            uint64 _plotHash{ 0 };
            int32  _owner{ constant::invalid_index::kInt32 };
            int32  _sentOwner{ constant::invalid_index::kInt32 };
            int64  _sentFunds{ 0 };
            uint32 _sentRevision{ 0 };
            uint8  _bSent             : 1; ///< 상태를 한 번이라도 보냈다
            uint8  _bClearHistory     : 1; ///< 주인이 바뀌었다 — 다음 `update` 에서 되돌리기 이력을 비운다
            uint8  _bReachErrorLogged : 1; ///< 위치 조회가 없다는 오류를 남겼다(요청마다 남기지 않는다)
            uint8  _reserved          : 5;

            PlotEntry()
                : _bSent{ SW_FALSE }
                , _bClearHistory{ SW_FALSE }
                , _bReachErrorLogged{ SW_FALSE }
                , _reserved{ 0 }
            {
            }
        };

        struct PendingEdit
        {
            HousingTransform     _transform{};
            uint64               _plotHash{ 0 };
            uint64               _partHash{ 0 };
            int32                _connectionID{ constant::invalid_index::kInt32 };
            uint32               _requestID{ 0 };
            uint32               _placementID{ 0 };
            HousingEditOp        _op{ HousingEditOp::Claim };
            HousingPlacementMode _mode{ HousingPlacementMode::Grid };
        };

        struct ConnectionLoad
        {
            int32 _connectionID{ constant::invalid_index::kInt32 };
            int32 _queuedCount{ 0 };
        };

        PlotEntry&         findOrAddEntry( uint64 plotHash );
        PlotEntry*         findEntry( uint64 plotHash );
        const PlotEntry*   findEntry( uint64 plotHash ) const;
        HousingPlaceResult evaluateEdit( const PendingEdit& edit, const vector<HousingNetPlotRef>& listPlot, uint32& outPlacementID );
        /** @brief 부지의 `_maxReach` 를 봅니다. 제한이 없거나 안이면 true 입니다. @p localPosition 은 부지 기준 3D 자리입니다. */
        bool isWithinReach( PlotEntry& entry, const HousingNetPlotRef& plot, int32 connectionID, const float3& localPosition );
        void reply( int32 connectionID, uint32 requestID, uint64 plotHash, HousingPlaceResult result, uint32 placementID );
        /** @brief 부지 상태를 @p listConnection 에 보냅니다(받는 연결마다 주인 칸이 다르다). */
        void sendPlotState( PlotEntry& entry, const HousingNetPlotRef& plot, const vector<int32>& listConnection );

    private:
        vector<PlotEntry>       _listPlot;
        vector<PendingEdit>     _listPending;
        vector<ConnectionLoad>  _listLoad;   ///< 이번 틱에 줄에 넣은 요청 수(연결마다) — `update` 가 비운다
        vector<int32>           _listJoined; ///< 다음 `update` 에서 모든 부지의 상태를 받을 연결
        vector<int32>           _listConnected;
        HousingServerConfig     _config;
        HousingReplicationStats _stats;
        NetMessageWriter        _writer;
        INetPeers*              _pHost;
    };
} // namespace sw

namespace sw
{
    /**
     * @class HousingReplicationClient
     * @brief 클라이언트 쪽 — 편집 요청을 보내고, 받은 부지 상태를 `update` 에서 부지 레이아웃 · 지갑에 적용하고, 판정을 돌려줍니다.
     * @code
     *     client.initialize( &host );
     *     _subscription = router.registerHandler( &client );
     *     (void)client.sendEdit( plotID, request );
     *     client.update( listPlot, listReply ); // 받은 메시지를 나눈 뒤(게임 스레드)
     * @endcode
     */
    class SW_KIT_API HousingReplicationClient : public NetTableHandler<HousingReplicationClient, NetHousingMessage::Table, NetHousingMessage::Reply, NetHousingMessage::PlotState>
    {
    public:
        HousingReplicationClient();

        void initialize( INetPeers* pHost );
        /** @brief 요청을 서버에 보냅니다. 보낸 요청 번호(1 부터)이고 보내지 못했으면 0 입니다. */
        [[nodiscard]] uint32 sendEdit( const hashed_string& plotID, const HousingEditRequest& request );
        /** @brief 받은 부지 상태를 적용하고 판정을 @p outListReply 에 더합니다. 목록에 아직 없는 부지의 상태는 들고 있다가 그 부지가 생기면 적용한다. */
        void update( const vector<HousingNetPlotRef>& listPlot, vector<HousingEditReply>& outListReply );
        /** @brief 마지막으로 받은 부지의 주인입니다(받은 적이 없으면 `None`). */
        HousingPlotOwner findOwner( const hashed_string& plotID ) const;

        NetHandleResult receive( NetHousingMessage::Reply, const NetMessageContext& context, BitReader& body );
        NetHandleResult receive( NetHousingMessage::PlotState, const NetMessageContext& context, BitReader& body );

        const HousingReplicationStats& getStats() const { return _stats; }

    private:
        struct PendingState
        {
            vector<uint8>    _layoutBytes{};
            uint64           _plotHash{ 0 };
            int64            _funds{ 0 };
            HousingPlotOwner _owner{ HousingPlotOwner::None };
        };

        struct ReceivedReply
        {
            uint64             _plotHash{ 0 };
            uint32             _requestID{ 0 };
            uint32             _placementID{ 0 };
            HousingPlaceResult _result{ HousingPlaceResult::Ok };
        };

        struct OwnerEntry
        {
            uint64           _plotHash{ 0 };
            HousingPlotOwner _owner{ HousingPlotOwner::None };
        };

    private:
        vector<PendingState>    _listState; ///< 부지 하나에 가장 새 상태 하나(전체 상태라 앞 것은 버린다)
        vector<ReceivedReply>   _listReply;
        vector<OwnerEntry>      _listOwner;
        HousingReplicationStats _stats;
        NetMessageWriter        _writer;
        INetPeers*              _pHost;
        uint32                  _nextRequestID;
    };
} // namespace sw
