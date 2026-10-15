/**
 * @file ChatSpamGuard.h
 * @brief 도배를 막습니다. 계정마다 토큰 버킷(한꺼번에 쓰기와 간격마다 하나)과 같은 글 반복(정규화 해시가 창 안에서 상한에 닿음)을 확인합니다.
 * @details 계정은 서버 하나에만 접속하므로 이 서버의 메모리로 충분하다. 캐시 왕복이 없다. 서비스 스레드 하나에서 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"

#include "GameFramework/Base/Online/Guard/TokenBucketMap.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

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
     * @brief 도배 방지기입니다.
     */
    class SW_KIT_SERVER_API ChatSpamGuard
    {
    public:
        ChatSpamGuard();

        void initialize( const ChatSpamConfig& settings );

        /** @brief 말하기 하나를 판정합니다. 허락하면 버킷에서 토큰을 꺼내고 반복 기록에 넣습니다. 거절하면 @p outRetryAfterMs 가 기다릴 시간입니다. */
        ChatSpamVerdict check( AccountID accountID, string_view text, int64 nowMs, int64& outRetryAfterMs );
        /** @brief 계정이 떠났을 때 반복 기록을 지웁니다. 버킷은 상한에서 저절로 정리됩니다. */
        void forget( AccountID accountID ) { _mapAccountToHistory.erase( accountID ); }

        /** @brief 정규화한 글의 64비트 해시입니다. 대소문자, 전각, 끼움 글자를 정규화합니다. */
        static uint64 computeTextHash( string_view text );

    private:
        static constexpr int32 kHistorySize = 4;

        /** @brief 계정 하나의 최근 글 해시 링 버퍼입니다. 시각이 0이면 빈 슬롯입니다. */
        struct History
        {
            uint64 _arrHash[kHistorySize]{};
            int64  _arrTimeMs[kHistorySize]{};
            int32  _next{ 0 };
        };

        TokenBucketMap                    _bucketMap;
        unordered_map<AccountID, History> _mapAccountToHistory;
        ChatSpamConfig                    _settings;
    };
} // namespace sw
