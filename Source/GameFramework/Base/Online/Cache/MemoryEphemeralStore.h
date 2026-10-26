/**
 * @file MemoryEphemeralStore.h
 * @brief 메모리 휘발성 저장소입니다. 서버 한 대와 테스트에 씁니다.
 *        데이터(`MemoryEphemeralDatabase`: 받은 시계(`IClock`), 게으른 만료, 채널 받은편지함)와 프런트(`MemoryEphemeralStore`)가 나뉩니다.
 * @details - 프런트의 `submit` 은 즉시 실행해 답을 큐에 넣는다(메모리 서비스 저장소와 같은 계약이며 결정적이다). "서버 둘" 테스트는 프런트 둘이 데이터 하나를 쓴다.
 *          - 만료는 읽을 때 지운다(게으른 만료, Valkey 와 같은 관측 결과). `nowMs >= 만료 시각` 이면 없는 것이다.
 *          - 발행은 그 채널을 구독한 앞마다 받은편지함에 넣고, 앞의 `pollMessages` 가 꺼낸다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class MemoryEphemeralDatabase
     * @brief 키별 값과 만료, 정렬 집합, 채널 구독과 받은편지함입니다. 잠금 하나로 모든 호출을 직렬화합니다(프런트 여럿이 같이 쓴다).
     */
    class SW_GF_API MemoryEphemeralDatabase : private SubscriptionSource
    {
    public:
        /**
         * @param clock 만료를 계산하는 시계입니다(운영 `SystemMonotonicClock`, 가짜 시각으로 도는 서버 · 테스트는 `ManualClock`). 데이터보다 오래 살아야 합니다(참조만 든다).
         */
        explicit MemoryEphemeralDatabase( const IClock& clock );

        /** @brief 요청 하나를 실행해 답을 돌려줍니다(요청 id 는 프런트가 채운다). 보내기 전의 형식 검사는 프런트가 맡습니다. */
        EphemeralReply execute( const EphemeralRequest& request );

        /** @brief 앞 하나를 받은편지함 주인으로 등록하고 그 구독을 돌려줍니다. 구독이 사라지면 받은편지함과 그 채널 구독을 모두 지웁니다. 번호는 @p outInboxID 로 받습니다. */
        [[nodiscard]] Subscription registerInbox( uint64& outInboxID );
        void                       subscribe( uint64 inboxID, string_view channel );
        void                       unsubscribe( uint64 inboxID, string_view channel );
        /** @brief @p inboxID 에 온 메시지를 @p outListMessage 뒤에 붙이고 비웁니다. 붙인 수입니다. */
        int32 takeMessages( uint64 inboxID, vector<EphemeralMessage>& outListMessage );

        /** @brief 받은 시계의 지금 시각(밀리초)입니다. */
        int64 getNowMs() const { return _clock.nowMilliseconds().count(); }
        /** @brief 값과 정렬 집합을 모두 지웁니다(구독은 남긴다). 테스트의 "캐시 서버가 재시작해 비었다" 에 씁니다. */
        void clearData();

    private:
        struct ValueEntry
        {
            vector<uint8> _bytes{};
            int64         _expiresAtMs{ 0 }; ///< 0 = 만료 없음
        };

        struct ScoreSetEntry
        {
            map<string, int64> _mapMemberToScore{};
            int64              _expiresAtMs{ 0 };
        };

        struct Inbox
        {
            vector<EphemeralMessage> _listMessage{};
            vector<string>           _listChannel{};
        };

        /** @brief 받은편지함 @p id 의 구독이 풀렸습니다. 받은편지함을 지웁니다(`_mutex`). */
        void releaseSubscription( uint64 id ) override;
        /** @brief 만료됐으면 지우고, 살아 있는 값이면 그것입니다. */
        ValueEntry*    findValue( const string& key, int64 nowMs );
        ScoreSetEntry* findScoreSet( const string& key, int64 nowMs );
        /** @brief 정렬 집합을 점수 내림차순 · 같은 점수는 멤버 바이트 내림차순으로 펼칩니다. */
        static vector<EphemeralScoredMember> makeRanking( const ScoreSetEntry& entry );

        void executeValue( const EphemeralRequest& request, int64 nowMs, EphemeralReply& outReply );
        void executeScore( const EphemeralRequest& request, int64 nowMs, EphemeralReply& outReply );
        void executePublish( const EphemeralRequest& request, EphemeralReply& outReply );

        const IClock&                        _clock;
        mutable mutex                        _mutex;
        unordered_map<string, ValueEntry>    _mapValue;
        unordered_map<string, ScoreSetEntry> _mapScoreSet;
        unordered_map<uint64, Inbox>         _mapInbox;
        IDAllocator<uint64>                  _inboxIDAllocator;
    };
} // namespace sw

namespace sw
{
    /**
     * @class MemoryEphemeralStore
     * @brief 메모리 데이터의 프런트입니다. `submit` 이 즉시 실행하고 답은 `pollReplies` 까지 쌓는다. 서비스 스레드 하나가 쓴다.
     */
    class SW_GF_API MemoryEphemeralStore final : public IEphemeralStore
    {
    public:
        /** @brief @p pDatabase 는 빌려 쓴다(앞보다 오래 산다). */
        explicit MemoryEphemeralStore( MemoryEphemeralDatabase* pDatabase );
        ~MemoryEphemeralStore() override;

        uint64 submit( const EphemeralRequest& request ) override;
        int32  pollReplies( vector<EphemeralReply>& outListReply ) override;

        void  subscribe( string_view channel ) override;
        void  unsubscribe( string_view channel ) override;
        int32 pollMessages( vector<EphemeralMessage>& outListMessage ) override;

        int32 getPendingCount() const override { return static_cast<int32>( _listReply.size() ); }
        void  shutdown() override;

    private:
        vector<EphemeralReply>   _listReply;
        MemoryEphemeralDatabase* _pDatabase;
        uint64                   _inboxID;
        Subscription             _inboxSubscription; ///< 데이터의 받은편지함. `shutdown` 이나 소멸이 지운다
        IDAllocator<uint64>      _requestIDAllocator;
        uint8                    _bShutdown;
    };
} // namespace sw
