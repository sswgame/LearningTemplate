/**
 * @file OnlineProtocol.h
 * @brief 온라인 서비스의 와이어 표 — 키트마다의 메서드 영역 · 호스트 메서드 · 공통 오류 코드 · 기반 프로토콜 판입니다.
 * @details - 메서드(uint16) = 영역 + 0x00..0x7F, 알림 종류(Message 프레임 `[종류 u16][몸]`) = 영역 + 0x80..0xFF. 키트 오류 = 영역 + n(공통 0..255 는 `OnlineError`).
 *          - 값은 와이어 형식이다 — 바꾸면 `OnlineProtocolConstant::kBaseVersion` 을 올린다. 키트의 메서드 상수는 `static_assert( OnlineMethodRange::isInRange( … ) )`.
 *          - 영역이 겹치는 키트 둘을 한 호스트에 올리면 `OnlineServiceHost::registerService` 가 거절한다(조립 실수를 기동에서 잡는다).
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
        static constexpr uint16 kHost            = 0x0000; ///< 판 협상 · 핑(호스트 자신)
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
    /** @brief 호스트 자신의 메서드입니다. */
    struct OnlineHostMethod
    {
        static constexpr uint16 kHello = OnlineMethodRange::kHost + 1; ///< 판 협상 — 연결의 첫 요청(로그인 없이)
        static constexpr uint16 kPing  = OnlineMethodRange::kHost + 2; ///< 서버 시각(ms) — Hello 뒤, 로그인 없이
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
        static constexpr uint16 kVersionMismatch = 6; ///< 프로토콜 판이 안 맞는다 — 몸: 서버의 (영역, 판) 목록
        static constexpr uint16 kUpdateRequired  = 7; ///< 클라이언트 빌드가 최소 판보다 낮다(계정 키트가 낸다)
        static constexpr uint16 kNotFound        = 8;
        static constexpr uint16 kConflict        = 9;
        static constexpr uint16 kFeatureDisabled = 10; ///< 원격 기능 플래그가 껐다
        static constexpr uint16 kInternal        = 255;
    };
} // namespace sw

namespace sw
{
    struct OnlineProtocolConstant
    {
        static constexpr uint32 kBaseVersion      = 1;  ///< 이 표 · Hello 몸 · 알림 프레임 형식의 판
        static constexpr int32  kMaxKitCount      = 64; ///< Hello 하나에 실을 키트 판 상한
        static constexpr int32  kMaxBuildTextSize = 64;
    };
} // namespace sw
