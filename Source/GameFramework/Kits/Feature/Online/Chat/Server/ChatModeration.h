/**
 * @file ChatModeration.h
 * @brief 채팅 검열입니다. 글 정규화(`ChatTextNormalizer`), 금칙어 거르개(`ChatWordFilter`), 도배 방지기(`ChatSpamGuard`)가 같은 정규화를 씁니다.
 * @details 전각 ASCII(U+FF01~FF5E)를 반각으로, ASCII 대문자를 소문자로 바꾸는 것은 유니코드 규칙이라 코드에 있다. 건너뛸 글자(`b.a.d` 의 `.` · 0 폭 글자)는
 *          운영 정책이라 데이터다(`Config/Server/chat_ignored_characters.txt`, 한 줄에 `U+XXXX` 하나, `#` 은 주석).
 */
#pragma once
#include "Core/Container/RingBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Guard/Server/TokenBucketMap.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"

namespace sw
{
    /**
     * @class ChatTextNormalizer
     * @brief 코드 포인트 하나의 정규화와 정규화한 글의 해시입니다. 만든 뒤에는 읽기만 한다.
     */
    class SW_KIT_SERVER_API ChatTextNormalizer
    {
    public:
        /** @brief 건너뛸 글자 파일을 읽습니다. 파일이 없거나 줄이 `U+XXXX` 가 아니면 오류를 남기고 false 이며 목록은 비어 있습니다. */
        [[nodiscard]] bool loadFromFile( const AbsolutePath& path );
        /** @brief 건너뛸 글자를 바로 정합니다(테스트 · 도구). */
        void setIgnored( vector<uint32> listCodepoint );

        /** @brief 정규화한 글자 하나입니다. 건너뛸 글자이면 0 입니다. */
        uint32 normalize( uint32 codepoint ) const;
        /** @brief 정규화한 글의 64비트 해시(코드 포인트 단위 FNV-1a)입니다. 글자가 건너뛰어지거나 바뀌어도(대소문자 · 전각) 같은 값이 된다. */
        uint64 computeTextHash( string_view text ) const;

    private:
        vector<uint32> _listIgnored; ///< 정렬해 둔다(이진 탐색)
    };
} // namespace sw

// 금칙어 필터입니다. 아호-코라식 자동자(코드 포인트 단위)로 글을 한 번 훑어 걸린 구간을 가리거나 거절합니다.
// 정규화는 `ChatTextNormalizer` 다(대소문자 · 전각, 건너뛸 글자는 데이터). 건너뛸 글자는 건너뛰고 맞추고, 가릴 때는 걸린 구간 안의 그 글자도 가린다
// (`b.a.d` 는 `*****` 가 된다).
// 낱말도 같은 정규화를 거쳐 넣는다. 만든 뒤에는 읽기만 하므로 여러 스레드가 `apply` 를 함께 불러도 된다.
namespace sw
{
    /** @brief 걸렸을 때 할 일입니다. */
    enum class ChatFilterMode : uint8
    {
        Mask = 0, ///< 걸린 코드 포인트를 '*' 로 가린다
        Reject    ///< 걸리면 글 전체를 거절한다
    };

    /** @brief 필터링 판정 결과입니다. */
    enum class ChatFilterResult : uint8
    {
        Clean = 0,
        Masked,
        Rejected,
        InvalidText ///< 잘못된 UTF-8 이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ChatWordFilter
     * @brief 금칙어 필터입니다.
     */
    class SW_KIT_SERVER_API ChatWordFilter
    {
    public:
        ChatWordFilter();

        /** @brief 낱말 목록으로 자동자를 만들고 들어간 낱말 수를 반환합니다. 빈 낱말, 건너뛸 글자만 있는 낱말, 잘못된 UTF-8 낱말은 버립니다. 정규화는 @p normalizer 를 복사해 둔다. */
        int32 initialize( const vector<string>& listWord, ChatFilterMode mode, const ChatTextNormalizer& normalizer );
        /**
         * @brief 텍스트 파일(한 줄에 낱말 하나, `#` 으로 시작하면 주석, UTF-8)을 읽어 `initialize` 합니다.
         * @return 파일을 읽지 못하면 false 를 반환하고 낱말 0개(거르지 않음)로 둡니다. 운영 서버는 기동할 때 이것을 오류로 다룹니다.
         */
        [[nodiscard]] bool loadFromFile( const AbsolutePath& path, ChatFilterMode mode, const ChatTextNormalizer& normalizer );

        /** @brief @p text 를 거릅니다. Masked 이면 @p outText 가 가린 글이고, Clean 이면 원문 그대로이며, Rejected 나 InvalidText 이면 건드리지 않습니다. */
        ChatFilterResult apply( string_view text, string& outText ) const;

        int32          getWordCount() const { return _wordCount; }
        ChatFilterMode getMode() const { return _mode; }

    private:
        /** @brief 자동자 노드입니다. 0번이 루트입니다. */
        struct Node
        {
            unordered_map<uint32, int32> _mapChild{};
            int32                        _fail{ 0 };
            int32                        _matchLength{ 0 }; ///< 여기서 끝나는 가장 긴 낱말의 길이이다. 정규화한 코드 포인트 수이며 실패 링크에서도 물려받는다
        };

        /** @brief @p nodeIndex 노드의 @p codepoint 자식 번호를 반환합니다. 없으면 비어 있습니다. */
        optional<int32> findChild( int32 nodeIndex, uint32 codepoint ) const;
        void            computeFailLinks();

    private:
        vector<Node>       _listNode;
        ChatTextNormalizer _normalizer;
        int32              _wordCount;
        ChatFilterMode     _mode;
    };
} // namespace sw

// 도배를 막습니다. 계정마다 토큰 버킷(한꺼번에 쓰기와 간격마다 하나)과 같은 글 반복(정규화 해시가 창 안에서 상한에 닿음)을 확인합니다.
// 계정은 서버 하나에만 접속하므로 이 서버의 메모리로 충분하다. 캐시 왕복이 없다. 서비스 스레드 하나에서 쓴다.
namespace sw
{
    /**
     * @brief 도배 방지 설정입니다(채팅 키트 설정 `_spam`, 모든 칸 필수). 코드 기본값은 0 이라 채우지 않으면 `ChatService::initialize` 가 거절한다(감사 O-18 — 정책 값을 C++ 상수로 두지 않는다).
     */
    REFLECT()
    struct ChatSpamConfig
    {
        REFLECT_BODY();
        /** @brief 토큰 하나가 다시 차는 간격(ms)입니다. */
        PROPERTY( Required, Min = 1, Max = 3600000 )
        int64 _refillIntervalMs{ 0 };
        /** @brief 같은 글 반복을 세는 창(ms)입니다. */
        PROPERTY( Required, Min = 1, Max = 3600000 )
        int64 _repeatWindowMs{ 0 };
        /** @brief 한꺼번에 보낼 수 있는 글 수(버킷 크기)입니다. */
        PROPERTY( Required, Min = 1, Max = 1000 )
        int32 _burstCount{ 0 };
        /** @brief 창 안에서 같은 글을 이 횟수까지 허용한다. 그다음 글은 거절한다. */
        PROPERTY( Required, Min = 1, Max = 1000 )
        int32 _maxRepeatCount{ 0 };
        /** @brief 버킷을 들고 있을 계정 수의 상한입니다(신고 버킷도 같은 상한을 쓴다). */
        PROPERTY( Required, Min = 1, Max = 16777216 )
        int32 _maxTrackedAccount{ 0 };

        /** @brief 모든 칸이 채워졌는가(코드로 만든 설정이 빈 채로 서비스를 세우지 않게). */
        bool isComplete() const { return _refillIntervalMs > 0 && _repeatWindowMs > 0 && _burstCount > 0 && _maxRepeatCount > 0 && _maxTrackedAccount > 0; }
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
