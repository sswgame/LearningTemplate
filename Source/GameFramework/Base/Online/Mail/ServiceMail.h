/**
 * @file ServiceMail.h
 * @brief 우편 넣기와 우편 레코드 형식을 정의합니다.
 * @details 우편 레코드, 보낸 기록(멱등), 만료 색인, 첨부 맡김 이동(재원이 발행이 아닐 때)을 부르는 쪽 트랜잭션에 붙인다.
 *          표는 셋이다.
 *          `mail_item` 의 키는 `<받는 계정 16 진>/<만든 시각 16 진>.<멱등 키 해시 16 진>` 이다.
 *          `mail_sent` 의 키는 `<받는 계정 16 진>/<멱등 키>` 이고 값은 우편 키이다.
 *          `mail_expiry` 의 키는 `<만료 16 진>/<우편 키>` 이고 값은 없다.
 *          `mail_notice` 의 키는 `<만든 시각 16 진>/<우편 키>` 이고 값은 제목 키다. 새 우편 알림을 보낼 차례이며, 우편함 키트가 가져가며 지운다(`claimNotices`).
 *          우편을 넣는 모든 경로가 이 함수 하나를 지나므로 알림을 빠뜨리는 경로가 없다. 우편 하나는 쓰기 넷(우편, 보낸 기록, 만료, 알림)이다.
 *          우편 키는 시각이 앞이라서 받는 계정의 우편은 키 순서가 만든 순서와 같다.
 *          저장소 스레드의 동기 함수이다. 읽기, 수령, 만료 처리는 우편함 키트(GF_Mailbox)가 한다.
 *          거래 실패 반환, GM 지급, 보상이 자기 레코드와 한 트랜잭션에서 우편을 만들어야 하므로 키트가 아니라 기반에 둔다. 키트끼리는 include 하지 못한다.
 *          국내 MMO 우편함과 같은 모양이다. 보낸 순간 보낸 쪽에서 빠지고(맡김), 운영 우편은 재원 없는 발행이라 수령 때 움직인다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 첨부 하나입니다. 온라인 메시지(관리자 요청, 우편함 응답)에 고정 배치형으로 실립니다. `MaxCount` 는 `LedgerConstant::kMaxAssetIDSize` 와 같은 값입니다. */
    REFLECT()
    struct ServiceMailAttachment
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = LedgerConstant::kMaxAssetIDSize )
        string _assetID{};
        PROPERTY()
        int64 _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 만료 때 첨부를 어떻게 할지 정합니다. 저장 값이라 순서를 바꾸면 안 됩니다. */
    enum class ServiceMailExpiryAction : uint8
    {
        Discard = 0,    ///< 버린다(운영, 보상 우편). 발행한 것은 아무것도 움직이지 않고, 에스크로에 맡긴 것은 소각으로 보낸다
        ReturnToSender, ///< 에스크로에서 보낸 계정으로 돌려준다(플레이어 우편이며 재원이 계정일 때만)
        Count
    };

    /** @brief 우편 상태입니다. 저장 값이라 순서를 바꾸면 안 됩니다. */
    ENUM( Count = Count )
    enum class ServiceMailState : uint8
    {
        Unread = 0,
        Read,
        Claimed, ///< 첨부를 받았다(첨부가 없는 우편은 읽음과 같다)
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 우편의 상한 값들입니다. */
    struct ServiceMailConstant
    {
        static constexpr int32 kMaxAttachmentCount    = 8;
        static constexpr int32 kMaxTitleSize          = 128;
        static constexpr int32 kMaxBodySize           = 2048;
        static constexpr int32 kMaxSenderNameSize     = 64;
        static constexpr int32 kMaxIdempotencyKeySize = 64;                                                               ///< `[0-9a-z_.-]`
        static constexpr int64 kDefaultRetentionMs    = 30 * constant::kSecondsPerDay * constant::kMillisecondsPerSecond; ///< 기본 보관 30 일
    };
} // namespace sw

namespace sw
{
    /** @brief 보낼 우편 하나입니다. */
    struct ServiceMailMessage
    {
        vector<ServiceMailAttachment> _listAttachment{};
        string                        _titleKey{}; ///< 로컬라이제이션 키(`_bLiteralText` 면 GM 이 쓴 글)
        string                        _body{};
        string                        _senderName{};
        string                        _idempotencyKey{}; ///< 필수. 보내는 쪽이 정한다("trade.<id>.return", "admin.<GM>.<요청>", "reward.<퀘스트>.<계정>")
        LedgerHolder                  _fundingHolder{};  ///< 기본 발행
        uint64                        _recipientAccountID{ 0 };
        uint64                        _actorID{ 0 };
        int64                         _createdMs{ 0 };
        int64                         _expiresMs{ 0 }; ///< 0 = 만료 없음
        LedgerActorKind               _actorKind{ LedgerActorKind::System };
        ServiceMailExpiryAction       _expiryAction{ ServiceMailExpiryAction::Discard };
        uint8                         _bLiteralText{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 저장된 우편 하나입니다. */
    struct ServiceMailRecord
    {
        ServiceMailMessage _message{};
        string             _mailKey{};
        uint64             _version{ 0 };
        ServiceMailState   _state{ ServiceMailState::Unread };
    };
} // namespace sw

namespace sw
{
    /** @brief 보낼 차례인 새 우편 알림 하나입니다. */
    struct ServiceMailNotice
    {
        string _mailKey{};
        string _titleKey{};
        uint64 _recipientAccountID{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 우편 넣기와 레코드 형식 함수 모음입니다. 저장소 스레드에서 부릅니다. */
    struct SW_GF_API ServiceMail
    {
        static const hashed_string& getMailTable();
        static const hashed_string& getSentTable();
        static const hashed_string& getExpiryTable();
        static const hashed_string& getNoticeTable(); ///< "mail_notice"

        /**
         * @brief 우편 하나를 @p inoutTransaction 에 붙입니다. 우편, 보낸 기록, 만료 색인(만료가 있을 때), 재원에서 우편으로 가는 맡김 이동(재원이 발행이 아닐 때)을 붙입니다.
         * @return `Ok` 이면 성공이며, 같은 멱등 키의 우편이 이미 있으면 붙이지 않고 @p outbReplayed 를 켠다.
         *         `Invalid` 는 규칙이나 쓰기 상한을 어겼다는 뜻이고, `InsufficientFunds` 는 재원이 모자란다는 뜻이다. 저장소를 못 읽으면 `Unavailable` 이다.
         */
        [[nodiscard]] static LedgerResult stageSend( IServiceStoreConnection& connection, const ServiceMailMessage& message, ServiceTransaction& inoutTransaction,
                                                     string& outMailKey, bool& outbReplayed );

        /**
         * @brief 오래된 새 우편 알림을 @p maxCount 개까지 가져가며 지웁니다. 지우기를 한 트랜잭션으로 커밋한 쪽만 알림을 받으므로 서버가 여럿이어도 한 번만 보냅니다.
         * @return 그사이 다른 서버가 가져갔으면 Conflict 이고 @p outListNotice 는 비어 있습니다. 손상된 알림은 건너뛰지 않고 Invalid 입니다.
         */
        [[nodiscard]] static ServiceStoreResult claimNotices( IServiceStoreConnection& connection, int32 maxCount, vector<ServiceMailNotice>& outListNotice );
        /** @brief 재원에 맞는 기본 만료 처리를 반환합니다. 계정이 낸 첨부(플레이어 우편)는 반환하고, 그 밖(운영, 보상, 거래 반환)은 버립니다. */
        static ServiceMailExpiryAction getDefaultExpiryAction( const LedgerHolder& fundingHolder );
        /** @brief 우편의 맡김 보유자 `esc/mail/<토큰>` 을 반환합니다. 토큰은 우편 키의 `/` 를 `.` 로 바꾼 값입니다. */
        static LedgerHolder makeEscrowHolder( string_view mailKey );
        static string       makeExpiryKey( int64 expiresMs, string_view mailKey );
        /** @brief 우편 키 앞의 받는 계정을 구합니다. 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool parseRecipient( string_view mailKey, uint64& outAccountID );

        static vector<uint8> encodeRecord( const ServiceMailRecord& record );
        /** @brief 레코드 바이트를 읽습니다. @p outRecord 의 `_mailKey` 와 `_version` 은 부르는 쪽이 채웁니다. */
        [[nodiscard]] static bool decodeRecord( const vector<uint8>& bytes, ServiceMailRecord& outRecord );
        /** @brief 보낼 수 있는 우편이면 true 입니다. 글자, 개수, 길이, 재원 규칙을 봅니다. */
        static bool isValidMessage( const ServiceMailMessage& message );
    };
} // namespace sw
