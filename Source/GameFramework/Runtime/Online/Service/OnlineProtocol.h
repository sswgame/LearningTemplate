/**
 * @file OnlineProtocol.h
 * @brief 온라인 서비스의 와이어 테이블입니다. 키트마다의 메서드 영역, 호스트 메서드, 공통 오류 코드, 기반 프로토콜 버전을 둡니다.
 * @details - 메서드(uint16) = 영역 + 0x00..0x7F, 알림 종류(Message 프레임 `[종류 u16][몸]`) = 영역 + 0x80..0xFF. 키트 오류 = 영역 + n(공통 0..255 는 `OnlineError`).
 *          - 영역 값은 와이어 형식이다. 바꾸면 `OnlineProtocolConstant::kBaseVersion` 을 올린다. 키트의 메서드와 알림 번호는 `OnlineMethodTable` 의 열거자 순서에서 나오고,
 *            그 이름 순서가 온라인 스키마 해시에 섞인다(`OnlineProtocolRegistry.h`).
 *          - 영역이 겹치는 키트 둘을 한 호스트에 올리면 `OnlineServiceHost::registerServerService` 가 거절한다(조립 실수를 기동에서 잡는다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetRequest.h"

#include <type_traits>
#include <utility>

namespace sw
{
    /**
     * @struct OnlineMethodRange
     * @brief 온라인 기능 키트마다의 요청 메서드 번호 첫 값입니다(키트마다 256 개).
     */
    struct OnlineMethodRange
    {
        static constexpr uint16 kSize            = 0x0100;
        static constexpr uint16 kMethodCount     = 0x0080; ///< 영역 + 0x00..0x7F 가 메서드, 나머지는 알림 종류
        static constexpr uint16 kHost            = 0x0000; ///< Hello · 핑(호스트 자신)
        static constexpr uint16 kAccount         = 0x0100;
        static constexpr uint16 kServerDirectory = 0x0200;
        static constexpr uint16 kTrade           = 0x0300;
        static constexpr uint16 kEconomy         = 0x0400;
        static constexpr uint16 kMailbox         = 0x0500;
        static constexpr uint16 kAdmin           = 0x0600;
        static constexpr uint16 kChat            = 0x0700;
        static constexpr uint16 kSocial          = 0x0800;
        static constexpr uint16 kLeaderboard     = 0x0900;
        static constexpr uint16 kMatchmaking     = 0x0A00;
        static constexpr uint16 kLiveOps         = 0x0B00;
        static constexpr uint16 kGame            = 0x8000; ///< 게임용으로 예약한 영역(0x8000..0xFFFF, 영역 128 개)

        static constexpr bool   isInRange( uint16 value, uint16 rangeBase ) { return rangeBase <= value && value < rangeBase + kSize; }
        static constexpr uint16 getRangeBase( uint16 value ) { return static_cast<uint16>( value & 0xFF00u ); }
        /** @brief 영역 안의 메서드 번호(0x00..0x7F)인지입니다. 아니면 알림 종류입니다. */
        static constexpr bool isMethod( uint16 value ) { return ( value & 0x00FFu ) < kMethodCount; }
    };
} // namespace sw

namespace sw
{
    /** @brief 알림을 보내지 않는 키트가 `OnlineMethodTable` 의 알림 테이블로 고르는 빈 열거형입니다. 열거자가 없으므로 이름을 찾지 않습니다. */
    enum class OnlineNoPushName : uint8
    {
        Count,
    };

    /** @brief 본문이 없는 요청 · 응답 · 알림의 자리표입니다. 와이어에 아무것도 싣지 않고 스키마 해시에도 섞이지 않습니다(`OnlineMessageTypes`). */
    struct OnlineNoBody
    {
    };

    /** @brief 메서드 서술자의 호출 규칙입니다. 규칙은 와이어의 뜻이므로 표의 해시에 섞입니다(`OnlineProtocolTable::computeMessageHash`). */
    enum class OnlineMethodFlag : uint8
    {
        None                   = 0,
        RequiresIdempotencyKey = 1u << 0, ///< 상태를 바꾸므로 멱등 키가 필수이다. 클라이언트는 없으면 만들고, 서버는 없으면 `kInvalidRequest` 로 답한다
        Anonymous              = 1u << 1, ///< 로그인 없이 부를 수 있다
    };

    constexpr OnlineMethodFlag operator|( OnlineMethodFlag left, OnlineMethodFlag right )
    {
        return static_cast<OnlineMethodFlag>( static_cast<uint8>( left ) | static_cast<uint8>( right ) );
    }

    /**
     * @brief 키트의 요청 메서드와 알림 종류의 표입니다. @p TMethodName 과 @p TPushName 은 `ENUM( Count = Count )` 열거형이고, 열거자 순서가 와이어 번호입니다.
     *        메서드는 @p RangeBase + 1 부터, 알림은 @p RangeBase + 0x80 부터 연속입니다. 알림이 없는 키트는 @p TPushName 으로 `OnlineNoPushName` 을 적습니다.
     * @details 키트의 `XxxMethod` 가 이 테이블을 상속해 와이어 상수를 `toMethod` 와 `toPush` 로 만들고, 등록 항목(`OnlineProtocolEntry`)이 이 테이블을 받아
     *          두 열거형의 이름 순서를 온라인 스키마 해시에 섞습니다. 그래서 같은 타입을 받는 두 메서드의 번호를 맞바꾸면(열거자 순서를 바꾸면) 해시가 바뀌어
     *          빌드가 다른 서버와 클라이언트가 접속 때 갈립니다. 번호를 손으로 적는 곳이 없으므로 테이블 밖의 번호가 생기지 않습니다.
     *          서비스 지표(`ServiceMetrics::initialize`)는 메서드 열거자 이름의 snake_case 를 메서드 라벨로 씁니다.
     *          새 메서드와 알림은 열거형 끝(`Count` 앞)에 더합니다. 중간에 넣어도 해시가 바뀌어 접속에서 갈리지만, 지표 라벨의 번호가 밀립니다.
     */
    template <typename TMethodName, uint16 RangeBase, typename TPushName>
    struct OnlineMethodTable
    {
        using MethodName = TMethodName;
        using PushName   = TPushName;

        static constexpr uint16 kRangeBase   = RangeBase;
        static constexpr int32  kCount       = static_cast<int32>( TMethodName::Count );
        static constexpr int32  kPushCount   = static_cast<int32>( TPushName::Count );
        static constexpr uint16 kFirstMethod = static_cast<uint16>( RangeBase + 1 );
        static constexpr uint16 kFirstPush   = static_cast<uint16>( RangeBase + OnlineMethodRange::kMethodCount );

        static constexpr uint16 toMethod( TMethodName name ) { return static_cast<uint16>( kFirstMethod + static_cast<uint16>( name ) ); }
        static constexpr uint16 toPush( TPushName name ) { return static_cast<uint16>( kFirstPush + static_cast<uint16>( name ) ); }

        /**
         * @brief 메서드 서술자입니다. 메서드와 요청 · 응답 타입의 짝, 호출 규칙을 형식 하나에 적습니다. 키트는 `using Claim = Method<MethodName::Claim, XRequest, XReply, ...>;` 로 씁니다.
         * @details 번호는 지금까지처럼 열거자 순서이고, 짝은 `OnlineProtocolTable` 이 스키마 해시에 섞습니다. gRPC `rpc Claim(ClaimRequest) returns (ClaimReply)`, 언리얼 Online Services 의 `struct FOp { Params; Result; }` 자리입니다(기억에 의존).
         */
        template <TMethodName Name, typename TRequest, typename TReply, OnlineMethodFlag Flags = OnlineMethodFlag::None>
        struct Method
        {
            using Request = TRequest;
            using Reply   = TReply;

            static constexpr uint16 kID                     = static_cast<uint16>( RangeBase + 1 + static_cast<uint16>( Name ) );
            static constexpr int32  kIndex                  = static_cast<int32>( Name );
            static constexpr uint8  kFlagBits               = static_cast<uint8>( Flags );
            static constexpr bool   kRequiresIdempotencyKey = ( kFlagBits & static_cast<uint8>( OnlineMethodFlag::RequiresIdempotencyKey ) ) != 0;
            static constexpr bool   kAnonymous              = ( kFlagBits & static_cast<uint8>( OnlineMethodFlag::Anonymous ) ) != 0;
        };

        /** @brief 알림 서술자입니다. 알림과 본문 타입의 짝을 적습니다. 키트는 `using NewMail = Push<PushName::NewMail, XBody>;` 로 씁니다. 다른 서버로 중계하는 알림은 서버 `.cpp` 의 `OnlinePushRelayEntry` 가 이 서술자를 받습니다. */
        template <TPushName Name, typename TBody>
        struct Push
        {
            using PushType = TPushName;
            using Body     = TBody;

            static constexpr TPushName kName  = Name;
            static constexpr uint16    kKind  = static_cast<uint16>( RangeBase + OnlineMethodRange::kMethodCount + static_cast<uint16>( Name ) );
            static constexpr int32     kIndex = static_cast<int32>( Name );
        };
        /** @brief 0부터 시작하는 번호입니다(열거자 순서). 지표와 테이블의 인덱스로 씁니다. 모르는 메서드이면 -1 입니다. */
        static constexpr int32 toIndex( uint16 method )
        {
            const int32 index = static_cast<int32>( method ) - static_cast<int32>( kFirstMethod );
            return 0 <= index && index < kCount ? index : constant::invalid_index::kInt32;
        }

        static_assert( OnlineMethodRange::isInRange( static_cast<uint16>( kFirstMethod + kCount - 1 ), RangeBase ) &&
                           OnlineMethodRange::isMethod( static_cast<uint16>( kFirstMethod + kCount - 1 ) ),
                       "the method table must fit in the method half of its range" );
        static_assert( kPushCount == 0 || ( OnlineMethodRange::isInRange( static_cast<uint16>( kFirstPush + kPushCount - 1 ), RangeBase ) &&
                                            OnlineMethodRange::isMethod( static_cast<uint16>( kFirstPush + kPushCount - 1 ) ) == false ),
                       "the push table must fit in the push half of its range" );
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트 자신의 메서드입니다. */
    struct OnlineHostMethod
    {
        static constexpr uint16 kHello = OnlineMethodRange::kHost + 1; ///< 스키마 해시 비교. 연결의 첫 요청(로그인 없이)
        static constexpr uint16 kPing  = OnlineMethodRange::kHost + 2; ///< 서버 시각(ms). Hello 뒤에 로그인 없이 부른다
    };
} // namespace sw

namespace sw
{
    /** @brief 응답 오류 코드입니다. `NetRequestStatus::ApplicationError` 본문의 첫 2 바이트(리틀 엔디언)이고, 뒤는 자세한 본문입니다. */
    struct OnlineError
    {
        static constexpr uint16 kOk              = 0;
        static constexpr uint16 kUnauthenticated = 1; ///< 로그인이 필요한 메서드
        static constexpr uint16 kForbidden       = 2; ///< 제재 · 권한
        static constexpr uint16 kRateLimited     = 3; ///< 몸: varuint 기다릴 ms
        static constexpr uint16 kInvalidRequest  = 4; ///< Hello 전 요청, 깨진 본문, 상한 초과
        static constexpr uint16 kUnavailable     = 5; ///< 저장소나 캐시 오류. 멱등 키로 다시 한다
        static constexpr uint16 kVersionMismatch = 6; ///< 온라인 스키마 해시가 다르다. 몸: 서버 해시 u64 리틀 엔디언(`kSchemaHashSize`). 클라이언트는 다시 접속하지 않는다
        static constexpr uint16 kUpdateRequired  = 7; ///< 클라이언트 빌드가 최소 판보다 낮다(계정 키트가 낸다)
        static constexpr uint16 kNotFound        = 8;
        static constexpr uint16 kConflict        = 9;
        static constexpr uint16 kFeatureDisabled = 10; ///< 원격 기능 플래그가 껐다
        static constexpr uint16 kBusy            = 11; ///< 저장소 버전 충돌을 재시도 안에 풀지 못했다. 같은 멱등 키로 다시 한다
        static constexpr uint16 kInternal        = 255;

        /** @brief 같은 멱등 키로 다시 시도할 오류(`kRateLimited` · `kUnavailable` · `kBusy`)인지입니다. 서버는 이 오류를 멱등 응답으로 기억하지 않는다(`OnlineServiceHost::respondError`). */
        static constexpr bool isRetryable( uint16 errorCode ) { return errorCode == kRateLimited || errorCode == kUnavailable || errorCode == kBusy; }
    };
} // namespace sw

namespace sw
{
    /** @brief 응답에 키트 결과 칸 `_result` 가 있는지입니다. 결과 칸이 없는 응답(목록)은 답을 받았으면 성공입니다. */
    template <typename TReply, typename = void>
    struct OnlineReplyHasResult : std::false_type
    {
    };

    template <typename TReply>
    struct OnlineReplyHasResult<TReply, std::void_t<decltype( std::declval<const TReply&>()._result )>> : std::true_type
    {
    };

    /** @brief 메서드 하나의 끝난 호출입니다. @p TReply 는 메서드 서술자의 응답 타입입니다. 원격 클라이언트(`OnlineProtocolClient`)와 같은 프로세스의 호출(`EconomyService::submitCall`)이 같은 모양으로 받습니다. */
    template <typename TReply>
    struct OnlineResult
    {
        TReply            _reply{};          ///< `_errorCode` 가 `kOk` 일 때만 서버가 보낸 값이다
        vector<uint8>     _errorDetail{};    ///< 공통 오류의 자세한 몸이다(`kRateLimited` 의 기다릴 ms, 키트 오류 몸). 없으면 비어 있다
        NetIdempotencyKey _idempotencyKey{}; ///< 재시도할 때 그대로 다시 넘기는 키이다. 키 없이 보낸 요청이면 비어 있다
        uint64            _requestID{ 0 };   ///< `call` 이 반환한 번호이다
        uint16            _method{ 0 };      ///< 서술자의 번호(`kID`)이다. 메서드를 실행 때 고르는 도구(GM)가 응답을 가른다
        uint16            _errorCode{ OnlineError::kOk };

        /** @brief 공통 오류 없이 답을 받았고 키트 결과(`_result` 가 있는 응답)가 `Ok` 이면 true 입니다. */
        [[nodiscard]] bool isOk() const
        {
            if ( _errorCode != OnlineError::kOk )
                return false;
            if constexpr ( OnlineReplyHasResult<TReply>::value )
                return _reply._result == decltype( _reply._result )::Ok;
            else
                return true;
        }
        /** @brief 같은 멱등 키로 다시 시도할 결과인지입니다. 범주에서 나오므로 키트마다 판정하지 않습니다. */
        [[nodiscard]] bool isRetryable() const { return OnlineError::isRetryable( _errorCode ); }
        /** @brief 클라이언트를 업데이트해야 하는 결과인지입니다. 다시 시도해도 소용이 없습니다. */
        [[nodiscard]] bool isUpdateRequired() const { return _errorCode == OnlineError::kVersionMismatch || _errorCode == OnlineError::kUpdateRequired; }
    };

    /** @brief 응답에 재시도 판정 `isRetryable()` 이 있는지입니다. 공통 범주가 아니면서 같은 키로 다시 할 업무 결과(스토어 대기)를 가진 키트만 둡니다. */
    template <typename TReply, typename = void>
    struct OnlineReplyHasRetryCheck : std::false_type
    {
    };

    template <typename TReply>
    struct OnlineReplyHasRetryCheck<TReply, std::void_t<decltype( std::declval<const TReply&>().isRetryable() )>> : std::true_type
    {
    };

    /**
     * @brief 서버 저장 함수의 결과입니다. 키트 업무 결과(`TResult`, 끝난 결과) 또는 공통 범주(`OnlineError`, `fail`)입니다.
     * @details 키트 결과 열거형에는 공통 범주(`Busy` · `Unavailable` · 로그인 · 버전)를 두지 않는다 — 공통 범주는 `fail` 로만 나가서 보존 · 지표가 형식에서 나온다
     *          (예전에는 키트마다 공통 범주 변환 표를 두었다). `TResult` 는 `Ok` 를 가지고 암시 변환으로 받으므로 저장 함수는 `return XResult::NotFound;` 로 씁니다.
     */
    template <typename TResult>
    struct OnlineOutcome
    {
        TResult _result;
        uint16  _errorCode;

        OnlineOutcome()
            : _result{ TResult::Ok }
            , _errorCode{ OnlineError::kOk }
        {
        }
        OnlineOutcome( TResult result ) // NOLINT(google-explicit-constructor): 저장 함수가 업무 결과를 그대로 돌려주는 자리다
            : _result{ result }
            , _errorCode{ OnlineError::kOk }
        {
        }

        [[nodiscard]] static OnlineOutcome fail( uint16 errorCode )
        {
            OnlineOutcome outcome;
            outcome._errorCode = errorCode;
            return outcome;
        }

        [[nodiscard]] bool isFailed() const { return _errorCode != OnlineError::kOk; }
        /** @brief 공통 범주 @p errorCode 로 끝났는지입니다. */
        [[nodiscard]] bool hasError( uint16 errorCode ) const { return _errorCode == errorCode; }
        [[nodiscard]] bool operator==( TResult result ) const { return isFailed() == false && _result == result; }
        [[nodiscard]] bool operator!=( TResult result ) const { return ( *this == result ) == false; }
    };
} // namespace sw

namespace sw
{
    struct OnlineProtocolConstant
    {
        static constexpr uint32 kBaseVersion      = 2; ///< 이 테이블, Hello 본문, 알림 프레임 형식의 버전. 온라인 스키마 해시에 섞인다(2: Hello 첫 필드가 스키마 해시). 키트 메시지의 배치는 타입 해시가, 메서드 번호는 메서드 테이블의 이름 순서가 잡으므로, 둘 다 그대로이고 뜻만 바뀐 변경일 때 이 버전을 올린다
        static constexpr int32  kSchemaHashSize   = 8; ///< Hello 첫 필드와 `kVersionMismatch` 자세한 몸의 바이트 수(해시 u64 리틀 엔디언)
        static constexpr int32  kMaxBuildTextSize = 64;
        static constexpr uint16 kDefaultPort      = 7100; ///< 온라인 서비스의 기본 리슨 포트 — 서버 호스트 · 세션(주소에 포트가 없을 때) · 부하 봇이 함께 쓴다
    };
} // namespace sw
