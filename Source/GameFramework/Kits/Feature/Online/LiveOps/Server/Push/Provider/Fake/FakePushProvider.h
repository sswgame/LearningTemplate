/**
 * @file FakePushProvider.h
 * @brief 가짜 푸시 제공자입니다. 보낸 것을 기록하고, 토큰(토픽 발송은 토픽 이름)마다 정해 둔 결과를 다음 `pollResults` 에 돌려줍니다. 기본 결과는 Delivered 이며 테스트와 개발 서버에서 씁니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationProvider.h"

namespace sw
{
    /**
     * @class FakePushProvider
     * @brief id 가 "fake" 인 가짜 제공자입니다.
     */
    class SW_KIT_SERVER_API FakePushProvider final : public IPushNotificationProvider
    {
    public:
        struct SentRecord
        {
            PushNotificationMessage _message{};
            string                  _deviceToken{}; ///< 토픽 발송이면 비어 있다
            string                  _topic{};       ///< 기기 발송이면 비어 있다
            uint64                  _deliveryID{ 0 };
        };

        FakePushProvider();

        /** @brief @p deviceToken(토픽 발송이면 토픽 이름)에 다음 @p count 번 @p status 를 돌려줍니다. 그 뒤에는 Delivered 를 돌려줍니다. */
        void scriptResult( string_view deviceToken, PushDeliveryStatus status, int32 count = 1, int64 retryAfterMs = 0 );

        string_view getProviderID() const override { return "fake"; }
        void        send( uint64 deliveryID, const string& deviceToken, const string& locale, const PushNotificationMessage& message, int64 nowMs ) override;
        bool        supportsTopics() const override { return true; }
        void        sendToTopic( uint64 deliveryID, string_view topic, const PushNotificationMessage& message, int64 nowMs ) override;
        int32       pollResults( vector<PushDeliveryResult>& outListResult ) override;

        const vector<SentRecord>& getSent() const { return _listSent; }

    private:
        /** @brief 정해 둔 결과를 한 번 소비해 다음 `pollResults` 에 올립니다. */
        void queueResult( uint64 deliveryID, const string& scriptKey );

        struct Script
        {
            int64              _retryAfterMs{ 0 };
            int32              _remainingCount{ 0 };
            PushDeliveryStatus _status{ PushDeliveryStatus::Delivered };
        };

        vector<SentRecord>            _listSent;
        vector<PushDeliveryResult>    _listPending;
        unordered_map<string, Script> _mapTokenToScript;
    };
} // namespace sw
