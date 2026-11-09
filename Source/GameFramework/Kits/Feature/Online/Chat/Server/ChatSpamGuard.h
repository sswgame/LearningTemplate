/**
 * @file ChatSpamGuard.h
 * @brief 도배를 막습니다. 계정마다 토큰 버킷(한꺼번에 쓰기와 간격마다 하나)과 같은 글 반복(정규화 해시가 창 안에서 상한에 닿음)을 확인합니다.
 * @details 계정은 서버 하나에만 접속하므로 이 서버의 메모리로 충분하다. 캐시 왕복이 없다. 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Container/RingBuffer.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatTextNormalizer.h"
#include "GameFramework/Runtime/Online/Guard/Server/TokenBucketMap.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"

namespace sw
{
    /** @brief 도배 방지 설정입니다. */
    struct ChatSpamConfig
    {
        int64 _refillIntervalMs{ 1000 };
        int64 _repeatWindowMs{ 10000 };
        int32 _burstCount{ 5 };
        int32 _maxRepeatCount{ 2 }; ///< 창 안에서 같은 글을 이 횟수까지 허용한다. 그다음 글은 거절한다
        int32 _maxTrackedAccount{ 65536 };
    };

    /** @brief 도배 판정 결과입니다. */
    enum class ChatSpamResult : uint8
    {
        Allowed = 0,
        RateLimited,
        Repeated
    };
} // namespace sw

namespace sw
{
    /**
     * @class ChatSpamGuard
     * @brief 도배 방지기입니다.
     */
    class SW_KIT_SERVER_API ChatSpamGuard
    {
    public:
        ChatSpamGuard();

        /** @brief 설정과 정규화(반복 해시가 쓴다, 금칙어 거르개와 같은 것)를 받습니다. 정규화는 복사해 둔다. */
        void initialize( const ChatSpamConfig& settings, const ChatTextNormalizer& normalizer );

        /** @brief 말하기 하나를 판정합니다. 허락하면 버킷에서 토큰을 꺼내고 반복 기록에 넣습니다. 거절하면 @p outRetryAfterMs 가 기다릴 시간입니다. */
        ChatSpamResult check( AccountID accountID, string_view text, int64 nowMs, int64& outRetryAfterMs );
        /** @brief 계정이 떠났을 때 반복 기록을 지웁니다. 버킷은 상한에서 저절로 정리됩니다. */
        void remove( AccountID accountID ) { _mapAccountToHistory.erase( accountID ); }

    private:
        static constexpr uint32 kHistorySize = 4;

        /** @brief 보낸 글 하나의 해시와 시각입니다. */
        struct SentText
        {
            uint64 _hash{ 0 };
            int64  _timeMs{ 0 };
        };

        /** @brief 계정 하나의 최근 글 링 버퍼입니다. 가득 차면 가장 오래된 글을 덮습니다. */
        using History = RingBuffer<SentText, kHistorySize>;

        TokenBucketMap                    _bucketMap;
        unordered_map<AccountID, History> _mapAccountToHistory;
        ChatSpamConfig                    _settings;
        ChatTextNormalizer                _normalizer;
    };
} // namespace sw
