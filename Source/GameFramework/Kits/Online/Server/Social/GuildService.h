/**
 * @file GuildService.h
 * @brief 길드 로직 — 만들기 · 초대 · 수락 · 떠나기 · 내보내기 · 역할 · 공지 · 조회. 모든 바꾸기는 읽기 → 판 조건 트랜잭션(충돌이면 4 번까지 다시). 전송 · 채팅을 모릅니다.
 * @details - 표 여섯: 길드 `social_guild` · 회원 `social_guild_member`(`<길드>/<계정>`) · 계정의 길드 `social_account_guild` · 이름 `social_guild_name`(ASCII 소문자로
 *            바꾼 바이트의 16 진 — 대소문자 무시 유일) · 초대 `social_guild_invite`(`<계정>/<길드>`, 7 일) · 순번 `social_sequence/guild`(만들기와 같은 트랜잭션).
 *          - 한 계정 한 길드(계정의 길드를 "없어야 함" 으로), 정원 100, 길드장은 혼자일 때만 떠나고 그때 해산(이름도 지운다), 내보내기는 높은 역할만.
 *          - 바뀐 뒤 회원 목록을 한 번 더 읽어 `GuildChanged` 알림을 보낸다(받는 이가 오프라인이면 호스트가 버린다).
 *          - 채팅 길드 채널은 사건(`GuildEvent` — 가입 · 떠남 · 로그인한 회원)으로 게임 조립이 잇는다(`setEventDelegate` — 키트끼리 모른다). 로그인한 계정의 길드는
 *            조립이 로그인 사건에서 `attachAccount` 를 불러 `MemberAttached` 로 받는다.
 *          PlayFab Groups · Nakama Groups(역할 · 초대 · 정원)와 같은 자리다. 길드 창고 · 레벨 · 가입 신청은 게임 몫(백로그).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Social/SocialTypes.h"

namespace sw
{
    class IServiceStore;

    /** @brief 길드 요청 종류입니다. */
    enum class GuildOperation : uint8
    {
        Create = 0,
        Invite,
        Accept,
        Leave,
        Kick,
        SetRole,
        SetNotice,
        Get,
        Attach ///< 로그인한 계정의 길드 찾기(완료 없음 — 사건만)
    };
} // namespace sw

namespace sw
{
    /** @brief 게임 조립이 받는 사건 — 채팅 길드 채널 회원을 맞춘다. */
    struct GuildEvent
    {
        enum class Kind : uint8
        {
            Joined = 0,
            Left,
            MemberAttached
        };

        AccountId _accountId{ kInvalidAccountId };
        uint64    _guildId{ 0 };
        Kind      _kind{ Kind::Joined };
    };

    using GuildEventDelegate = Delegate<void( const GuildEvent& )>;
} // namespace sw

namespace sw
{
    /** @brief 길드 요청 하나입니다. */
    struct GuildRequest
    {
        string         _text{};                         ///< 이름 · 공지
        AccountId      _accountId{ kInvalidAccountId }; ///< 요청한 이
        AccountId      _targetId{ kInvalidAccountId };  ///< 초대 · 내보내기 · 역할 대상
        uint64         _guildId{ 0 };                   ///< 수락
        int64          _nowMs{ 0 };
        GuildRole      _role{ GuildRole::Member };
        GuildOperation _operation{ GuildOperation::Get };
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다. */
    struct GuildCompletion
    {
        GuildInfo    _info{}; ///< Create(id · 이름) · Get(회원까지)
        uint64       _requestTag{ 0 };
        SocialResult _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 일 하나가 정한 결과입니다 — 서비스가 완료 · 알림 · 사건으로 나눈다. */
    struct GuildOutcome
    {
        GuildInfo         _info{};
        vector<AccountId> _listNotifyMember{}; ///< GuildChanged 를 받을 회원
        AccountId         _invitedId{ kInvalidAccountId };
        AccountId         _joinedId{ kInvalidAccountId };
        AccountId         _leftId{ kInvalidAccountId };
        AccountId         _attachedId{ kInvalidAccountId };
        uint64            _guildId{ 0 };
        SocialResult      _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GuildService
     * @brief 길드 로직입니다(서비스 스레드).
     */
    class SW_GF_API GuildService
    {
    public:
        GuildService();

        void initialize( IServiceStore* pStore );
        void shutdown();

        void submit( const GuildRequest& request, uint64 requestTag );
        /** @brief 로그인한 계정의 길드를 찾아 `MemberAttached` 사건을 냅니다(완료 없음). */
        void attachAccount( AccountId accountId, int64 nowMs );
        /** @brief 사건을 받을 곳입니다(게임 조립 — 비어 있으면 버린다). */
        void setEventDelegate( const GuildEventDelegate& onEvent ) { _onEvent = onEvent; }

        void  drainCompletions( vector<GuildCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void  drainNotifications( vector<SocialNotification>& outListNotification ) { _notificationBuffer.drainTo( outListNotification ); }
        int32 getPendingCount() const { return _pendingCount; }

        /** @brief 이름 규칙 — 1..24 바이트, 올바른 UTF-8, 제어 글자 없음, 앞뒤 공백 없음. */
        static bool isValidName( string_view name );

        /** @brief 일의 `complete` 가 부릅니다(키트 안). */
        void applyOutcome( const GuildRequest& request, uint64 requestTag, GuildOutcome&& outcome );

    private:
        void raiseEvent( AccountId accountId, uint64 guildId, GuildEvent::Kind kind );

        EventBuffer<GuildCompletion>    _completionBuffer;
        EventBuffer<SocialNotification> _notificationBuffer;
        GuildEventDelegate              _onEvent;
        IServiceStore*                  _pStore;
        int32                           _pendingCount;
    };
} // namespace sw
