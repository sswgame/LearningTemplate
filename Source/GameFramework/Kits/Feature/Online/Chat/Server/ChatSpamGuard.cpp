#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatSpamGuard.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatWordFilter.h"

namespace sw
{
    ChatSpamGuard::ChatSpamGuard()
        : _bucketMap{}
        , _mapAccountToHistory{}
        , _settings{}
    {
    }

    void ChatSpamGuard::initialize( const ChatSpamSettings& settings )
    {
        _settings = settings;
        _bucketMap.initialize( settings._burstCount, settings._refillIntervalMs, static_cast<size_t>( settings._maxTrackedAccount ) );
        _mapAccountToHistory.clear();
    }

    uint64 ChatSpamGuard::computeTextHash( string_view text )
    {
        // 코드 포인트 단위 FNV-1a — 글자 하나가 정규화로 사라지거나(끼움 글자) 바뀌어도(대소 · 전각) 같은 값이 된다.
        uint64 hash   = HashUtil::kFnvOffset64;
        size_t offset = 0;
        while ( offset < text.size() )
        {
            const uint32 codepoint = ChatWordFilter::normalizeCodepoint( StringUtil::decodeUtf8( text, offset ) );
            if ( codepoint == 0 )
                continue;
            hash = ( hash ^ codepoint ) * HashUtil::kFnvPrime64;
        }
        return hash;
    }

    ChatSpamVerdict ChatSpamGuard::check( AccountId accountId, string_view text, int64 nowMs, int64& outRetryAfterMs )
    {
        const uint64 hash        = computeTextHash( text );
        History&     history     = _mapAccountToHistory[accountId];
        int32        repeatCount = 0;
        for ( int32 index = 0; index < kHistorySize; ++index )
        {
            const bool bRecent = history._arrTimeMs[index] != 0 && nowMs - history._arrTimeMs[index] < _settings._repeatWindowMs;
            repeatCount += ( bRecent && history._arrHash[index] == hash ) ? 1 : 0;
        }
        if ( repeatCount >= _settings._maxRepeatCount )
        {
            outRetryAfterMs = _settings._repeatWindowMs;
            return ChatSpamVerdict::Repeated;
        }
        if ( _bucketMap.tryConsume( accountId, nowMs, outRetryAfterMs ) == false )
            return ChatSpamVerdict::RateLimited;
        history._arrHash[history._next]   = hash;
        history._arrTimeMs[history._next] = nowMs != 0 ? nowMs : 1;
        history._next                     = ( history._next + 1 ) % kHistorySize;
        outRetryAfterMs                   = 0;
        return ChatSpamVerdict::Allowed;
    }
} // namespace sw
