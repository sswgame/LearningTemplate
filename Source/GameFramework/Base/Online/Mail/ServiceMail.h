/**
 * @file ServiceMail.h
 * @brief 우편 넣기 — 우편 레코드 · 보낸 기록(멱등) · 만료 색인 · (재원이 발행이 아니면) 첨부 맡김 이동을 부르는 쪽 트랜잭션에 붙입니다. 레코드 형식도 여기 있습니다.
 * @details 표 셋: `mail_item`(키 `<받는 계정 16 진>/<만든 시각 16 진>.<멱등 키 해시 16 진>`) · `mail_sent`(키 `<받는 계정 16 진>/<멱등 키>` → 우편 키) ·
 *          `mail_expiry`(키 `<만료 16 진>/<우편 키>`, 값 없음). 우편 키의 시각이 앞이라 받는 계정의 우편은 키 순서 = 만든 순서다.
 *          저장소 스레드의 동기 함수다. 읽기 · 수령 · 만료 처리는 우편함 키트(GF_Mailbox)가 한다.
 *          거래 실패 반환 · GM 지급 · 보상이 자기 레코드와 한 트랜잭션에서 우편을 만들어야 해서 키트가 아니라 기반에 둔다(키트끼리는 include 하지 못한다).
 *          국내 MMO 우편함과 같은 모양 — 보낸 순간 보낸 쪽에서 빠지고(맡김), 운영 우편은 재원 없는 발행이라 수령 때 움직인다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 첨부 하나입니다. */
    struct ServiceMailAttachment
    {
        string _assetID{};
        int64  _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 만료 때 첨부를 어떻게 하는가입니다. 저장 값이라 순서를 바꾸지 않는다. */
    enum class ServiceMailExpiryAction : uint8
    {
        Discard = 0,    ///< 버린다(운영 · 보상 우편) — 발행이 낸 것은 아무것도 움직이지 않고, 맡긴 것은 맡김 → 소각
        ReturnToSender, ///< 맡김 → 보낸 계정(플레이어 우편 — 재원이 계정일 때만)
        Count
    };

    /** @brief 우편 상태입니다. 저장 값이라 순서를 바꾸지 않는다. */
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
    /** @brief 우편 상한입니다. */
    struct ServiceMailConstant
    {
        static constexpr int32 kMaxAttachmentCount    = 8;
        static constexpr int32 kMaxTitleSize          = 128;
        static constexpr int32 kMaxBodySize           = 2048;
        static constexpr int32 kMaxSenderNameSize     = 64;
        static constexpr int32 kMaxIdempotencyKeySize = 64;                         ///< `[0-9a-z_.-]`
        static constexpr int64 kDefaultRetentionMs    = 30ll * 24 * 60 * 60 * 1000; ///< 기본 보관 30 일
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
        string                        _idempotencyKey{}; ///< 필수 — 보내는 쪽이 정한다("trade.<id>.return" · "admin.<GM>.<요청>" · "reward.<퀘스트>.<계정>")
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
    /** @brief 우편 넣기와 레코드 형식입니다(저장소 스레드). */
    struct SW_GF_API ServiceMail
    {
        static const hashed_string& getMailTable();
        static const hashed_string& getSentTable();
        static const hashed_string& getExpiryTable();

        /**
         * @brief 우편 하나를 @p inoutTransaction 에 붙입니다 — 우편 · 보낸 기록 · (만료가 있으면) 만료 색인 · (재원이 발행이 아니면) 재원 → 우편 맡김 이동.
         * @return `Ok`(같은 멱등 키의 우편이 이미 있으면 붙이지 않고 @p outbReplayed), `Invalid`(규칙 · 쓰기 상한), `InsufficientFunds`(재원이 모자람), `Unavailable`.
         */
        [[nodiscard]] static LedgerResult stageSend( IServiceStoreConnection& connection, const ServiceMailMessage& message, ServiceTransaction& inoutTransaction,
                                                     string& outMailKey, bool& outbReplayed );

        /** @brief 재원에 맞는 기본 만료 처리입니다 — 계정이 낸 첨부(플레이어 우편)는 반환, 그 밖(운영 · 보상 · 거래 반환)은 버림. */
        static ServiceMailExpiryAction getDefaultExpiryAction( const LedgerHolder& fundingHolder );
        /** @brief 우편의 맡김 보유자 `esc/mail/<토큰>`(토큰 = 우편 키의 `/` 를 `.` 로)입니다. */
        static LedgerHolder makeEscrowHolder( string_view mailKey );
        static string       makeExpiryKey( int64 expiresMs, string_view mailKey );
        /** @brief 우편 키 앞의 받는 계정입니다. 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool parseRecipient( string_view mailKey, uint64& outAccountID );

        static vector<uint8> encodeRecord( const ServiceMailRecord& record );
        /** @brief 레코드 바이트를 읽습니다(@p outRecord 의 `_mailKey` · `_version` 은 부르는 쪽이 채운다). */
        [[nodiscard]] static bool decodeRecord( const vector<uint8>& bytes, ServiceMailRecord& outRecord );
        /** @brief 보낼 수 있는 우편인가입니다(글자 · 개수 · 길이 · 재원 규칙). */
        static bool isValidMessage( const ServiceMailMessage& message );
    };
} // namespace sw
