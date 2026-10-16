/**
 * @file LiveOpsClient.h
 * @brief 라이브 운영 클라이언트입니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며, 열린 이벤트를 받아 보관하고 변경 알림(`kPushLiveState`)이 오면 스스로 다시 받습니다.
 * @details - 게임은 `getActiveEvents` 와 `hasEventKind` 만 본다. 이벤트 종류를 해석하는 것은 게임이며 경험치 2배나 상점 할인이 예이다.
 *          - 요청마다 완료 델리게이트를 `tick` 스레드에서 정확히 한 번 부른다. 결과는 본문의 `LiveOpsResult` 에, 전송 오류와 공통 오류는 `_errorCode` 에 있다.
 *
 *          PlayFab 의 타이틀 데이터와 Unity Remote Config 클라이언트처럼 "알림을 받으면 다시 받는" 방식이다. 사람마다 대상이 다르므로 알림에는 내용을 싣지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineKitClient.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsProtocol.h"

namespace sw
{
    /**
     * @class LiveOpsClient
     * @brief 연결 하나의 라이브 운영 클라이언트입니다.
     */
    class SW_KIT_API LiveOpsClient final : public OnlineKitClient<LiveOpsProtocol, LiveOpsReply>
    {
    public:
        LiveOpsClient();

        /** @brief 열린 이벤트를 받습니다. 로그인한 뒤에 부릅니다. 지역과 빌드 버전은 보관했다가 변경 알림이 올 때 다시 씁니다. */
        uint64 requestLiveState( string_view region, uint32 buildVersion, const ReplyDelegate& onReply );
        /** @brief OS 가 준 푸시 토큰을 등록합니다. 로그인한 뒤 토큰이 바뀔 때마다 부르며, 같은 토큰은 덮어씁니다. */
        uint64 registerDevice( const PushDeviceRegistration& registration, const ReplyDelegate& onReply );
        uint64 unregisterDevice( string_view providerID, string_view token, const ReplyDelegate& onReply );

        const vector<LiveEventState>& getActiveEvents() const { return _listEvent; }
        bool                          hasEventKind( string_view kind ) const;
        /** @brief 받을 때마다 올라가는 수입니다. 화면이 다시 그릴지 판단하는 데 씁니다. */
        uint64 getRevision() const { return _revision; }

        // IOnlineClientService
        const OnlineProtocolEntry& getProtocol() const override { return LiveOpsProtocol::kOnlineEntry; }
        void                       onServicePush( uint16 kind, BitReader& body ) override;

    private:
        void onCallSucceeded( const Call& call, const LiveOpsReply& reply ) override;

        vector<LiveEventState> _listEvent;
        string                 _region;
        uint64                 _revision;
        uint32                 _buildVersion;
        uint8                  _bRequested; ///< 한 번이라도 받았다. 알림이 오면 다시 받는다
    };
} // namespace sw
