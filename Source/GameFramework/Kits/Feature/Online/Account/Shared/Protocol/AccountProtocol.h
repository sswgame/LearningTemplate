/**
 * @file AccountProtocol.h
 * @brief 계정 키트의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kAccount`)와 요청과 응답 본문을 정의하며, 서버(`AccountServer`)와 클라이언트(`AccountClient`)가 함께 씁니다.
 * @details - 요청, 응답, 알림, 오류 상세, 서버 버스 본문은 `REFLECT` 구조체 하나씩이고 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *          - 응답 본문의 결과는 `LoginResult` 이다. 로그인, 연동, 탈퇴의 업무 결과는 본문에 담고, 전송 실패만 공통 오류 코드를 쓴다.
 *            예외는 시도 제한, 저장소 오류, 빌드 버전이다. 이 셋은 공통 오류(`kRateLimited` 와 기다릴 ms, `kUnavailable`, `kUpdateRequired` 와 상점 주소)로 보낸다.
 *            그래야 모든 키트가 같은 화면(다시 시도, 업데이트)을 보여 줄 수 있다.
 *          - 비밀번호, 장치 비밀, 세션 비밀은 TLS 안에서 보내므로 그대로 싣는다. 서버 로그에는 본문을 찍지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Protocol/AccountTypes.h"

namespace sw
{
    /** @brief 계정 키트 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class AccountMethodName : uint8
    {
        Register,
        Login,
        GuestLogin,
        PlatformLogin,
        Resume,
        Logout,
        LinkCredential,
        LinkPlatform,
        IssueGameTicket,
        UnlinkPlatform,
        ListLinks,
        RequestDeletion,
        CancelDeletion,
        Count,
    };

    /** @brief 계정 키트 알림 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class AccountPushName : uint8
    {
        Revoked,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 키트의 메서드와 알림 번호입니다. 번호는 열거자 순서에서 나오며 그 이름 순서가 온라인 스키마 해시에 섞입니다(`OnlineMethodTable`). */
    struct AccountMethod : OnlineMethodTable<AccountMethodName, OnlineMethodRange::kAccount, AccountPushName>
    {
        static constexpr uint16 kRegister        = toMethod( AccountMethodName::Register );      ///< 익명 — 이름 · 비밀번호
        static constexpr uint16 kLogin           = toMethod( AccountMethodName::Login );         ///< 익명 호출이다. 이름, 비밀번호, 클라이언트 정보를 받는다
        static constexpr uint16 kGuestLogin      = toMethod( AccountMethodName::GuestLogin );    ///< 익명 호출이다. 장치 비밀 32바이트와 클라이언트 정보를 받는다
        static constexpr uint16 kPlatformLogin   = toMethod( AccountMethodName::PlatformLogin ); ///< 익명 호출이다. 제공자, 티켓, 클라이언트 정보를 받는다
        static constexpr uint16 kResume          = toMethod( AccountMethodName::Resume );        ///< 익명 호출이다. 토큰과 클라이언트 정보를 받는다
        static constexpr uint16 kLogout          = toMethod( AccountMethodName::Logout );
        static constexpr uint16 kLinkCredential  = toMethod( AccountMethodName::LinkCredential );  ///< 이름과 비밀번호를 받는다
        static constexpr uint16 kLinkPlatform    = toMethod( AccountMethodName::LinkPlatform );    ///< 제공자와 티켓을 받는다
        static constexpr uint16 kIssueGameTicket = toMethod( AccountMethodName::IssueGameTicket ); ///< 서버 id 문자열을 받아 티켓 64바이트, 비밀 32바이트, 시한을 돌려준다
        static constexpr uint16 kUnlinkPlatform  = toMethod( AccountMethodName::UnlinkPlatform );  ///< 제공자
        static constexpr uint16 kListLinks       = toMethod( AccountMethodName::ListLinks );
        static constexpr uint16 kRequestDeletion = toMethod( AccountMethodName::RequestDeletion );
        static constexpr uint16 kCancelDeletion  = toMethod( AccountMethodName::CancelDeletion );
        static constexpr uint16 kPushRevoked     = toPush( AccountPushName::Revoked ); ///< 알림이다. 이유와 사유 코드를 보내며, 보낸 뒤 서버가 연결을 닫는다
    };
} // namespace sw

namespace sw
{
    /** @brief 가입(`kRegister`)과 이름 연동(`kLinkCredential`)의 요청입니다. 이름 상한은 `LoginConstant::kMaxLoginNameSize` 의 네 배(UTF-8 바이트), 비밀번호 상한은 `kMaxPasswordSize` 와 같습니다. */
    REFLECT()
    struct AccountCredentialRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 64 )
        string _loginName{};
        PROPERTY( MaxCount = 128 )
        string _password{};
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인(`kLogin`)의 요청입니다. */
    REFLECT()
    struct AccountLoginRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 64 )
        string _loginName{};
        PROPERTY( MaxCount = 128 )
        string _password{};
        PROPERTY()
        AccountClientInfo _clientInfo{};
    };
} // namespace sw

namespace sw
{
    /** @brief 게스트 로그인(`kGuestLogin`)의 요청입니다. */
    REFLECT()
    struct AccountGuestLoginRequest
    {
        REFLECT_BODY();
        PROPERTY( Min = 0, Max = 255 )
        uint8 _arrDeviceSecret[LoginConstant::kDeviceSecretSize]{};
        PROPERTY()
        AccountClientInfo _clientInfo{};
    };
} // namespace sw

namespace sw
{
    /** @brief 외부 로그인(`kPlatformLogin`)과 외부 연동(`kLinkPlatform`)의 요청입니다. 연동은 클라이언트 정보를 비웁니다. 티켓 상한은 `LoginConstant::kMaxPlatformTicketSize` 와 같습니다. */
    REFLECT()
    struct AccountPlatformRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 16 )
        string _provider{};
        PROPERTY( MaxCount = 8192, Min = 0, Max = 255 )
        vector<uint8> _ticketBytes{};
        PROPERTY()
        AccountClientInfo _clientInfo{};
    };
} // namespace sw

namespace sw
{
    /** @brief 재접속(`kResume`)의 요청입니다. */
    REFLECT()
    struct AccountResumeRequest
    {
        REFLECT_BODY();
        PROPERTY()
        LoginSessionToken _token{};
        PROPERTY()
        AccountClientInfo _clientInfo{};
    };
} // namespace sw

namespace sw
{
    /** @brief 게임 접속 티켓 발급(`kIssueGameTicket`)의 요청입니다. 서버 id 는 비어 있으면 안 됩니다. */
    REFLECT()
    struct AccountGameTicketRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 32 )
        string _serverID{};
    };
} // namespace sw

namespace sw
{
    /** @brief 외부 연동 해제(`kUnlinkPlatform`)의 요청입니다. */
    REFLECT()
    struct AccountProviderRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 16 )
        string _provider{};
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인, 게스트 로그인, 외부 로그인, 재접속, 연동, 탈퇴의 응답입니다. 결과와 발급 결과를 담으며, 실패하면 결과와 이유 필드만 의미가 있습니다. */
    REFLECT()
    struct AccountGrantReply
    {
        REFLECT_BODY();
        PROPERTY()
        LoginGrant _grant{};
        PROPERTY()
        LoginResult _result{ LoginResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 게임 접속 티켓 발급의 응답입니다. */
    REFLECT()
    struct AccountTicketReply
    {
        REFLECT_BODY();
        PROPERTY()
        NetGameTicket _ticket{};
        PROPERTY()
        LoginResult _result{ LoginResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인 수단 목록(`kListLinks`)과 연동 해제의 응답입니다. */
    REFLECT()
    struct AccountLinkReply
    {
        REFLECT_BODY();
        PROPERTY()
        AccountLinkSummary _summary{};
        PROPERTY()
        LoginResult _result{ LoginResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 세션 종료 알림(`kPushRevoked`)의 본문입니다. 사유 코드 상한은 `LoginConstant::kMaxReasonCodeSize` 와 같습니다. */
    REFLECT()
    struct AccountRevokedPush
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 64 )
        string _reasonCode{};
        PROPERTY()
        LoginRevokeReason _reason{ LoginRevokeReason::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 공통 오류 `kUpdateRequired` 의 상세 본문입니다. 원격 설정의 상점 주소이며 비어 있을 수 있습니다. */
    REFLECT()
    struct AccountUpdateDetail
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 512 )
        string _storeURL{};
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 버스(`AccountBus::kRevokeTopic`)의 본문입니다. 다른 서버에서 끊긴 세션을 알립니다. 버스에만 쓰므로 `MessageTypes` 에 없습니다(A). */
    REFLECT()
    struct AccountRevokeNotice
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 의미 버전(docs/plans/ServerBusFormat.md 3.4)
        PROPERTY( MaxCount = 64 )
        string _reasonCode{};
        PROPERTY()
        AccountID _accountID{ kInvalidAccountID };
        PROPERTY()
        uint64 _sessionID{ 0 };
        PROPERTY()
        LoginRevokeReason _reason{ LoginRevokeReason::None };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AccountProtocol
     * @brief 계정 키트의 메시지 타입 목록과 등록 항목입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     */
    struct SW_KIT_API AccountProtocol
    {
        using MessageTypes = OnlineMessageTypes<AccountCredentialRequest, AccountLoginRequest, AccountGuestLoginRequest, AccountPlatformRequest, AccountResumeRequest,
                                                AccountGameTicketRequest, AccountProviderRequest, AccountGrantReply, AccountTicketReply, AccountLinkReply,
                                                AccountRevokedPush, AccountUpdateDetail>;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
    };
} // namespace sw
