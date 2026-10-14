/**
 * @file AccountProtocol.h
 * @brief 계정 키트의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kAccount`)와 요청과 응답 본문의 코덱을 정의하며, 서버(`AccountServer`)와 클라이언트(`AccountClient`)가 함께 씁니다.
 * @details - 본문은 `BitStream` 이다. 문자열은 길이 뒤에 UTF-8 을 싣고, 토큰과 티켓은 고정 바이트로 싣는다.
 *          - 응답 본문의 첫 값은 `LoginResult` 이다. 로그인, 연동, 탈퇴의 업무 결과는 본문에 담고, 전송 실패만 공통 오류 코드를 쓴다.
 *            예외는 시도 제한, 저장소 오류, 빌드 버전이다. 이 셋은 공통 오류(`kRateLimited` 와 기다릴 ms, `kUnavailable`, `kUpdateRequired` 와 상점 주소)로 보낸다.
 *            그래야 모든 키트가 같은 화면(다시 시도, 업데이트)을 보여 줄 수 있다.
 *          - 비밀번호, 장치 비밀, 세션 비밀은 TLS 안에서 보내므로 그대로 싣는다. 서버 로그에는 본문을 찍지 않는다.
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

    /** @brief 계정 키트의 메서드와 알림 번호입니다. */
    struct AccountMethod
    {
        static constexpr uint16 kRegister        = OnlineMethodRange::kAccount + 0x01; ///< 익명 — 이름 · 비밀번호
        static constexpr uint16 kLogin           = OnlineMethodRange::kAccount + 0x02; ///< 익명 호출이다. 이름, 비밀번호, 클라이언트 정보를 받는다
        static constexpr uint16 kGuestLogin      = OnlineMethodRange::kAccount + 0x03; ///< 익명 호출이다. 장치 비밀 32바이트와 클라이언트 정보를 받는다
        static constexpr uint16 kPlatformLogin   = OnlineMethodRange::kAccount + 0x04; ///< 익명 호출이다. 제공자, 티켓, 클라이언트 정보를 받는다
        static constexpr uint16 kResume          = OnlineMethodRange::kAccount + 0x05; ///< 익명 호출이다. 토큰 40바이트와 클라이언트 정보를 받는다
        static constexpr uint16 kLogout          = OnlineMethodRange::kAccount + 0x06;
        static constexpr uint16 kLinkCredential  = OnlineMethodRange::kAccount + 0x07; ///< 이름과 비밀번호를 받는다
        static constexpr uint16 kLinkPlatform    = OnlineMethodRange::kAccount + 0x08; ///< 제공자와 티켓을 받는다
        static constexpr uint16 kIssueGameTicket = OnlineMethodRange::kAccount + 0x09; ///< 서버 id 문자열을 받아 티켓 64바이트, 비밀 32바이트, 시한을 돌려준다
        static constexpr uint16 kUnlinkPlatform  = OnlineMethodRange::kAccount + 0x0A; ///< 제공자
        static constexpr uint16 kListLinks       = OnlineMethodRange::kAccount + 0x0B;
        static constexpr uint16 kRequestDeletion = OnlineMethodRange::kAccount + 0x0C;
        static constexpr uint16 kCancelDeletion  = OnlineMethodRange::kAccount + 0x0D;
        static constexpr uint16 kPushRevoked     = OnlineMethodRange::kAccount + 0x80; ///< 알림이다. 이유와 사유 코드를 보내며, 보낸 뒤 서버가 연결을 닫는다

        static_assert( OnlineMethodRange::isInRange( kCancelDeletion, OnlineMethodRange::kAccount ) && OnlineMethodRange::isMethod( kCancelDeletion ) );
        static_assert( OnlineMethodRange::isMethod( kPushRevoked ) == false );
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 키트 프로토콜 버전입니다. Hello 에 싣는 값이며, 와이어 타입이나 본문 형식을 바꾸면 올립니다. */
    struct AccountProtocol
    {
        static constexpr uint32 kVersion = 1;
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 와이어 코덱입니다. 읽기는 형식이나 상한이 틀리면 false 를 반환합니다. */
    struct SW_KIT_API AccountWire
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

        /** @brief 로그인, 게스트 로그인, 외부 로그인, 재접속, 연동, 탈퇴의 응답입니다. 결과와 발급 결과를 담으며, 실패하면 결과와 이유 필드만 의미가 있습니다. */
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
