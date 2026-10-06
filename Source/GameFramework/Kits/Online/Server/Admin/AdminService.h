/**
 * @file AdminService.h
 * @brief GM 서비스(서버) — GM 명령을 저장소 일로 맡기고 완료에서 답합니다. 정지 · 영구 정지가 걸리면 세션 끊기 창구로 그 계정을 내보냅니다. 서비스 스레드 하나에서 씁니다.
 * @details - 경제 서비스와 같은 모양: 입구 둘(`onServiceRequest` — 로그인한 GM 연결, `submitCall` — 같은 프로세스 · 시험), 일 하나 = 명령 하나.
 *          - 조회의 표시 이름 · 접속 여부는 이 프로세스의 계정 창구(`IAccountDirectory`)로 채운다 — 접속하지 않은 계정을 이름으로 찾는 일은 백로그.
 *          - 세션 끊기는 새로 적용된 정지에만(재시도의 지난 결과에서는 다시 끊지 않는다).
 *          - 운영 포트를 따로(GM 전용 호스트 — 사설망 · TLS) 두는 것을 권한다. 첫 관리자는 서버 조립이 기동 때 `seedRole` 로.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Observability/ServiceMetrics.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Admin/AdminProtocol.h"

namespace sw
{
    class IAccountDirectory;
    class IAccountSessionControl;
    class ILedgerPolicy;
    class IServiceStore;
    class MetricRegistry;

    /** @brief GM 서비스 설정 — 포인터는 빌려 쓴다. */
    struct AdminServiceSettings
    {
        const IAccountDirectory* _pDirectory{ nullptr };      ///< 이름으로 찾기 · 표시 이름 · 접속 여부(없으면 id 로만)
        IAccountSessionControl*  _pSessionControl{ nullptr }; ///< 정지 · 영구 정지 뒤 세션 끊기(계정 키트가 구현)
        const ILedgerPolicy*     _pPolicy{ nullptr };         ///< 지급 상한(게임이 CurrencyCatalog 를)
        MetricRegistry*          _pMetricRegistry{ nullptr };
    };
} // namespace sw

namespace sw
{
    /** @class AdminService @brief GM 명령 처리기입니다. */
    class SW_GF_API AdminService final : public IOnlineService
    {
    public:
        using ReplyDelegate = Delegate<void( const AdminReply& )>;

        AdminService();
        ~AdminService() override;

        AdminService( const AdminService& )            = delete;
        AdminService& operator=( const AdminService& ) = delete;

        /** @brief 저장소가 없으면 false 입니다. */
        [[nodiscard]] bool initialize( IServiceStore* pStore, const AdminServiceSettings& settings );
        /** @brief 맡긴 일이 끝날 때까지 저장소를 거둡니다(상한 5 초) — 저장소를 내리기 전에. */
        void shutdown();

        /** @brief 명령 하나 — @p adminId 는 로그인한 GM 계정(호스트 경로는 `OnlineCallContext::_accountId`). @p onReply 는 정확히 한 번. */
        void submitCall( AccountId adminId, uint16 method, const AdminRequest& request, const NetIdempotencyKey& idempotencyKey, int64 nowMs, ReplyDelegate onReply );
        /** @brief 서버 설정의 첫 관리자(레코드가 없을 때만)를 맡깁니다. 서버 조립이 기동 때 부른다. */
        void seedRole( AccountId accountId, AdminRole role, int64 nowMs );
        void tick( int64 nowMs );

        // IOnlineService
        uint16 getMethodRange() const override { return OnlineMethodRange::kAdmin; }
        uint32 getProtocolVersion() const override { return AdminProtocol::kVersion; }
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;

        /** @brief 일의 `complete` 가 부릅니다. */
        void  completeCall( uint64 callId, AdminReply& inoutReply );
        void  completeSeed( AdminResult result );
        int32 getPendingCallCount() const { return static_cast<int32>( _listPendingCall.size() ); }

    private:
        struct PendingCall
        {
            ReplyDelegate      _onReply{};
            NetRequestToken    _token{};
            OnlineServiceHost* _pHost{ nullptr };
            uint64             _callId{ 0 };
            int64              _receivedNanoseconds{ 0 };
            int64              _nowMs{ 0 };
            AccountId          _targetId{ kInvalidAccountId };
            int32              _methodIndex{ 0 };
            uint16             _method{ 0 };
        };

        void startCall( AccountId adminId, uint16 method, const AdminRequest& request, const NetIdempotencyKey& idempotencyKey, int64 nowMs, PendingCall pending );
        void finishCall( size_t pendingIndex, const AdminReply& reply );

        vector<PendingCall>  _listPendingCall;
        ServiceMetrics       _metrics;
        AdminServiceSettings _settings;
        IServiceStore*       _pStore;
        uint64               _nextCallId;
        int64                _nowMs;
        int32                _pendingSeedCount;
    };
} // namespace sw
