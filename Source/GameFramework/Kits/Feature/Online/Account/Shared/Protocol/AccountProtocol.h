/**
 * @file AccountProtocol.h
 * @brief 계정 키트의 와이어 — 메서드 · 알림 번호(영역 `OnlineMethodRange::kAccount`)와 요청 · 응답 몸 코덱입니다. 서버(`AccountServer`)와 클라이언트(`AccountClient`)가 같이 씁니다.
 * @details - 몸은 `BitStream`. 문자열은 길이 + UTF-8, 토큰 · 표는 고정 바이트.
 *          - 응답 몸의 첫 값은 `LoginResult`(로그인 · 연동 · 탈퇴의 업무 결과는 몸에 — 전송 실패만 공통 오류 코드). 예외: 시도 제한 · 저장소 · 빌드 판은 공통 오류
 *            (`kRateLimited` + 기다릴 ms, `kUnavailable`, `kUpdateRequired` + 상점 주소)로 — 모든 키트가 같은 화면(다시 시도 · 업데이트)으로 보인다.
 *          - 비밀번호 · 장치 비밀 · 세션 비밀은 TLS 안이라 그대로 싣는다. 서버 로그에 몸을 찍지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Protocol/AccountTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 계정 키트 메서드 · 알림 번호입니다. */
    struct AccountMethod
    {
        static constexpr uint16 kRegister        = OnlineMethodRange::kAccount + 0x01; ///< 익명 — 이름 · 비밀번호
        static constexpr uint16 kLogin           = OnlineMethodRange::kAccount + 0x02; ///< 익명 — 이름 · 비밀번호 · 클라이언트 정보
        static constexpr uint16 kGuestLogin      = OnlineMethodRange::kAccount + 0x03; ///< 익명 — 장치 비밀 32 · 정보
        static constexpr uint16 kPlatformLogin   = OnlineMethodRange::kAccount + 0x04; ///< 익명 — 제공자 · 표 · 정보
        static constexpr uint16 kResume          = OnlineMethodRange::kAccount + 0x05; ///< 익명 — 토큰 40 · 정보
        static constexpr uint16 kLogout          = OnlineMethodRange::kAccount + 0x06;
        static constexpr uint16 kLinkCredential  = OnlineMethodRange::kAccount + 0x07; ///< 이름 · 비밀번호
        static constexpr uint16 kLinkPlatform    = OnlineMethodRange::kAccount + 0x08; ///< 제공자 · 표
        static constexpr uint16 kIssueGameTicket = OnlineMethodRange::kAccount + 0x09; ///< 서버 id 글 → 표 64 + 비밀 32 + 시한
        static constexpr uint16 kUnlinkPlatform  = OnlineMethodRange::kAccount + 0x0A; ///< 제공자
        static constexpr uint16 kListLinks       = OnlineMethodRange::kAccount + 0x0B;
        static constexpr uint16 kRequestDeletion = OnlineMethodRange::kAccount + 0x0C;
        static constexpr uint16 kCancelDeletion  = OnlineMethodRange::kAccount + 0x0D;
        static constexpr uint16 kPushRevoked     = OnlineMethodRange::kAccount + 0x80; ///< 알림 — 까닭 · 사유 코드. 보낸 뒤 서버가 연결을 닫는다

        static_assert( OnlineMethodRange::isInRange( kCancelDeletion, OnlineMethodRange::kAccount ) && OnlineMethodRange::isMethod( kCancelDeletion ) );
        static_assert( OnlineMethodRange::isMethod( kPushRevoked ) == false );
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 키트 프로토콜 판입니다(Hello 에 싣는다). 와이어 타입 · 몸 형식을 바꾸면 올린다. */
    struct AccountProtocol
    {
        static constexpr uint32 kVersion = 1;
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 와이어 코덱입니다. 읽기는 형식 · 상한이 틀리면 false 입니다. */
    struct SW_GF_API AccountWire
    {
        static void               writeClientInfo( BitWriter& outWriter, const AccountClientInfo& clientInfo );
        [[nodiscard]] static bool readClientInfo( BitReader& reader, AccountClientInfo& outClientInfo );
        static void               writeCredential( BitWriter& outWriter, string_view loginName, string_view password );
        [[nodiscard]] static bool readCredential( BitReader& reader, string& outLoginName, string& outPassword );
        static void               writeToken( BitWriter& outWriter, const LoginSessionToken& token );
        [[nodiscard]] static bool readToken( BitReader& reader, LoginSessionToken& outToken );
        static void               writeText( BitWriter& outWriter, string_view text );
        [[nodiscard]] static bool readText( BitReader& reader, int32 maxSize, string& outText );
        static void               writeBlob( BitWriter& outWriter, const vector<uint8>& bytes );
        [[nodiscard]] static bool readBlob( BitReader& reader, int32 maxSize, vector<uint8>& outBytes );

        /** @brief 로그인 · 게스트 · 외부 · 재접속 · 연동 · 탈퇴 응답 — 결과 + 발급 결과(실패면 결과와 까닭 칸만 쓸모 있다). */
        static void               writeGrantReply( BitWriter& outWriter, LoginResult result, const LoginGrant& grant );
        [[nodiscard]] static bool readGrantReply( BitReader& reader, LoginResult& outResult, LoginGrant& outGrant );
        static void               writeTicketReply( BitWriter& outWriter, LoginResult result, const NetGameTicket& ticket );
        [[nodiscard]] static bool readTicketReply( BitReader& reader, LoginResult& outResult, NetGameTicket& outTicket );
        static void               writeLinkReply( BitWriter& outWriter, LoginResult result, const AccountLinkSummary& summary );
        [[nodiscard]] static bool readLinkReply( BitReader& reader, LoginResult& outResult, AccountLinkSummary& outSummary );
        static void               writeRevokedPush( BitWriter& outWriter, LoginRevokeReason reason, string_view reasonCode );
        [[nodiscard]] static bool readRevokedPush( BitReader& reader, LoginRevokeReason& outReason, string& outReasonCode );
    };
} // namespace sw
