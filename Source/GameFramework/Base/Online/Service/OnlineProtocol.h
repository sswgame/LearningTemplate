/**
 * @file OnlineProtocol.h
 * @brief 온라인 서비스의 와이어 표 — 키트마다의 메서드 영역 · 호스트 메서드 · 공통 오류 코드 · 기반 프로토콜 판입니다.
 * @details - 메서드(uint16) = 영역 + 0x00..0x7F, 알림 종류(Message 프레임 `[종류 u16][몸]`) = 영역 + 0x80..0xFF. 키트 오류 = 영역 + n(공통 0..255 는 `OnlineError`).
 *          - 값은 와이어 형식이다 — 바꾸면 `OnlineProtocolConstant::kBaseVersion` 을 올린다. 키트의 메서드 상수는 `static_assert( OnlineMethodRange::isInRange( … ) )`.
 *          - 영역이 겹치는 키트 둘을 한 호스트에 올리면 `OnlineServiceHost::registerHostService` 가 거절한다(조립 실수를 기동에서 잡는다).
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct OnlineMethodRange
     * @brief 온라인 기능 키트마다의 요청 메서드 번호 첫 값입니다(키트마다 256 칸).
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
        static constexpr uint16 kGame            = 0x8000; ///< 게임 몫(0x8000..0xFFFF, 영역 128 개)

        static constexpr bool   isInRange( uint16 value, uint16 rangeBase ) { return rangeBase <= value && value < rangeBase + kSize; }
        static constexpr uint16 getRangeBase( uint16 value ) { return static_cast<uint16>( value & 0xFF00u ); }
        /** @brief 영역 안의 메서드 칸(0x00..0x7F)인가입니다 — 아니면 알림 종류다. */
        static constexpr bool isMethod( uint16 value ) { return ( value & 0x00FFu ) < kMethodCount; }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 키트 요청 메서드의 표입니다. @p TMethodName 은 `ENUM( Count = Count )` 열거형이고, 열거자 순서가 와이어 번호(@p RangeBase + 1 부터 연속)입니다.
     * @details 키트의 `XxxMethod` 가 이 표를 상속해 와이어 상수를 `toMethod` 로 만들고, 서비스 지표(`ServiceMetrics::initialize`)는 열거자 이름의 snake_case 를 메서드 라벨로 씁니다.
     *          새 메서드는 열거형 끝(`Count` 앞)에만 더합니다 — 순서가 와이어 값입니다.
     */
    template <typename TMethodName, uint16 RangeBase>
    struct OnlineMethodTable
    {
        using MethodName = TMethodName;

        static constexpr int32  kCount       = static_cast<int32>( TMethodName::Count );
        static constexpr uint16 kFirstMethod = static_cast<uint16>( RangeBase + 1 );

        static constexpr uint16 toMethod( TMethodName name ) { return static_cast<uint16>( kFirstMethod + static_cast<uint16>( name ) ); }
        /** @brief 0부터 시작하는 번호입니다(열거자 순서). 지표와 표의 인덱스로 씁니다. 모르는 메서드이면 -1 입니다. */
        static constexpr int32 toIndex( uint16 method )
        {
            const int32 index = static_cast<int32>( method ) - static_cast<int32>( kFirstMethod );
            return 0 <= index && index < kCount ? index : -1;
        }

        static_assert( OnlineMethodRange::isInRange( static_cast<uint16>( kFirstMethod + kCount - 1 ), RangeBase ) &&
                           OnlineMethodRange::isMethod( static_cast<uint16>( kFirstMethod + kCount - 1 ) ),
                       "the method table must fit in the method half of its range" );
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트 자신의 메서드입니다. */
    struct OnlineHostMethod
    {
        static constexpr uint16 kHello = OnlineMethodRange::kHost + 1; ///< 스키마 해시 비교. 연결의 첫 요청(로그인 없이)
        static constexpr uint16 kPing  = OnlineMethodRange::kHost + 2; ///< 서버 시각(ms) — Hello 뒤, 로그인 없이
    };
} // namespace sw

namespace sw
{
    /** @brief 공통 오류 코드 하나를 키트 결과 하나로 바꾸는 표의 한 줄입니다(`OnlineError::toResult`). */
    template <typename TResult>
    struct OnlineErrorResult
    {
        uint16  _errorCode;
        TResult _result;
    };
} // namespace sw

namespace sw
{
    /** @brief 응답 오류 코드 — `NetRequestStatus::ApplicationError` 몸의 첫 2 바이트(리틀 엔디언), 뒤는 자세한 몸입니다. */
    struct OnlineError
    {
        static constexpr uint16 kOk              = 0;
        static constexpr uint16 kUnauthenticated = 1; ///< 로그인이 필요한 메서드
        static constexpr uint16 kForbidden       = 2; ///< 제재 · 권한
        static constexpr uint16 kRateLimited     = 3; ///< 몸: varuint 기다릴 ms
        static constexpr uint16 kInvalidRequest  = 4; ///< Hello 전 요청 · 몸이 깨졌다 · 상한
        static constexpr uint16 kUnavailable     = 5; ///< 저장소 · 캐시 — 다시(멱등 키로)
        static constexpr uint16 kVersionMismatch = 6; ///< 온라인 스키마 해시가 다르다. 몸: 서버 해시 u64 리틀 엔디언(`kSchemaHashSize`). 클라이언트는 다시 접속하지 않는다
        static constexpr uint16 kUpdateRequired  = 7; ///< 클라이언트 빌드가 최소 판보다 낮다(계정 키트가 낸다)
        static constexpr uint16 kNotFound        = 8;
        static constexpr uint16 kConflict        = 9;
        static constexpr uint16 kFeatureDisabled = 10; ///< 원격 기능 플래그가 껐다
        static constexpr uint16 kInternal        = 255;

        /**
         * @brief @p errorCode 를 키트 표 @p arrRow 에서 찾아 키트 결과로 바꿉니다. 표에 없으면 @p fallback 입니다.
         * @details 결과 열거형과 표, 기본값(대개 `Unavailable`)은 키트가 가진다 — 키트마다 받는 코드가 다르다.
         */
        template <typename TResult, size_t Count>
        static constexpr TResult toResult( uint16 errorCode, const OnlineErrorResult<TResult> ( &arrRow )[Count], TResult fallback )
        {
            for ( const OnlineErrorResult<TResult>& row : arrRow )
            {
                if ( row._errorCode == errorCode )
                    return row._result;
            }
            return fallback;
        }
    };
} // namespace sw

namespace sw
{
    struct OnlineProtocolConstant
    {
        static constexpr uint32 kBaseVersion      = 2; ///< 이 표 · Hello 몸 · 알림 프레임 형식의 판. 온라인 스키마 해시에 섞인다(2: Hello 첫 필드가 스키마 해시)
        static constexpr int32  kSchemaHashSize   = 8; ///< Hello 첫 필드와 `kVersionMismatch` 자세한 몸의 바이트 수(해시 u64 리틀 엔디언)
        static constexpr int32  kMaxBuildTextSize = 64;
    };
} // namespace sw
