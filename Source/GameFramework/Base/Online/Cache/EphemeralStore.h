/**
 * @file EphemeralStore.h
 * @brief 휘발성 저장 계약을 정의합니다.
 * @details 만료가 있는 키-값, 원자 증감, 비교 후 쓰기, 정렬 집합(순위표), 발행/구독을 지원한다.
 *          요청을 맡기고(`submit`) 답을 거둔다(`pollReplies`).
 *          자체 서버가 Redis/Valkey 를 세션, 순위, 팬아웃에 쓰고 RDB 를 원본으로 두는 것과 같은 경계이다.
 *
 *          - 잃어도 되는 것만 둔다. 원본은 `IServiceStore` 이다. 캐시를 통째로 잃어도 영속 데이터는 맞아야 한다.
 *            거래의 원본은 영속 원장 트랜잭션이고, 캐시 임대는 "같은 계정의 동시 거래를 일찍 거절"하는 최적화일 뿐이다.
 *            그래서 트랜잭션과 버전이 없고, 모든 값에 만료를 줄 수 있다.
 *          - 키는 ASCII(`[0-9a-z_.:/-]`, 256 B 이하)이다. 드라이버가 배포 접두를 붙인다.
 *          - 점수는 정수(±2^53)이다. RESP 서버의 점수는 배정밀도라 그 범위 안에서만 정확하다.
 *            같은 점수는 멤버 이름의 바이트 역순으로 정렬해, 순위표의 "큰 것부터"와 같은 방향이 되게 한다.
 *          - 발행/구독은 최대 한 번 배달한다. 구독 전이나 끊긴 동안의 메시지는 없다. 놓치면 안 되는 것은 영속 저장에 두고 다시 읽어야 한다.
 *          - 답은 한 앞 안에서 맡긴 순서대로 온다. 한 앞은 서비스 스레드 하나가 맡기고 거둔다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 휘발성 저장 호출의 결과 코드입니다. */
    enum class EphemeralResult : uint8
    {
        Ok = 0,
        NotFound,    ///< 키 · 멤버가 없다
        Conflict,    ///< 조건(없어야 함 · 있어야 함 · 기대 값)이 어긋났다 — 쓰지 않았다
        Unavailable, ///< 서버에 닿지 못했다 — 쓰기면 적용됐는지 모른다
        Invalid      ///< 키 규칙 · 크기 상한 · 점수 범위 · 정수가 아닌 값의 증감
    };
} // namespace sw

namespace sw
{
    /** @brief `Set` 의 조건입니다. */
    enum class EphemeralCondition : uint8
    {
        Always = 0,
        IfAbsent, ///< 없을 때만(임대 얻기)
        IfPresent ///< 있을 때만
    };
} // namespace sw

namespace sw
{
    /** @brief 연산 종류입니다. */
    enum class EphemeralOperation : uint8
    {
        Get = 0,
        Set, ///< _value · _ttlMs(0 = 만료 없음) · _condition
        Erase,
        CompareAndSet,   ///< 지금 값이 _expected 일 때만 _value 로(임대 연장), 만료는 _ttlMs 로
        CompareAndErase, ///< 지금 값이 _expected 일 때만 지운다(임대 놓기 — 남의 임대를 지우지 않는다)
        Increment,       ///< _delta 를 더하고 새 값(_integer). 새로 생긴 키면 _ttlMs 를 건다(고정 창 카운터)
        Expire,          ///< 만료를 _ttlMs 로 다시(0 이하면 바로 지운다)
        ScoreSet,        ///< 정렬 집합 _key 의 _member 점수를 _score 로
        ScoreAdd,        ///< _member 점수에 _score 를 더하고 새 점수(_integer · _score)
        ScoreRemove,
        ScoreRank,  ///< 큰 것부터 0 기준 순위(_integer)와 점수(_score) — 없으면 NotFound
        ScoreRange, ///< 큰 것부터 [_offset, _offset + _count) 의 멤버 · 점수(_listMember)
        Publish     ///< 채널 _key 에 _value — 받은 구독자 수(_integer, 서버 전체)
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나입니다. `make*` 로 만듭니다. */
    struct SW_GF_API EphemeralRequest
    {
        vector<uint8>      _value{};
        vector<uint8>      _expected{};
        string             _key{};
        string             _member{};
        int64              _ttlMs{ 0 };
        int64              _delta{ 0 };
        int64              _score{ 0 };
        int32              _offset{ 0 };
        int32              _count{ 0 };
        EphemeralCondition _condition{ EphemeralCondition::Always };
        EphemeralOperation _operation{ EphemeralOperation::Get };

        static EphemeralRequest makeGet( string_view key );
        static EphemeralRequest makeSet( string_view key, vector<uint8> valueBytes, int64 ttlMs, EphemeralCondition condition = EphemeralCondition::Always );
        static EphemeralRequest makeErase( string_view key );
        static EphemeralRequest makeCompareAndSet( string_view key, vector<uint8> expectedBytes, vector<uint8> valueBytes, int64 ttlMs );
        static EphemeralRequest makeCompareAndErase( string_view key, vector<uint8> expectedBytes );
        static EphemeralRequest makeIncrement( string_view key, int64 delta, int64 ttlMsWhenCreated );
        static EphemeralRequest makeExpire( string_view key, int64 ttlMs );
        static EphemeralRequest makeScoreSet( string_view key, string_view member, int64 score );
        static EphemeralRequest makeScoreAdd( string_view key, string_view member, int64 delta );
        static EphemeralRequest makeScoreRemove( string_view key, string_view member );
        static EphemeralRequest makeScoreRank( string_view key, string_view member );
        static EphemeralRequest makeScoreRange( string_view key, int32 offset, int32 count );
        static EphemeralRequest makePublish( string_view channel, vector<uint8> messageBytes );

        /** @brief 키 규칙, 크기, 점수 범위를 검사합니다. 구현이 맡기기 전에 부르며, 어긋나면 Invalid 답을 냅니다. */
        bool isWellFormed() const;
        /** @brief 키가 키 규칙(`[0-9a-z_.:/-]`, 1..256 B)을 지키면 true 입니다. */
        static bool isValidKey( string_view key );
    };
} // namespace sw

namespace sw
{
    /** @brief 정렬 집합의 멤버 하나입니다. */
    struct EphemeralScoredMember
    {
        string _member{};
        int64  _score{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 답 하나입니다. */
    struct EphemeralReply
    {
        vector<uint8>                 _value{};
        vector<EphemeralScoredMember> _listMember{};
        uint64                        _requestID{ 0 };
        int64                         _integer{ 0 };
        int64                         _score{ 0 };
        EphemeralResult               _result{ EphemeralResult::Ok };
        EphemeralOperation            _operation{ EphemeralOperation::Get };
    };
} // namespace sw

namespace sw
{
    /** @brief 구독한 채널에 온 메시지입니다. */
    struct EphemeralMessage
    {
        vector<uint8> _bytes{};
        string        _channel{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class IEphemeralStore
     * @brief 휘발성 저장의 앞입니다. 서비스 스레드 하나가 맡기고 거둡니다.
     * @details 구현은 `MemoryEphemeralStore`(서버 한 대와 테스트)와 RESP 구현(GF_Server_CacheStore)이 있다.
     *          서비스는 `pollReplies` 나 `pollMessages` 를 직접 부르지 않고 `host.getEphemeralRouter()` 를 쓴다.
     *          직접 부르면 앞 전체의 답을 꺼내므로, 소비자가 둘이면 서로의 답을 가져간다.
     */
    class SW_GF_API IEphemeralStore
    {
    public:
        static constexpr int32 kMaxKeySize   = 256;
        static constexpr int32 kMaxValueSize = 64 * 1024;
        static constexpr int64 kMaxAbsScore  = 1ll << 53;

        IEphemeralStore()          = default;
        virtual ~IEphemeralStore() = default;

        IEphemeralStore( const IEphemeralStore& )            = delete;
        IEphemeralStore& operator=( const IEphemeralStore& ) = delete;

        /** @brief 요청을 맡기고 요청 id 를 반환합니다. id 는 1 부터 시작하며 답의 `_requestID` 와 같습니다. */
        virtual uint64 submit( const EphemeralRequest& request ) = 0;
        /** @brief 끝난 답을 @p outListReply 뒤에 붙이고, 붙인 수를 반환합니다. */
        virtual int32 pollReplies( vector<EphemeralReply>& outListReply ) = 0;

        virtual void  subscribe( string_view channel )                         = 0;
        virtual void  unsubscribe( string_view channel )                       = 0;
        virtual int32 pollMessages( vector<EphemeralMessage>& outListMessage ) = 0;

        /** @brief 맡았지만 아직 거두지 않은 답의 수를 반환합니다. */
        virtual int32 getPendingCount() const = 0;
        /** @brief 남은 요청은 Unavailable 로 답하고 구독을 풉니다. 그 뒤에 맡긴 요청도 Unavailable 입니다. */
        virtual void shutdown() = 0;
    };
} // namespace sw
