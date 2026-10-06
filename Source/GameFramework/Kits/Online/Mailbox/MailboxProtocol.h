/**
 * @file MailboxProtocol.h
 * @brief 우편함 서비스의 와이어 — 메서드 번호(영역 `OnlineMethodRange::kMailbox`) · 결과 · 화면 우편 · 요청/응답 형식입니다.
 * @details 응답 몸 = `MailboxResult` + 칸(업무 결과는 몸에 — 경제 · 거래 키트와 같다). 캠페인 우편의 키는 `campaign/<id 16 진>` 이다(개인 우편 키는 받는 계정 16 진으로
 *          시작해 겹치지 않는다). 수령 응답의 잔액은 게임이 경제 클라이언트(`EconomyClient::applyBalances`)로 넘긴다 — 키트끼리는 모른다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 우편함 메서드입니다. 와이어 값 — 바꾸면 `MailboxProtocol::kVersion` 을 올린다. */
    struct MailboxMethod
    {
        static constexpr uint16 kList     = OnlineMethodRange::kMailbox + 0x01;
        static constexpr uint16 kMarkRead = OnlineMethodRange::kMailbox + 0x02;
        static constexpr uint16 kClaim    = OnlineMethodRange::kMailbox + 0x03; ///< 멱등 키 필수
        static constexpr uint16 kClaimAll = OnlineMethodRange::kMailbox + 0x04; ///< 멱등 키 필수
        static constexpr uint16 kDelete   = OnlineMethodRange::kMailbox + 0x05;
        static constexpr int32  kCount    = 5;

        static_assert( OnlineMethodRange::isInRange( kDelete, OnlineMethodRange::kMailbox ) && OnlineMethodRange::isMethod( kDelete ) );

        /** @brief 0 부터의 번호(지표)입니다. 모르는 메서드면 −1 입니다. */
        static constexpr int32 toIndex( uint16 method ) { return kList <= method && method < kList + kCount ? static_cast<int32>( method - kList ) : -1; }
        /** @brief 상태를 바꾸는 수령인가입니다(멱등 키 필수). */
        static constexpr bool isClaim( uint16 method ) { return method == kClaim || method == kClaimAll; }
    };
} // namespace sw

namespace sw
{
    /** @brief 우편함 결과입니다. 와이어 값 — 끝에만 더한다. */
    enum class MailboxResult : uint8
    {
        Ok = 0,
        NotSignedIn,
        InvalidRequest,
        NotFound, ///< 없는 우편 · 남의 우편
        AlreadyClaimed,
        Expired,
        HasAttachments, ///< 받지 않은 첨부가 있어 지울 수 없다
        CapExceeded,    ///< 받으면 상한을 넘는다 — 우편은 남는다
        Busy,
        Unavailable,
        Count
    };

    SW_GF_API const utf8* toString( MailboxResult result );
} // namespace sw

namespace sw
{
    /** @brief 화면에 보이는 우편 하나입니다. */
    struct MailView
    {
        vector<ServiceMailAttachment> _listAttachment{};
        string                        _mailKey{};
        string                        _titleKey{};
        string                        _body{};
        string                        _senderName{};
        int64                         _createdMs{ 0 };
        int64                         _expiresMs{ 0 };
        ServiceMailState              _state{ ServiceMailState::Unread };
        uint8                         _bLiteralText{ SW_FALSE };
        uint8                         _bCampaign{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 요청(쓰는 칸만)입니다. */
    struct MailboxRequest
    {
        string _mailKey{};
        string _cursor{}; ///< List — 앞 응답의 `_nextCursor`(처음은 빈 글)
        int32  _maxCount{ 20 };
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답(쓰는 칸만)입니다. */
    struct MailboxReply
    {
        vector<MailView>      _listMail{};
        vector<LedgerBalance> _listBalance{}; ///< 수령 · 모두 받기 — 바뀐 잔액(게임이 EconomyClient::applyBalances 로)
        string                _nextCursor{};
        int32                 _claimedCount{ 0 };
        MailboxResult         _result{ MailboxResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 형식 쓰기 · 읽기입니다. 읽기는 상한을 넘거나 모자라면 false 입니다. */
    struct SW_GF_API MailboxProtocol
    {
        static constexpr uint32      kVersion        = 1;
        static constexpr int32       kMaxListCount   = 50;
        static constexpr int32       kMaxKeySize     = 256;
        static constexpr const utf8* kCampaignPrefix = "campaign/";

        static void               writeRequest( BitWriter& outWriter, const MailboxRequest& request );
        [[nodiscard]] static bool readRequest( BitReader& reader, MailboxRequest& outRequest );
        static void               writeReply( BitWriter& outWriter, const MailboxReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, MailboxReply& outReply );
        /** @brief 공통 오류 코드(`OnlineError`) → 결과입니다(로그인 없음 · 깨진 몸 · 충돌, 나머지 Unavailable). */
        static MailboxResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
