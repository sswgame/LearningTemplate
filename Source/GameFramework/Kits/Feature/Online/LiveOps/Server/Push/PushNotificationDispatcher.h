/**
 * @file PushNotificationDispatcher.h
 * @brief 푸시 발송기입니다. 계정마다 도배를 제한하고, 기기 목록(영속 테이블 `liveops_device`)을 읽어 기기마다 배달하며, 결과에 따라 기기를 지우거나 백오프 뒤 다시 보냅니다. 서비스 스레드 하나에서 씁니다.
 * @details - 기기 레코드의 키는 `<계정 16진>/<제공자>.<토큰 해시 16진>` 이며 같은 토큰을 다시 등록하면 덮어쓴다. 값은 `[버전 1][제공자, 토큰, 언어, 등록 시각]` 이다.
 *            계정마다 10개까지 두며 넘으면 가장 오래 등록된 것을 같은 트랜잭션에서 지운다.
 *          - 결과에 따른 처리는 다음과 같다. InvalidToken 은 기기를 지운다. Transient 와 RateLimited 는 1, 2, 4, 8초 백오프로 최대 다섯 번까지 다시 보내며, 제공자가 준 시간이 더 길면 그 시간을 쓴다.
 *            Rejected 는 경고를 남기고 버린다. 이 서버 빌드에 없는 제공자의 기기(개발 서버의 실제 제공자)는 건너뛴다.
 *          - 접속해 있는 계정에게는 게임 알림이 먼저이다. "오프라인이면 푸시" 는 호출하는 쪽(게임, 우편, 이벤트)이 정한다.
 *
 *          PlayFab Push 의 등록과 FCM 관례(무효 토큰이면 등록을 지운다)와 같다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Guard/TokenBucketMap.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationProvider.h"

namespace sw
{
    enum class ServiceStoreResult : uint8;

    class IServiceStore;

    /** @brief 발송기 설정입니다. */
    struct PushDispatcherConfig
    {
        int64 _accountRefillIntervalMs{ 600000 }; ///< 계정마다 10분에 하나씩 다시 채운다
        int32 _accountBurst{ 6 };                 ///< 한꺼번에 보낼 수 있는 상한이다
        int32 _maxTrackedAccountCount{ 100000 };  ///< 도배 제한이 보관하는 계정 수의 상한이다
    };
} // namespace sw

namespace sw
{
    /** @brief 발송 통계입니다. 관측과 테스트에 씁니다. */
    struct PushDispatcherStats
    {
        uint64 _sentCount{ 0 };
        uint64 _deliveredCount{ 0 };
        uint64 _droppedCount{ 0 }; ///< 재시도를 모두 소진했거나 Rejected 인 횟수이다
        uint64 _invalidTokenCount{ 0 };
        uint64 _rateLimitedCount{ 0 };  ///< 계정 도배 제한 때문에 보내지 않은 횟수이다
        uint64 _topicClaimedCount{ 0 }; ///< 토픽 알림의 표를 써서 이 서버가 보낸 횟수이다
        uint64 _topicSkippedCount{ 0 }; ///< 다른 서버가 이미 보냈거나 저장소에 닿지 못해 보내지 않은 토픽 알림 수이다
    };
} // namespace sw

namespace sw
{
    /** @brief 기기 등록과 해지 요청의 완료입니다. */
    struct PushDeviceCompletion
    {
        uint64        _requestTag{ 0 };
        LiveOpsResult _result{ LiveOpsResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @class PushNotificationDispatcher
     * @brief 푸시 발송기입니다.
     */
    class SW_KIT_SERVER_API PushNotificationDispatcher
    {
    public:
        PushNotificationDispatcher();

        PushNotificationDispatcher( const PushNotificationDispatcher& )            = delete;
        PushNotificationDispatcher& operator=( const PushNotificationDispatcher& ) = delete;

        /** @brief @p pStore 는 빌려 씁니다. 맡긴 작업은 저장소의 `shutdown` 과 `pollCompletions` 가 회수하므로 이 객체는 그때까지 살아 있어야 합니다. */
        void initialize( IServiceStore* pStore, const PushDispatcherConfig& settings );
        /** @brief 제공자를 등록합니다. 빌려 쓰는 포인터이며 같은 id 가 있으면 false 를 반환합니다. */
        [[nodiscard]] bool registerProvider( IPushNotificationProvider* pProvider );

        /** @brief 계정의 모든 기기에 보냅니다. 도배 제한에 걸리면 보내지 않고 false 를 반환합니다. */
        bool notifyAccount( AccountID accountID, const PushNotificationMessage& message, int64 nowMs );
        /**
         * @brief 토픽을 구독한 모든 기기에 한 번에 보냅니다(전체 대상 알림 — 계정마다 보내지 않는다). 토픽 발송을 하는 제공자(`supportsTopics`)마다 한 번씩 맡기고 맡긴 수를 반환합니다.
         * @details 0 이면 토픽을 보낼 제공자가 없다는 뜻이다(호출하는 쪽이 알 수 있게 숫자로 돌려준다). 같은 알림을 서버 여럿이 보내지 않게 하는 일은 호출하는 쪽의 몫이다.
         *          재시도는 기기 배달과 같다. 계정 도배 제한은 적용하지 않는다(계정이 없다).
         */
        int32 notifyTopic( string_view topic, const PushNotificationMessage& message, int64 nowMs );
        /**
         * @brief `notifyTopic` 을 같은 @p claimKey 로 한 번만 합니다 — 저장소의 `liveops_push` 에 "없어야 한다" 조건으로 표를 쓰고, 쓴 서버만 보냅니다.
         * @details 서버 여럿이 같은 이벤트 시작을 보고도 전체 대상 알림이 한 번만 나가게 한다. 키는 저장소 키 규칙(`[0-9a-z_./-]`)을 지켜야 한다. 저장소에 닿지 못하면 보내지 않고 센다
         *          (다음 시작 알림이 아니라 이번 알림을 놓치는 쪽이 중복보다 낫다). 표는 지우지 않는다 — 창마다 하나뿐이다.
         */
        void notifyTopicOnce( string_view claimKey, string_view topic, const PushNotificationMessage& message, int64 nowMs );
        /** @brief 제공자의 결과를 회수하고, 백오프가 끝난 배달을 다시 보냅니다. 저장소 완료는 호스트(또는 서버 루프)가 비웁니다. */
        void update( int64 nowMs );

        // 기기 등록 함수들이며 바인딩이 부른다. 완료는 요청 태그로 받고, 태그가 0이면 완료가 없다
        void registerDevice( AccountID accountID, const PushDeviceRegistration& registration, uint64 requestTag );
        void unregisterDevice( AccountID accountID, string_view providerID, string_view token, uint64 requestTag );
        void drainCompletions( vector<PushDeviceCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }

        const PushDispatcherStats& getStats() const { return _stats; }
        int32                      getInFlightCount() const { return static_cast<int32>( _mapDelivery.size() ); }
        int32                      getPendingWorkCount() const { return _pendingWorkCount; }
        bool                       hasProvider( string_view providerID ) const { return findProvider( providerID ) != nullptr; }

        static string makeDeviceKey( AccountID accountID, string_view providerID, string_view token );
        static string makeAccountPrefix( AccountID accountID );

        /** @brief 저장소 작업의 `complete` 가 호출합니다. 키트 안에서만 씁니다. */
        void applyDevices( AccountID accountID, const PushNotificationMessage& message, vector<PushDeviceRegistration>&& listDevice, int64 nowMs );
        void applyWrite( uint64 requestTag, LiveOpsResult result );
        void applyTopicClaim( const string& topic, const PushNotificationMessage& message, ServiceStoreResult result, int64 nowMs );

    private:
        struct Delivery
        {
            PushNotificationMessage _message{};
            PushDeviceRegistration  _device{}; ///< 토픽 배달이면 제공자 id 만 채운다
            string                  _topic{};  ///< 토픽 배달의 토픽 이름
            AccountID               _accountID{ kInvalidAccountID };
            int64                   _nextAttemptMs{ 0 };
            int32                   _attempt{ 0 }; ///< 실패한 횟수이다
            uint8                   _bInFlight{ SW_FALSE };
            uint8                   _bTopic{ SW_FALSE }; ///< 기기가 아니라 토픽으로 보내는 배달이다
        };

        IPushNotificationProvider* findProvider( string_view providerID ) const;
        void                       sendDelivery( uint64 deliveryID, Delivery& delivery, int64 nowMs );

        unordered_map<uint64, Delivery>    _mapDelivery;
        vector<IPushNotificationProvider*> _listProvider;
        vector<PushDeliveryResult>         _listResultScratch;
        EventBuffer<PushDeviceCompletion>  _completionBuffer;
        TokenBucketMap                     _accountBucketMap;
        PushDispatcherStats                _stats;
        IServiceStore*                     _pStore;
        uint64                             _nextDeliveryID;
        int32                              _pendingWorkCount;
    };
} // namespace sw
