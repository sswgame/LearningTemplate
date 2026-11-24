/**
 * @file HousingReplication.h
 * @brief 하우징 부지의 서버 권위 배치입니다 — 클라이언트는 편집 요청만 보내고, 서버가 판정해 적용한 부지 상태를 모든 클라이언트에 복제합니다.
 * @details 키트 `GF_Housing` 의 메시지 영역(`NetKitMessageRange::kHousing`)을 쓴다. 언리얼 Fortnite 건축 · 발하임 · FFXIV 하우징처럼 배치는 서버가 정하고
 *          클라이언트는 결과를 받는다(기억에 의존한 비교) — 클라이언트 예측은 없다(건축은 틱 단위 반응이 필요 없고, 되돌림이 생기면 비용 · 받침이 함께 흔들린다).
 *          - **신뢰 경계**: 클라이언트가 보내는 것은 부지 id 해시 · 부품 id 해시 · 날 변환(조준 · 지면 투영) · 배치 id 뿐이다. 서버는 해시로 자기 부지 · 카탈로그를 찾고,
 *            스냅 · 겹침 · 받침 · 층 · 부지 범위 · 값(서버 지갑)을 자기 레이아웃으로 다시 판정한다. 스냅한 결과 · 비용 · 잔액은 받지 않는다. 날 값이 유한하지 않거나
 *            범위(`HousingTransformNetState` 의 칸 선언) 밖이면 깨진 메시지다(레이아웃의 칸 계산이 정수로 넘치지 않게). 글(부품 이름)은 받지 않는다 — 받은 글을 인턴하면 이름 표가 무한히 큰다.
 *          - **주인**: 주인은 연결이 아니라 주체다(`HousingOwnerID` — 접속 인증기가 연결에 지정한 계정 `INetPeers::getConnectionPrincipal`). 다시 붙어도 같은 주인이고
 *            부지 상태와 함께 세이브에 실린다(부지 컴포넌트). 계정 없이 붙은 연결은 세션 주체(연결 번호)라 그 연결이 닫히면 주인 없음으로 돌아가고 세이브에 싣지 않는다.
 *            네트워크 없는 게임은 로컬 주체다. 주인 없는 부지는 차지 요청(`Claim`)으로 얻고(주체 하나에 부지 하나 — FFXIV 개인 집), 주인이 아닌 주체의 편집은 `NotOwner` 다.
 *            주인이 바뀌면 되돌리기 이력을 비운다(다음 주인이 앞 주인의 편집을 되돌리지 못한다). 주체 값은 클라이언트에 보내지 않는다 — 받는 연결에서 본 내 것 · 남의 것 · 없음만 간다.
 *          - **거리**: 부지의 `_maxReach` 가 0 보다 크면 서버는 요청자 자리를 클라이언트가 아니라 게임의 서버 쪽 위치 조회(`HousingServerConfig::_locateCaller` —
 *            서버의 아바타 자리)로 얻어 편집이 건드릴 모든 자리(놓기 · 치우기 · 옮기기의 양쪽, 되돌리기 · 다시 하기는 이력 칸의 자리)와 XZ 거리를 본다.
 *            조회가 없거나 실패하면 거절한다(믿을 자리가 없다).
 *          - **요청률**: 연결마다 서버 틱당 `_maxRequestsPerTick` 개(편집 · 상태 요청)까지 줄에 넣고 넘치면 판정하지 않고 `RateLimited` 로 답한다.
 *          - **복제**(언리얼 복제의 델타 · 조각 전송과 같은 생각): 연결마다 처음(늦은 참가 · 다시 맞추기 요청)에는 부지 상태 바이트(`HousingLayout::writeState`)를
 *            `kSnapshotChunkBytes` 조각으로 나눠 신뢰 창이 받는 만큼만 보내고 남은 조각은 다음 틱에 잇는다(`trySendMessage` — 창이 차도 연결을 닫지 않는다).
 *            받는 쪽은 다 모은 뒤 세이브 읽기와 같은 검사를 거쳐 적용한다. 그 뒤로는 서버가 마지막으로 보낸 배치와 지금 배치를 견준 델타(사라진 id · 새로 · 바뀐 배치)와
 *            잔액 · 다음 id · 주인만 보낸다. 델타가 `kMaxDeltaBytes` 를 넘거나 보내던 조각 전송이 있으면 그 연결은 새 상태로 조각 전송을 다시 시작한다.
 *            클라이언트가 델타를 적용하지 못하면(모르는 부품 · 규칙 위반) 그 부지의 상태를 다시 달라고 요청하고, 상태가 올 때까지 그 부지의 델타는 버린다.
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

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Net/NetMessageBody.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"
#include "GameFramework/Kits/Feature/World/Housing/Rule/HousingLayout.h"

namespace sw
{
    class INetPeers;
    class Wallet;

    /** @brief 받는 연결에서 본 부지의 주인입니다. 값은 와이어 형식(상태 · 델타의 2 비트, 상태 조각 본문의 열거자 범위)이다. */
    ENUM( Count = Count )
    enum class HousingPlotOwner : uint8
    {
        None = 0,
        Self,
        Other,
        Count
    };

    /** @brief 하우징 선 형식의 상한입니다. 본문 형식(`PROPERTY( MaxCount )`)과 메시지 표가 함께 쓴다. */
    struct HousingNetLimit
    {
        static constexpr int32 kSnapshotChunkBytes = 16 * 1024; ///< `NetHousingMessage::kSnapshotChunkBytes`
    };

    /** @brief 편집 판정(서버 → 요청한 클라이언트) 본문입니다. 필드 순서가 와이어 형식이다. */
    REFLECT()
    struct HousingReplyBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _requestID{ 0 };
        PROPERTY()
        uint64 _plotHash{ 0 };
        PROPERTY()
        HousingPlaceResult _result{ HousingPlaceResult::Ok };
        PROPERTY()
        uint32 _placementID{ 0 }; ///< 새 배치 id. 놓기가 아니면 0
    };

    /** @brief 부지 상태 조각(서버 → 클라이언트) 본문입니다. 시작 자리 0 은 새 전송이다(받던 조각은 버린다). */
    REFLECT()
    struct HousingPlotSnapshotBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _plotHash{ 0 };
        PROPERTY()
        HousingPlotOwner _owner{ HousingPlotOwner::None };
        PROPERTY()
        int64 _funds{ 0 };
        PROPERTY( Min = 1 )
        int32 _totalSize{ 1 }; ///< 부지 상태 전체 바이트(상한 `NetHousingMessage::kMaxSnapshotBytes` 는 받는 쪽이 본다)
        PROPERTY( Min = 0 )
        int32 _offset{ 0 };
        PROPERTY( MaxCount = HousingNetLimit::kSnapshotChunkBytes, Min = 0, Max = 255 )
        vector<uint8> _chunk{};
    };

    /** @brief 부지 상태를 처음부터 다시 달라는 요청(클라이언트 → 서버) 본문입니다. 델타를 적용하지 못했을 때 보낸다. */
    REFLECT()
    struct HousingSnapshotRequestBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _plotHash{ 0 };
    };

    /**
     * @brief 손 본문(편집 요청 · 부지 델타)에 싣는 날 변환입니다. 칸 선언(`Replicated` · 범위)이 곧 바이트이고 범위가 신뢰 경계의 검사다(`ReplicatedLayout`).
     * @details 예전에는 쓰기 · 읽기 짝을 손으로 맞추고 범위를 상수 둘로 따로 봤다. 범위 밖 · NaN · 무한대는 읽기가 거절한다(깨진 메시지).
     *          레이아웃 규칙의 `HousingTransform` 과 칸이 같다 — 와이어 칸은 여기서만 정한다. 칸을 바꾸면 `Edit` · `PlotDelta` 의 본문 판을 올린다(손 본문이라 판이 스키마 해시를 따라가지 않는다).
     */
    REFLECT()
    struct HousingTransformNetState
    {
        REFLECT_BODY();
        PROPERTY( Replicated, Min = -100000, Max = 100000 )
        float2 _position{ 0.0f, 0.0f }; ///< 부지 원점 기준 XZ(m)
        PROPERTY( Replicated )
        int32 _level{ 0 };
        PROPERTY( Replicated, Min = -1000, Max = 1000 )
        float32 _yaw{ 0.0f }; ///< 라디안. 레이아웃이 [0, 2π) 를 다시 본다
    };

    /** @brief 하우징 키트의 메시지 표입니다. 종류 · 마스크 · 와이어 버전은 표에서 나온다(`NetMessageTable`). */
    struct NetHousingMessage
    {
        /** @brief 클라이언트 → 서버. 편집 요청 — 요청 번호(varuint) · 부지 id 해시(varuint) · 종류(3 비트) · 종류마다의 날 값. */
        using Edit = NetMessageDef<0, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered, 2>; ///< 판 2: 변환을 칸 선언(`HousingTransformNetState`)으로 쓴다
        /** @brief 서버 → 요청한 클라이언트. 판정(`HousingReplyBody`). */
        using Reply = NetMessageBodyDef<1, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, HousingReplyBody>;
        /** @brief 서버 → 클라이언트. 부지 상태 조각(`HousingPlotSnapshotBody`) — 부지 전체를 메시지 하나(64 KB 상한)가 아니라 조각으로 나눠 보낸다. */
        using PlotSnapshot = NetMessageBodyDef<2, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, HousingPlotSnapshotBody>;
        /**
         * @brief 서버 → 클라이언트. 부지 델타 — 부지 id 해시 · 주인(2 비트) · 잔액(varint) · 다음 배치 id(varuint) · 사라진 id 수와 id 들(varuint) ·
         *        새로 · 바뀐 배치 수와 배치 들(id · 부품 id 해시 · 부품 판 · x · z · 층 · 요).
         */
        using PlotDelta = NetMessageDef<3, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, 2>; ///< 판 2: 변환을 칸 선언(`HousingTransformNetState`)으로 쓴다
        /** @brief 클라이언트 → 서버. 부지 상태를 처음부터 다시 달라는 요청(`HousingSnapshotRequestBody`). */
        using SnapshotRequest = NetMessageBodyDef<4, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered, HousingSnapshotRequestBody>;
        using Table           = NetWorldMessageTable<NetKitMessageRange::kHousing, Edit, Reply, PlotSnapshot, PlotDelta, SnapshotRequest>;

        static constexpr uint8 kEdit            = Table::getKindOf<Edit>();
        static constexpr uint8 kReply           = Table::getKindOf<Reply>();
        static constexpr uint8 kPlotSnapshot    = Table::getKindOf<PlotSnapshot>();
        static constexpr uint8 kPlotDelta       = Table::getKindOf<PlotDelta>();
        static constexpr uint8 kSnapshotRequest = Table::getKindOf<SnapshotRequest>();

        static constexpr int32 kSnapshotChunkBytes = HousingNetLimit::kSnapshotChunkBytes; ///< 상태 조각 하나의 바이트(신뢰 메시지 64 KB 안 — 창을 조금씩 채운다)
        static constexpr int32 kMaxDeltaBytes      = 16 * 1024;                            ///< 델타 메시지가 이보다 크면 조각 전송으로 보낸다
        static constexpr int32 kMaxSnapshotBytes   = 4 * 1024 * 1024;                      ///< 부지 상태 하나의 상한(64 x 64 칸 · 8 층을 꽉 채워도 들어간다). 넘는 전체 크기는 깨진 메시지다
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

    /**
     * @struct HousingOwnerID
     * @brief 부지 주인(주체)의 값입니다. 0 은 주인 없음, 계정 주체는 접속 인증기가 준 값 그대로, 계정 없는 연결은 세션 깃발 + 연결 번호, 네트워크 없는 게임은 로컬 주체다.
     * @details 세션 주체는 그 세션에만 뜻이 있어 세이브에 싣지 않는다(`isPersistent`). 깃발 비트를 쓰는 계정 값은 세션 주체와 갈리지 않으므로 세션 주체로 다룬다(실리지 않을 뿐 판정은 맞다).
     */
    struct HousingOwnerID
    {
        static constexpr uint64 kNone        = 0;
        static constexpr uint64 kSessionFlag = uint64{ 1 } << 63;
        static constexpr uint64 kLocal       = kSessionFlag - 1; ///< 네트워크 없는 게임의 로컬 플레이어

        /** @brief 연결의 주체입니다. 계정(@p principal 이 0 이 아니고 깃발이 없다)이면 그 값, 아니면 세션 주체입니다. */
        static uint64 makeForConnection( uint64 principal, int32 connectionID )
        {
            const bool bAccount = principal != kNone && ( principal & kSessionFlag ) == 0 && principal != kLocal;
            return bAccount ? principal : ( kSessionFlag | static_cast<uint64>( static_cast<uint32>( connectionID ) ) );
        }
        static bool isSessionScoped( uint64 owner ) { return ( owner & kSessionFlag ) != 0; }
        /** @brief 세이브에 실을 값이면 true 입니다(계정 · 로컬 주체). */
        static bool isPersistent( uint64 owner ) { return owner != kNone && isSessionScoped( owner ) == false; }
    };

    /** @brief 편집 요청 하나입니다. 놓기는 `_candidate`, 치우기는 `_placementID`, 옮기기는 둘 다(부품 id 는 쓰지 않는다)를 씁니다. */
    struct HousingEditRequest
    {
        HousingEditOp    _op{ HousingEditOp::Claim };
        HousingCandidate _candidate{};
        uint32           _placementID{ 0 };
    };

    /** @brief `update` 한 번 동안 빌려 주는 부지 하나입니다. 레이아웃은 지갑이 묶여 있고(`bindWallet`) 셋 다 `update` 가 끝날 때까지 산다. */
    struct HousingNetPlotRef
    {
        HousingLayout*   _pLayout{ nullptr };
        Wallet*          _pWallet{ nullptr };
        uint64*          _pOwner{ nullptr }; ///< 서버 — 부지의 주인 주체(부지가 세이브에 싣는다). 클라이언트는 쓰지 않는다
        AbsolutePosition _origin{};          ///< 부지 원점 — 서버의 거리 검증
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
        uint64 _editCount{ 0 };            ///< 서버가 줄에 넣은 편집 요청
        uint64 _appliedCount{ 0 };         ///< 서버가 판정해 `Ok` 로 적용한 요청
        uint64 _rejectedCount{ 0 };        ///< 서버가 `Ok` 가 아닌 결과로 답한 요청(요청률 거절 포함)
        uint64 _snapshotChunkCount{ 0 };   ///< 서버가 보낸 상태 조각
        uint64 _deltaSentCount{ 0 };       ///< 서버가 보낸 델타
        uint64 _snapshotAppliedCount{ 0 }; ///< 클라이언트가 다 모아 적용한 상태
        uint64 _deltaAppliedCount{ 0 };    ///< 클라이언트가 적용한 델타
        uint64 _snapshotRequestCount{ 0 }; ///< 클라이언트가 보낸 다시 맞추기 요청
        uint32 _sendFailedCount{ 0 };      ///< 보내지 못한 메시지(신뢰 창이 차 연결이 닫혔거나 상태가 상한을 넘었다)
    };

    /** @brief 서버가 아는 요청자의 자리입니다(서버의 아바타 — 절대). 모르면 false 입니다. */
    using HousingCallerLocator = Delegate<bool( int32 connectionID, AbsolutePosition& outPosition )>;

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
     * @brief 서버 쪽 — 편집 · 상태 요청을 줄에 넣고 `update` 에서 판정 · 적용 · 답하고, 바뀐 부지는 델타로, 처음 받는 연결은 조각 전송으로 보냅니다.
     * @code
     *     (void)server.initialize( &host, config );
     *     _subscription = router.registerHandler( &server );
     *     server.update( listPlot ); // 씬 틱 뒤(게임 스레드)
     * @endcode
     */
    class SW_KIT_API HousingReplicationServer
        : public NetTableHandler<HousingReplicationServer, NetHousingMessage::Table, NetHousingMessage::Edit, NetHousingMessage::SnapshotRequest>
    {
    public:
        HousingReplicationServer();

        /** @brief 설정을 받습니다. 요청 상한이 0 이하면 오류를 남기고 false 입니다(받지 않는다). */
        [[nodiscard]] bool initialize( INetPeers* pHost, const HousingServerConfig& config );
        void               setCallerLocator( const HousingCallerLocator& locator ) { _config._locateCaller = locator; }
        /** @brief 줄의 요청을 판정해 적용하고 답한 뒤, 바뀐 부지의 델타 · 조각 전송을 보냅니다. */
        void update( const vector<HousingNetPlotRef>& listPlot );

        NetHandleResult receive( NetHousingMessage::Edit, const NetMessageContext& context, BitReader& body );
        NetHandleResult receive( NetHousingMessage::SnapshotRequest, const NetMessageContext& context, const HousingSnapshotRequestBody& body );
        void            onPeerEnteredWorld( int32 connectionID ) override;
        void            onPeerLeftWorld( int32 connectionID ) override;

        /** @brief 아직 다 보내지 못한 조각 전송 수입니다(신뢰 창이 차 다음 틱으로 넘긴 것 포함). */
        size_t                         getTransferCount() const { return _listTransfer.size(); }
        const HousingReplicationStats& getStats() const { return _stats; }

    private:
        struct PlotEntry
        {
            vector<HousingPlacement> _listSentPlacement{}; ///< 마지막으로 보낸 배치(id 오름차순) — 델타의 기준
            uint64                   _plotHash{ 0 };
            uint64                   _sentOwner{ HousingOwnerID::kNone };
            int64                    _sentFunds{ 0 };
            uint32                   _sentRevision{ 0 };
            uint8                    _bSent             : 1; ///< 상태를 한 번이라도 보냈다
            uint8                    _bReachErrorLogged : 1; ///< 위치 조회가 없다는 오류를 남겼다(요청마다 남기지 않는다)
            [[maybe_unused]] uint8   _reserved          : 6;

            PlotEntry()
                : _bSent{ SW_FALSE }
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

        /** @brief 연결 하나에 보내는 부지 상태 하나입니다. 창이 차면 다음 틱에 `_offset` 부터 잇는다. */
        struct Transfer
        {
            vector<uint8>    _bytes{};
            uint64           _plotHash{ 0 };
            int64            _funds{ 0 };
            int32            _connectionID{ constant::invalid_index::kInt32 };
            int32            _offset{ 0 };
            HousingPlotOwner _owner{ HousingPlotOwner::None };
        };

        struct ConnectionLoad
        {
            int32 _connectionID{ constant::invalid_index::kInt32 };
            int32 _queuedCount{ 0 };
        };

        struct SnapshotRequestEntry
        {
            uint64 _plotHash{ 0 };
            int32  _connectionID{ constant::invalid_index::kInt32 };
        };

        PlotEntry& getOrCreateEntry( uint64 plotHash );
        /** @brief 이번 틱에 이 연결이 넣을 수 있는 요청이면 셈을 올리고 true 입니다. */
        [[nodiscard]] bool acceptRequest( int32 connectionID );
        uint64             findCallerOwner( int32 connectionID ) const;
        HousingPlaceResult evaluateEdit( const PendingEdit& edit, const vector<HousingNetPlotRef>& listPlot, uint32& outPlacementID );
        /** @brief 부지의 `_maxReach` 를 봅니다. 제한이 없거나 모든 자리가 안이면 true 입니다. @p listLocal 은 부지 기준 3D 자리들입니다. */
        bool isWithinReach( PlotEntry& entry, const HousingNetPlotRef& plot, int32 connectionID, const vector<float3>& listLocal );
        /** @brief 세션 주체의 주인이 닫힌 연결이면 주인 없음으로 돌리고 이력을 비웁니다. */
        void releaseClosedOwners( const vector<HousingNetPlotRef>& listPlot );
        void reply( int32 connectionID, uint32 requestID, uint64 plotHash, HousingPlaceResult result, uint32 placementID );
        void sendPlotChanges( PlotEntry& entry, const HousingNetPlotRef& plot, const vector<int32>& listFresh );
        /** @brief @p connectionID 에 부지 상태 조각 전송을 (다시) 시작합니다. 보내던 같은 부지의 전송은 새 상태로 바꾼다. */
        void startTransfer( const HousingNetPlotRef& plot, uint64 plotHash, int32 connectionID, const vector<uint8>& bytes );
        /** @brief 조각 전송들을 신뢰 창이 받는 만큼 보냅니다. */
        void sendTransfers();
        bool hasTransfer( uint64 plotHash, int32 connectionID ) const;

    private:
        vector<PlotEntry>            _listPlot;
        vector<PendingEdit>          _listPending;
        vector<SnapshotRequestEntry> _listSnapshotRequest;
        vector<Transfer>             _listTransfer;
        vector<ConnectionLoad>       _listLoad;   ///< 이번 틱에 줄에 넣은 요청 수(연결마다) — `update` 가 비운다
        vector<int32>                _listJoined; ///< 다음 `update` 에서 모든 부지의 상태를 받을 연결
        vector<int32>                _listClosed; ///< 다음 `update` 에서 세션 주인을 풀 연결
        vector<int32>                _listConnected;
        vector<float3>               _listReachTarget;
        HousingServerConfig          _config;
        HousingReplicationStats      _stats;
        NetMessageWriter             _writer;
        INetPeers*                   _pHost;
    };
} // namespace sw

namespace sw
{
    /**
     * @class HousingReplicationClient
     * @brief 클라이언트 쪽 — 편집 요청을 보내고, 받은 상태 조각을 모으고 델타와 함께 받은 순서대로 `update` 에서 부지 레이아웃 · 지갑에 적용하고, 판정을 돌려줍니다.
     * @code
     *     client.initialize( &host );
     *     _subscription = router.registerHandler( &client );
     *     (void)client.sendEdit( plotID, request );
     *     client.update( listPlot, listReply ); // 받은 메시지를 나눈 뒤(게임 스레드)
     * @endcode
     */
    class SW_KIT_API HousingReplicationClient
        : public NetTableHandler<HousingReplicationClient, NetHousingMessage::Table, NetHousingMessage::Reply, NetHousingMessage::PlotSnapshot, NetHousingMessage::PlotDelta>
    {
    public:
        HousingReplicationClient();

        void initialize( INetPeers* pHost );
        /** @brief 요청을 서버에 보냅니다. 보낸 요청 번호(1 부터)이고 보내지 못했으면 0 입니다. */
        [[nodiscard]] uint32 sendEdit( const hashed_string& plotID, const HousingEditRequest& request );
        /** @brief 서버의 월드에 아직 한 번도 들어가지 않았으면 true 입니다 — 그동안 요청은 보내지 말고 들고 있는다(들어간 뒤 끊긴 것과 다르다, 그때는 답이 오지 않는다). */
        bool isAwaitingServer();
        /** @brief 서버의 새 월드에 들어왔습니다 — 앞 월드에서 모으던 조각 · 적용을 기다리던 것을 버립니다(서버가 부지 상태를 처음부터 보낸다). */
        void onWorldOpened( uint32 generation ) override;
        /** @brief 들어갔던 서버의 월드와 떨어져 있으면(끊김 · 다른 씬) true 입니다 — 보낸 요청의 답이 오지 않는다. */
        bool isServerLost() const { return _bServerReached == SW_TRUE && ( _pHost == nullptr || _pHost->isInWorld( 0 ) == false ); }
        /** @brief 받은 상태 · 델타를 받은 순서대로 적용하고 판정을 @p outListReply 에 더합니다. 목록에 아직 없는 부지의 것은 들고 있다가 그 부지가 생기면 적용한다. */
        void update( const vector<HousingNetPlotRef>& listPlot, vector<HousingEditReply>& outListReply );
        /** @brief 마지막으로 받은 부지의 주인입니다(받은 적이 없으면 `None`). */
        HousingPlotOwner findOwner( const hashed_string& plotID ) const;

        NetHandleResult receive( NetHousingMessage::Reply, const NetMessageContext& context, const HousingReplyBody& body );
        NetHandleResult receive( NetHousingMessage::PlotSnapshot, const NetMessageContext& context, const HousingPlotSnapshotBody& body );
        NetHandleResult receive( NetHousingMessage::PlotDelta, const NetMessageContext& context, BitReader& body );

        const HousingReplicationStats& getStats() const { return _stats; }

    private:
        /** @brief 델타의 배치 하나입니다(부품은 해시 — 적용할 때 부지의 카탈로그로 찾는다). */
        struct DeltaPlacement
        {
            HousingTransform _transform{};
            uint64           _partHash{ 0 };
            uint32           _placementID{ 0 };
            int32            _partVersion{ 0 };
        };

        enum class OpKind : uint8
        {
            Snapshot = 0,
            Delta
        };

        /** @brief 받은 순서대로 적용할 일 하나 — 다 모은 상태 또는 델타입니다. */
        struct PendingOp
        {
            vector<uint8>          _snapshotBytes{};
            vector<uint32>         _listRemovedID{};
            vector<DeltaPlacement> _listUpsert{};
            uint64                 _plotHash{ 0 };
            int64                  _funds{ 0 };
            uint32                 _nextPlacementID{ 0 };
            HousingPlotOwner       _owner{ HousingPlotOwner::None };
            OpKind                 _kind{ OpKind::Delta };
        };

        struct Assembly
        {
            vector<uint8> _bytes{};
            uint64        _plotHash{ 0 };
            int32         _totalSize{ 0 };
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

        /** @brief 델타 하나를 부지에 적용합니다. 모르는 부품이거나 규칙을 어기면 false 입니다(부지는 그대로). */
        [[nodiscard]] static bool applyDelta( const HousingNetPlotRef& plot, const PendingOp& op );
        void                      requestSnapshot( uint64 plotHash );
        void                      setOwner( uint64 plotHash, HousingPlotOwner owner );

    private:
        vector<PendingOp>       _listOp;
        vector<Assembly>        _listAssembly;
        vector<uint64>          _listAwaitingSnapshot; ///< 다시 맞추기를 요청한 부지 — 상태가 올 때까지 그 부지의 델타는 버린다
        vector<ReceivedReply>   _listReply;
        vector<OwnerEntry>      _listOwner;
        HousingReplicationStats _stats;
        NetMessageWriter        _writer;
        INetPeers*              _pHost;
        uint32                  _nextRequestID;
        uint8                   _bServerReached : 1; ///< 서버와 한 번이라도 붙었다(`isAwaitingServer`)
    };
} // namespace sw
