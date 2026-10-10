/**
 * @file ChatSpamGuard.h
 * @brief 도배 막이 — 계정마다 토큰 버킷(몰아 쓰기 + 간격마다 하나)과 같은 글 반복(정규화 해시가 창 안에서 상한에 닿음)을 봅니다.
 * @details 계정은 서버 하나에만 붙으므로 이 서버의 메모리로 충분하다(캐시 왕복 없음). 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"

#include "GameFramework/Base/Online/Guard/TokenBucketMap.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 도배 막이 설정입니다. */
    struct ChatSpamSettings
    {
        int64 _refillIntervalMs{ 1000 };
        int64 _repeatWindowMs{ 10000 };
        int32 _burstCount{ 5 };
        int32 _maxRepeatCount{ 2 }; ///< 창 안에서 같은 글은 이만큼까지(다음 것을 거절)
        int32 _maxTrackedAccount{ 65536 };
    };

    /** @brief 도배 판정입니다. */
    enum class ChatSpamVerdict : uint8
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
     * @brief 도배 막이입니다.
     */
    class SW_GF_API ChatSpamGuard
    {
    public:
        ChatSpamGuard();

        void initialize( const ChatSpamSettings& settings );

        /** @brief 말하기 하나를 판정합니다. 허락이면 버킷에서 꺼내고 반복 기록에 넣습니다. 거절이면 @p outRetryAfterMs 가 기다릴 시간입니다. */
        ChatSpamVerdict check( AccountID accountID, string_view text, int64 nowMs, int64& outRetryAfterMs );
        /** @brief 계정이 떠났다 — 반복 기록을 지웁니다(버킷은 상한에서 저절로 정리된다). */
        void forget( AccountID accountID ) { _mapAccountToHistory.erase( accountID ); }

        /** @brief 정규화(대소 · 전각 · 끼움 글자)한 글의 64 비트 해시입니다. */
        static uint64 computeTextHash( string_view text );

    private:
        static constexpr int32 kHistorySize = 4;

        /** @brief 계정 하나의 최근 글 해시 고리입니다. 시각 0 은 빈 칸이다. */
        struct History
        {
            uint64 _arrHash[kHistorySize]{};
            int64  _arrTimeMs[kHistorySize]{};
            int32  _next{ 0 };
        };

        TokenBucketMap                    _bucketMap;
        unordered_map<AccountID, History> _mapAccountToHistory;
        ChatSpamSettings                  _settings;
    };
} // namespace sw
