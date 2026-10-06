/**
 * @file PushNotificationDispatcher.h
 * @brief 푸시 발송기 — 계정마다 도배 제한, 기기 목록 읽기(영속 `liveops_device`), 기기마다 배달, 결과에 따라 지우기 · 물러났다 다시. 서비스 스레드 하나.
 * @details - 기기 레코드 키 `<계정 16 진>/<제공자>.<토큰 해시 16 진>`(같은 토큰 다시 등록 = 덮음), 값 `[판 1][제공자 · 토큰 · 언어 · 등록 시각]`. 계정마다 10 개 — 넘으면
 *            가장 오래 등록된 것을 같은 트랜잭션에서 지운다.
 *          - 결과: InvalidToken 은 기기를 지우고, Transient · RateLimited 는 1 · 2 · 4 · 8 초 물러났다(제공자가 준 시간이 더 길면 그만큼) 다섯 번까지, Rejected 는 버린다(경고).
 *            이 서버 빌드에 없는 제공자의 기기(개발 서버의 실제 제공자)는 건너뛴다.
 *          - 붙어 있는 계정에게는 게임 알림이 먼저다 — "오프라인이면 푸시" 는 부르는 쪽(게임 · 우편 · 이벤트)이 정한다.
 *          PlayFab Push 의 등록 + FCM 관례(무효 토큰이면 등록을 지운다)와 같다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Guard/TokenBucketMap.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Server/LiveOps/Push/PushNotificationProvider.h"

namespace sw
{
    class IServiceStore;

    /** @brief 발송기 설정입니다. */
    struct PushDispatcherSettings
    {
        int64 _accountRefillIntervalMs{ 600000 }; ///< 계정마다 10 분에 하나 다시 채움
        int32 _accountBurst{ 6 };                 ///< 몰아 보내기 상한
        int32 _maxTrackedAccountCount{ 100000 };  ///< 도배 제한이 들고 있는 계정 수 상한
    };
} // namespace sw

namespace sw
{
    /** @brief 발송 수치입니다(관측 · 시험). */
    struct PushDispatcherStats
    {
        uint64 _sentCount{ 0 };
        uint64 _deliveredCount{ 0 };
        uint64 _droppedCount{ 0 }; ///< 다시 하기를 다 썼거나 Rejected
        uint64 _invalidTokenCount{ 0 };
        uint64 _rateLimitedCount{ 0 }; ///< 계정 도배 제한으로 보내지 않음
    };
} // namespace sw

namespace sw
{
    /** @brief 기기 등록 · 해지 요청의 완료입니다. */
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
    class SW_GF_API PushNotificationDispatcher
    {
    public:
        PushNotificationDispatcher();

        PushNotificationDispatcher( const PushNotificationDispatcher& )            = delete;
        PushNotificationDispatcher& operator=( const PushNotificationDispatcher& ) = delete;

        /** @brief @p pStore 는 빌려 쓴다 — 맡긴 일은 저장소의 `shutdown` · `pollCompletions` 가 거두므로 이 객체는 그때까지 살아 있어야 한다. */
        void initialize( IServiceStore* pStore, const PushDispatcherSettings& settings );
        /** @brief 제공자를 올립니다(빌려 쓴다). 같은 id 가 있으면 false. */
        [[nodiscard]] bool registerProvider( IPushNotificationProvider* pProvider );

        /** @brief 계정의 모든 기기에 보냅니다. 도배 제한에 걸리면 false(보내지 않음). */
        bool notifyAccount( AccountId accountId, const PushNotificationMessage& message, int64 nowMs );
        /** @brief 제공자 결과 거두기 · 물러남이 끝난 배달 다시. 저장소 완료는 호스트(또는 서버 루프)가 비운다. */
        void tick( int64 nowMs );

        // 기기 등록(바인딩이 부른다 — 완료는 꼬리표, 0 이면 완료 없음)
        void registerDevice( AccountId accountId, const PushDeviceRegistration& registration, uint64 requestTag );
        void unregisterDevice( AccountId accountId, string_view providerId, string_view token, uint64 requestTag );
        void drainCompletions( vector<PushDeviceCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }

        const PushDispatcherStats& getStats() const { return _stats; }
        int32                      getInFlightCount() const { return static_cast<int32>( _mapDelivery.size() ); }
        int32                      getPendingWorkCount() const { return _pendingWorkCount; }
        bool                       hasProvider( string_view providerId ) const { return findProvider( providerId ) != nullptr; }

        static string makeDeviceKey( AccountId accountId, string_view providerId, string_view token );
        static string makeAccountPrefix( AccountId accountId );

        /** @brief 저장소 일의 `complete` 가 부른다(키트 안). */
        void applyDevices( AccountId accountId, const PushNotificationMessage& message, vector<PushDeviceRegistration>&& listDevice, int64 nowMs );
        void applyWrite( uint64 requestTag, LiveOpsResult result );

    private:
        struct Delivery
        {
            PushNotificationMessage _message{};
            PushDeviceRegistration  _device{};
            AccountId               _accountId{ kInvalidAccountId };
            int64                   _nextAttemptMs{ 0 };
            int32                   _attempt{ 0 }; ///< 실패한 횟수
            uint8                   _bInFlight{ SW_FALSE };
        };

        IPushNotificationProvider* findProvider( string_view providerId ) const;
        void                       sendDelivery( uint64 deliveryId, Delivery& delivery );

        unordered_map<uint64, Delivery>    _mapDelivery;
        vector<IPushNotificationProvider*> _listProvider;
        vector<PushDeliveryResult>         _listResultScratch;
        EventBuffer<PushDeviceCompletion>  _completionBuffer;
        TokenBucketMap                     _accountBucketMap;
        PushDispatcherStats                _stats;
        IServiceStore*                     _pStore;
        uint64                             _nextDeliveryId;
        int32                              _pendingWorkCount;
    };
} // namespace sw
