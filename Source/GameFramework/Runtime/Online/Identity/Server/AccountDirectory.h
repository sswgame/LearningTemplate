/**
 * @file AccountDirectory.h
 * @brief 서버의 계정 포트입니다. 계정 디렉터리 인터페이스(이 프로세스에 접속한 계정)를 둡니다. 값 형식(계정 id · 공개 신원)은 공유 `Identity/AccountIdentity.h` 에 있습니다.
 *        계정을 다루는 다른 포트(차단 목록 `IAccountBlockList`, 세션 끊기 `IAccountSessionControl`, 이름 색인 `IAccountNameIndex`, 외부 로그인 연동 `IAccountExternalLinks`, 오프라인 푸시 `IOfflinePushSender`,
 *        탈퇴 뒤 키트 정리 `IAfterDeleteAccount`)도 여기 둔다.
 * @details 발급은 계정 키트(GF_Account, GF_Server_Account)가 한다. 다른 키트(Trade, Economy, Mailbox, Admin)는 이 인터페이스만 보고 계정 키트를 include 하지 않는다
 *          (키트끼리는 include 하지 않는다). 서버가 여럿이면 다른 서버에 붙은 계정은 캐시 층의 접속 상태(`IEphemeralStore`)로 본다.
 *          세션 토큰 검증은 따로 인터페이스를 두지 않는다. 스트림 연결은 로그인할 때 한 번 확인되고 그 뒤로는 연결의 주체로 다니며, UDP 게임 서버는 계정 키트의 접속 테이블이 확인한다.
 *          언리얼에서 친구, 세션, 업적 인터페이스가 `IOnlineIdentity`(`FUniqueNetId`, 닉네임, 로그인 상태)를 같이 보는 것에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Identity/AccountIdentity.h"

namespace sw
{
    class IServiceStoreConnection;

    enum class ServiceStoreResult : uint8;
} // namespace sw

namespace sw
{
    /**
     * @class IAccountDirectory
     * @brief 계정 디렉터리 인터페이스입니다. 이 프로세스에 접속한 계정만 답합니다(메모리만 보고 저장소를 읽지 않는다). 서비스 스레드에서 부릅니다.
     */
    class IAccountDirectory
    {
    public:
        IAccountDirectory()          = default;
        virtual ~IAccountDirectory() = default;

        IAccountDirectory( const IAccountDirectory& )            = delete;
        IAccountDirectory& operator=( const IAccountDirectory& ) = delete;

        /** @brief 계정 id 의 공개 정보입니다. 이 프로세스에 접속한 계정만 알며, 모르면 false 입니다. */
        [[nodiscard]] virtual bool findIdentity( AccountID accountID, AccountIdentity& outIdentity ) const = 0;
        /** @brief 접속한 계정을 표시 이름(대소문자 무시)으로 찾습니다. 거래 신청과 귓속말은 상대가 접속해 있어야 합니다. */
        [[nodiscard]] virtual bool findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const = 0;
        /** @brief 지금 이 프로세스에 로그인해 붙어 있는가입니다(재접속 유예 중이면 false). */
        virtual bool isAccountOnline( AccountID accountID ) const = 0;
    };
} // namespace sw

namespace sw
{
    // 계정끼리의 차단 목록을 묻는 포트 — 친구 키트(GF_Server_Social)가 구현하고, 채팅처럼 차단을 지켜야 하는 키트가 서버 조립에서 받는다(키트끼리는 include 하지 못한다).
    /**
     * @class IAccountBlockList
     * @brief 서비스 스레드에서 부릅니다. 이 서버에 접속한 계정의 목록만 메모리에서 답합니다(받는 쪽 서버가 판단한다).
     */
    class IAccountBlockList
    {
        // 순수 인터페이스라 내보내지 않는다. 인라인 생성자와 소멸자를 GameFramework.dll 이 내보내지 않으므로 dllimport 로 해석되면 키트 링크가 깨진다(C-3).
    public:
        IAccountBlockList()          = default;
        virtual ~IAccountBlockList() = default;

        IAccountBlockList( const IAccountBlockList& )            = delete;
        IAccountBlockList& operator=( const IAccountBlockList& ) = delete;

        /** @brief @p ownerID 가 @p otherID 를 막았으면 true 입니다. @p ownerID 가 이 서버에 없으면 false 입니다. */
        virtual bool isBlocked( AccountID ownerID, AccountID otherID ) const = 0;
    };
} // namespace sw

namespace sw
{
    // 계정의 접속 세션을 끊는 포트 — 계정 키트(GF_Server_Account)가 구현하고, 게임(서버 조립)이 GM 키트와 제재를 적용하는 쪽에 넘긴다(키트끼리는 include 하지 못한다).
    /**
     * @class IAccountSessionControl
     * @brief 서비스 스레드에서 부릅니다. 다른 서버 프로세스의 세션은 서버 버스 알림(또는 그쪽의 다음 세션 확인)으로 끊긴다.
     */
    class IAccountSessionControl
    {
        // 순수 인터페이스라 내보내지 않는다. 인라인 생성자와 소멸자를 GameFramework.dll 이 내보내지 않으므로 dllimport 로 해석되면 키트 링크가 깨진다(C-3).
    public:
        IAccountSessionControl()          = default;
        virtual ~IAccountSessionControl() = default;

        IAccountSessionControl( const IAccountSessionControl& )            = delete;
        IAccountSessionControl& operator=( const IAccountSessionControl& ) = delete;

        /** @brief 그 계정의 세션을 모두 끝내고 붙어 있는 클라이언트에 @p reasonCode(로컬라이제이션 키)를 알립니다. */
        virtual void revokeAccountSessions( AccountID accountID, string_view reasonCode, UnixMilliseconds nowMs ) = 0;
    };
} // namespace sw

namespace sw
{
    // 계정 이름 색인 포트 — 접속해 있지 않은 계정도 저장소에서 표시 이름과 계정 id 로 찾는다(친구 신청, 우편 받는 사람, 귓속말 대상).
    // - 계정 키트(서버)가 구현한다(`AccountNameIndex`, 계정 테이블의 형식은 그쪽만 안다). 다른 키트는 이 인터페이스만 보고 계정 키트를 include 하지 않는다.
    // - **저장소 스레드에서 부른다**: 부르는 키트의 저장소 작업(`IServiceStoreJob::run`) 안에서 그 연결로 읽는다. 이름으로 찾고 같은 작업에서 관계를 쓰면
    //   왕복이 하나다. 구현은 상태가 없어(연결만 쓴다) 어느 스레드에서 불러도 된다.
    // - 이름으로 찾을 수 있는 것은 정식 계정(로그인 이름이 곧 고유한 표시 이름)뿐이다. 게스트와 외부 계정의 생성된 이름(`Guest-…`)은 고유하지 않아 색인에 없으므로 계정 id 로 찾는다.
    // - 접속해 있는 계정은 `IAccountDirectory`(이 프로세스)와 `IAccountPresence`(서버 여럿)가 저장소를 읽지 않고 답한다. 이 인터페이스는 오프라인까지 볼 때 쓴다.
    // 언리얼 `IOnlineUser::QueryUserIdMapping`(표시 이름에서 사용자 id), PlayFab `GetAccountInfo(TitleDisplayName)` 에 해당한다.
    // 결과 형식(`ServiceStoreResult`)과 연결은 전방 선언만 둔다 — 부르는 쪽과 구현은 `Store/ServiceStore.h` 를 include 한다.
    /**
     * @class IAccountNameIndex
     * @brief 계정 이름 색인 인터페이스입니다(저장소 스레드).
     */
    class IAccountNameIndex
    {
        // 순수 인터페이스라 내보내지 않는다. 인라인 생성자와 소멸자를 GameFramework.dll 이 내보내지 않으므로 dllimport 로 해석되면 키트 링크가 깨진다(C-3).
    public:
        IAccountNameIndex()          = default;
        virtual ~IAccountNameIndex() = default;

        IAccountNameIndex( const IAccountNameIndex& )            = delete;
        IAccountNameIndex& operator=( const IAccountNameIndex& ) = delete;

        /** @brief 정식 계정을 표시 이름(대소문자 무시)으로 찾습니다. 없거나 규칙 밖 이름이면 NotFound, 저장소가 아프면 Unavailable. */
        [[nodiscard]] virtual ServiceStoreResult readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName,
                                                                            AccountIdentity& outIdentity ) const = 0;
        /** @brief 계정 id 의 공개 신원을 읽습니다(게스트 · 외부 계정 포함). 없으면 NotFound. */
        [[nodiscard]] virtual ServiceStoreResult readIdentity( IServiceStoreConnection& connection, AccountID accountID, AccountIdentity& outIdentity ) const = 0;
    };
} // namespace sw

namespace sw
{
    // 외부 로그인 연동 포트 — 계정 키트(서버)가 구현하고, 스토어 신원과 계정을 이어야 하는 키트(경제의 영수증 구매자 대조)가 서버 조립 문맥에서 받는다. 연동 테이블의 형식은 계정 키트만 안다.
    // **저장소 스레드에서 부른다**: `IAccountNameIndex` 와 같이 부르는 쪽의 저장소 작업 안에서 그 연결로 읽고, 구현은 상태가 없다.
    // 언리얼 `IOnlineIdentity`(플랫폼 사용자 id)를 게임 서버가 대조하는 자리다. 주체는 다이제스트만 저장되므로 "이 주체인가" 로만 묻는다 — 저장된 값을 꺼내 줄 수 없다.
    /**
     * @class IAccountExternalLinks
     * @brief 외부 로그인 연동 인터페이스입니다(저장소 스레드).
     */
    class IAccountExternalLinks
    {
        // 순수 인터페이스라 내보내지 않는다. 인라인 생성자와 소멸자를 GameFramework.dll 이 내보내지 않으므로 dllimport 로 해석되면 키트 링크가 깨진다(C-3).
    public:
        IAccountExternalLinks()          = default;
        virtual ~IAccountExternalLinks() = default;

        IAccountExternalLinks( const IAccountExternalLinks& )            = delete;
        IAccountExternalLinks& operator=( const IAccountExternalLinks& ) = delete;

        /** @brief @p accountID 가 제공자 @p provider 의 주체 @p subject 에 연동돼 있으면 @p outbLinked 가 true 입니다. 연동이 없거나 다른 주체면 false 이고 결과는 Ok 입니다. 저장소가 아프면 Unavailable. */
        [[nodiscard]] virtual ServiceStoreResult readIsLinked( IServiceStoreConnection& connection, AccountID accountID, string_view provider, string_view subject, bool& outbLinked ) const = 0;
    };
} // namespace sw

namespace sw
{
    // 접속해 있지 않은 계정에게 기기 푸시를 보내는 포트 — 라이브 운영 키트가 푸시 발송기로 구현해 서버 조립 문맥(`OnlineServerKitContext::_kits`)에 내놓고,
    // 우편함 키트(새 우편)처럼 "오프라인이면 푸시" 를 정하는 키트가 부른다. 키트끼리 include 하지 않도록 글은 로컬라이제이션 키로만 넘긴다.
    // 접속해 있는지는 부르는 쪽이 정한다(접속한 계정에게는 게임 알림이 먼저다). 서비스 스레드 하나에서만 쓴다.
    /**
     * @class IOfflinePushSender
     * @brief 오프라인 계정의 기기 푸시 창입니다.
     */
    class IOfflinePushSender
    {
    public:
        IOfflinePushSender()          = default;
        virtual ~IOfflinePushSender() = default;

        IOfflinePushSender( const IOfflinePushSender& )            = delete;
        IOfflinePushSender& operator=( const IOfflinePushSender& ) = delete;

        /** @brief @p accountID 의 모든 기기에 보냅니다. 계정마다 도배 제한에 걸리면 보내지 않고 false 입니다. @p deepLink 는 앱이 열 화면(예: "mailbox")입니다. */
        virtual bool sendOfflinePush( AccountID accountID, string_view titleKey, string_view bodyKey, string_view deepLink, UnixMilliseconds nowMs ) = 0;
    };
} // namespace sw

namespace sw
{
    // 탈퇴 정리가 계정 레코드를 지운 뒤 키트마다 부르는 훅 — 키트가 `IOnlineServerKit::getAfterDeleteAccount` 로 내놓고, 계정 키트의 탈퇴 정리가 재시도 대기열
    // (`account_after_delete`)로 모든 키트를 부른다. Nakama 런타임의 `RegisterAfterDeleteAccount` 훅, PlayFab DeletePlayer 가 타이틀 데이터 · 통계 · 친구를 함께 지우는 자리다.
    /**
     * @class IAfterDeleteAccount
     * @brief 저장소 스레드(`IServiceStoreJob::run`)에서 부릅니다. 키트가 소유하며 바뀌지 않는 상태만 읽어야 합니다(여러 저장소 일이 동시에 부를 수 있다).
     */
    class IAfterDeleteAccount
    {
    public:
        IAfterDeleteAccount()          = default;
        virtual ~IAfterDeleteAccount() = default;

        IAfterDeleteAccount( const IAfterDeleteAccount& )            = delete;
        IAfterDeleteAccount& operator=( const IAfterDeleteAccount& ) = delete;

        /**
         * @brief 지운 계정 @p accountID 를 키나 값으로 둔 이 키트의 레코드를 지우거나 익명화합니다.
         * @details 멱등이어야 한다 — 다 끝낼 때까지 다음 회차가 다시 부른다. 트랜잭션 상한 안에서 스스로 나눠 커밋한다.
         * @return 남은 것이 없으면 Ok, 충돌로 다 못 했으면 Conflict, 저장소 문제면 Unavailable 입니다. Ok 가 아니면 대기열에 남습니다.
         */
        [[nodiscard]] virtual ServiceStoreResult afterDeleteAccount( IServiceStoreConnection& connection, AccountID accountID, int64 nowMs ) const = 0;
    };
} // namespace sw
