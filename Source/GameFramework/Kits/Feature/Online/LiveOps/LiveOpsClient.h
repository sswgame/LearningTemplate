/**
 * @file LiveOpsClient.h
 * @brief 라이브 운영 클라이언트 — `OnlineServiceClient` 에 올리는 `IOnlineClientService`. 열린 이벤트를 받아 들고, 바뀜 알림(`kPushLiveState`)이 오면 스스로 다시 받습니다.
 * @details - 게임은 `getActiveEvents` · `hasEventKind` 만 본다(이벤트 종류를 해석하는 것은 게임 — 경험치 2 배 · 상점 할인).
 *          - 요청마다 완료 델리게이트를 정확히 한 번(`tick` 스레드) — 결과는 몸의 `LiveOpsResult`, 전송 · 공통 오류는 `_errorCode`.
 *          PlayFab 타이틀 데이터 · Unity Remote Config 클라이언트처럼 "알림 → 다시 받기" 이고 알림에 내용을 싣지 않는다(사람마다 대상이 다르다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/LiveOpsProtocol.h"

namespace sw
{
    /** @brief 라이브 운영 클라이언트 응답 하나입니다. */
    struct LiveOpsClientReply
    {
        LiveOpsReply _reply{};
        uint64       _requestId{ 0 };
        uint16       _errorCode{ 0 }; ///< 전송 · 공통 오류(`OnlineError`) — 0 이 아니면 `_reply._result` 는 그 코드의 결과
    };
} // namespace sw

namespace sw
{
    /**
     * @class LiveOpsClient
     * @brief 연결 하나의 라이브 운영 창구입니다.
     */
    class SW_GF_API LiveOpsClient final : public IOnlineClientService
    {
    public:
        using ReplyDelegate = Delegate<void( const LiveOpsClientReply& )>;

        LiveOpsClient();

        /** @brief @p pClient 는 빌려 쓴다 — `registerClientService( this )` 는 부르는 쪽이(초기화 전에). */
        void initialize( OnlineServiceClient* pClient );

        /** @brief 열린 이벤트를 받습니다(로그인 뒤). 지역 · 빌드 판은 들어 두었다가 바뀜 알림 때 다시 쓴다. */
        uint64 requestLiveState( string_view region, uint32 buildVersion, const ReplyDelegate& onReply );
        /** @brief OS 가 준 푸시 토큰을 등록합니다(로그인 뒤, 토큰이 바뀔 때마다 — 같은 토큰은 덮는다). */
        uint64 registerDevice( const PushDeviceRegistration& registration, const ReplyDelegate& onReply );
        uint64 unregisterDevice( string_view providerId, string_view token, const ReplyDelegate& onReply );

        const vector<LiveEventState>& getActiveEvents() const { return _listEvent; }
        bool                          hasEventKind( string_view kind ) const;
        /** @brief 받을 때마다 오르는 수입니다(화면이 다시 그릴지 본다). */
        uint64 getRevision() const { return _revision; }

        // IOnlineClientService
        uint16 getMethodRange() const override { return OnlineMethodRange::kLiveOps; }
        uint32 getProtocolVersion() const override { return LiveOpsProtocol::kVersion; }
        void   onServicePush( uint16 kind, BitReader& body ) override;

    private:
        uint64 send( uint16 method, const BitWriter& body, const ReplyDelegate& onReply );
        void   onResponse( const OnlineResponse& response );

        ServiceClientCallTable<ReplyDelegate> _callTable;
        vector<LiveEventState>                _listEvent;
        string                                _region;
        OnlineServiceClient*                  _pClient;
        uint64                                _revision;
        uint32                                _buildVersion;
        uint8                                 _bRequested; ///< 한 번이라도 받았다 — 알림에 다시 받는다
    };
} // namespace sw
