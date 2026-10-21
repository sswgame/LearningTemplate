/**
 * @file AdminService.h
 * @brief GM 서비스(서버)입니다. GM 명령을 저장소 작업으로 맡기고 완료에서 응답합니다. 정지나 영구 정지가 걸리면 세션 끊기 인터페이스로 그 계정을 내보냅니다. 서비스 스레드 하나에서 씁니다.
 * @details - 경제 서비스와 같은 구조이다. 요청이 들어오는 경로는 둘이며 `onServiceRequest` 는 로그인한 GM 연결의 요청이고 `submitCall` 은 같은 프로세스와 테스트의 요청이다. 작업 하나가 명령 하나이다.
 *          - 조회는 계정 키트의 이름 색인(`IAccountNameIndex`, 저장소)으로 이름이나 id 를 찾으므로 접속하지 않은 계정도 찾는다. 접속 여부만 이 프로세스의 계정 디렉터리로 채운다.
 *          - 세션 끊기는 새로 적용된 정지에서만 한다. 재시도로 받은 지난 결과에서는 다시 끊지 않는다.
 *          - 점검과 공지 명령은 등급만 저장소에서 확인하고 서버 디렉터리 키트(`IServerDirectoryControl`)에 넘긴다. 효과와 감사 기록은 그 키트가 쓰고, 완료는 `update` 가 회수한다.
 *          - 운영 포트는 GM 전용 호스트로 따로 두는 것을 권한다. 사설망과 TLS 를 쓴다. 첫 관리자는 서버 조립 단계가 기동할 때 `seedRole` 로 넣는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Observability/ServiceMetrics.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminProtocol.h"

namespace sw
{
    class IAccountDirectory;
    class IAccountNameIndex;
    class IAccountSessionControl;
    class IBalanceNotice;
    class ILedgerPolicy;
    class ILiveEventControl;
    class IServerDirectoryControl;
    class IServiceStore;
    class MetricRegistry;
    class ServerBus;

    /** @brief GM 서비스 설정입니다. 포인터는 빌려 씁니다. */
    struct AdminServiceConfig
    {
        const IAccountDirectory* _pDirectory{ nullptr };      ///< 이 프로세스에 접속해 있는지에 쓴다
        const IAccountNameIndex* _pNameIndex{ nullptr };      ///< 이름과 id 로 계정을 찾는다(저장소). 계정 키트가 없으면 null 이고 이름 조회는 UnknownAccount 이다
        IAccountSessionControl*  _pSessionControl{ nullptr }; ///< 정지나 영구 정지 뒤에 세션을 끊는다. 계정 키트가 구현한다
        ServerBus*               _pBus{ nullptr };            ///< 제재를 쓴 뒤 `ServiceSanctionBus::kChangedTopic` 을 낸다. 없으면 채팅은 캐시 시한이 지난 뒤에 제재를 본다
        const ILedgerPolicy*     _pPolicy{ nullptr };         ///< 지급 상한이다. 게임이 CurrencyCatalog 를 넘긴다
        MetricRegistry*          _pMetricRegistry{ nullptr };
        IServerDirectoryControl* _pDirectoryControl{ nullptr }; ///< 점검과 공지 명령을 넘긴다. 서버 디렉터리 키트가 없으면 null 이고 그 명령은 Unavailable 이다
        IBalanceNotice*          _pBalanceNotice{ nullptr };    ///< 지급과 회수로 바뀐 잔액을 그 계정에 알린다. 경제 키트가 없으면 null 이고 알리지 않는다
        ILiveEventControl*       _pLiveEventControl{ nullptr }; ///< 라이브 이벤트 명령을 넘긴다. 라이브 운영 키트가 없으면 null 이고 그 명령은 Unavailable 이다
    };
} // namespace sw

namespace sw
{
    /** @class AdminService @brief GM 명령 처리기입니다. */
    class SW_KIT_SERVER_API AdminService final : public IOnlineHostService
    {
    public:
        using ReplyDelegate = Delegate<void( const AdminReply& )>;

        AdminService();
        ~AdminService() override;

        AdminService( const AdminService& )            = delete;
        AdminService& operator=( const AdminService& ) = delete;

        /** @brief 저장소가 없으면 false 를 반환합니다. */
        [[nodiscard]] bool initialize( IServiceStore* pStore, const AdminServiceConfig& settings );
        /** @brief 맡긴 작업이 끝날 때까지 저장소를 회수합니다. 상한은 5초이며 저장소를 종료하기 전에 부릅니다. */
        void shutdown();

        /** @brief 명령 하나를 처리합니다. @p adminID 는 로그인한 GM 계정이며 호스트 경로에서는 `OnlineCallContext::_accountID` 입니다. @p onReply 는 정확히 한 번 불립니다. */
        void submitCall( AccountID adminID, uint16 method, const AdminRequest& request, const NetIdempotencyKey& idempotencyKey, int64 nowMs, ReplyDelegate onReply );
        /** @brief 서버 설정의 첫 관리자를 맡깁니다. 레코드가 없을 때만 넣으며 서버 조립 단계가 기동할 때 부릅니다. */
        void seedRole( AccountID accountID, AdminRole role, int64 nowMs );
        void update( int64 nowMs );

        // IOnlineHostService
        const OnlineProtocolEntry& getProtocol() const override { return AdminProtocol::kOnlineEntry; }
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void                       onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;

        /** @brief 작업의 `complete` 가 호출합니다. */
        void  completeCall( uint64 callID, AdminReply& inoutReply );
        void  completeSeed( AdminResult result );
        int32 getPendingCallCount() const { return static_cast<int32>( _listPendingCall.size() ); }

    private:
        struct PendingCall
        {
            ReplyDelegate      _onReply{};
            NetRequestToken    _token{};
            OnlineServiceHost* _pHost{ nullptr };
            uint64             _callID{ 0 };
            int64              _receivedNanoseconds{ 0 };
            int64              _nowMs{ 0 };
            AdminRequest       _request{}; ///< 점검과 공지 명령만 쓴다. 등급 확인 뒤 서버 디렉터리 키트에 넘긴다
            AccountID          _adminID{ kInvalidAccountID };
            AccountID          _targetID{ kInvalidAccountID };
            int32              _methodIndex{ 0 };
            uint16             _method{ 0 };
        };

        void startCall( AccountID adminID, uint16 method, const AdminRequest& request, const NetIdempotencyKey& idempotencyKey, int64 nowMs, PendingCall pending );
        /** @brief 등급을 확인한 점검과 공지 명령을 서버 디렉터리 키트에 넘깁니다. 넘겼으면 true 이며 완료는 `update` 가 회수한다. */
        [[nodiscard]] bool forwardDirectoryCall( const PendingCall& pending );
        /** @brief 등급을 확인한 라이브 이벤트 명령을 라이브 운영 키트에 넘깁니다. 목록은 바로 @p inoutReply 에 채우고 false, 바꾸기는 넘기고 true 입니다. */
        [[nodiscard]] bool forwardLiveEventCall( const PendingCall& pending, AdminReply& inoutReply );
        /** @brief 넘긴 키트(서버 디렉터리, 라이브 운영)의 완료를 거두어 응답합니다. */
        void pollForwardedCompletions();
        /** @brief 넘긴 명령 하나를 그 결과로 끝냅니다. */
        void finishForwardedCall( uint64 callID, AdminResult result );
        void finishCall( size_t pendingIndex, const AdminReply& reply );

        vector<PendingCall> _listPendingCall;
        ServiceMetrics      _metrics;
        AdminServiceConfig  _settings;
        IServiceStore*      _pStore;
        uint64              _nextCallID;
        int64               _nowMs;
        int32               _pendingSeedCount;
    };
} // namespace sw
