/**
 * @file GuildService.h
 * @brief 길드 로직입니다. 만들기, 초대, 수락, 떠나기, 내보내기, 역할, 공지, 조회를 처리합니다. 모든 변경은 읽은 뒤 버전 조건으로 쓰는 트랜잭션이며 충돌이 나면 최대 4번 다시 시도합니다. 전송과 채팅은 모릅니다.
 * @details - 테이블은 여섯 개이다.
 *            길드는 `social_guild`, 멤버는 `social_guild_member`(`<길드>/<계정>`), 계정의 길드는 `social_account_guild` 에 둔다.
 *            이름은 `social_guild_name` 에 ASCII 소문자로 바꾼 바이트의 16진 문자열로 두므로 대소문자를 무시하고 유일하다.
 *            초대는 `social_guild_invite`(`<계정>/<길드>`, 7일)에, 순번은 `social_sequence/guild` 에 두며 순번은 만들기와 같은 트랜잭션에서 올린다.
 *          - 한 계정은 한 길드에만 들어간다. 계정의 길드를 "없어야 함" 조건으로 쓰기 때문이다. 정원은 100명이다.
 *            길드장은 혼자일 때만 떠날 수 있고 그때 길드가 해산되며 이름도 지운다. 내보내기는 더 높은 역할만 할 수 있다.
 *          - 변경한 뒤 멤버 목록을 한 번 더 읽어 `GuildChanged` 알림을 보낸다. 받는 사람이 오프라인이면 호스트가 버린다.
 *          - 채팅 길드 채널은 이벤트(`GuildEvent`)로 게임 조립 단계가 연결한다(`setEventDelegate`). 이벤트는 가입, 떠남, 로그인한 멤버이며 키트끼리는 서로 모른다.
 *            로그인한 계정의 길드는 조립 단계가 로그인 이벤트에서 `attachAccount` 를 불러 `MemberAttached` 로 받는다.
 *
 *          PlayFab Groups 와 Nakama Groups(역할, 초대, 정원)에 해당한다. 길드 창고, 레벨, 가입 신청은 게임이 구현한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialTypes.h"

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
        Attach ///< 로그인한 계정의 길드를 찾는다. 완료는 없고 이벤트만 발생한다
    };
} // namespace sw

namespace sw
{
    /** @brief 게임 조립이 받는 이벤트입니다. 채팅 길드 채널의 멤버를 맞추는 데 씁니다. */
    struct GuildEvent
    {
        enum class Kind : uint8
        {
            Joined = 0,
            Left,
            MemberAttached
        };

        AccountID _accountID{ kInvalidAccountID };
        uint64    _guildID{ 0 };
        Kind      _kind{ Kind::Joined };
    };

    using GuildEventDelegate = Delegate<void( const GuildEvent& )>;
} // namespace sw

namespace sw
{
    /** @brief 길드 요청 하나입니다. */
    struct GuildRequest
    {
        string         _text{};                         ///< 이름이나 공지이다
        AccountID      _accountID{ kInvalidAccountID }; ///< 요청한 사람이다
        AccountID      _targetID{ kInvalidAccountID };  ///< 초대, 내보내기, 역할 변경의 대상이다
        uint64         _guildID{ 0 };                   ///< 수락할 길드이다
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
        GuildInfo    _info{}; ///< Create 에서는 id 와 이름이고 Get 에서는 멤버까지 담는다
        uint64       _requestTag{ 0 };
        SocialResult _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 작업 하나가 정한 결과입니다. 서비스가 완료, 알림, 이벤트로 나눕니다. */
    struct GuildOutcome
    {
        GuildInfo         _info{};
        vector<AccountID> _listNotifyMember{}; ///< GuildChanged 를 받을 멤버이다
        AccountID         _invitedID{ kInvalidAccountID };
        AccountID         _joinedID{ kInvalidAccountID };
        AccountID         _leftID{ kInvalidAccountID };
        AccountID         _attachedID{ kInvalidAccountID };
        uint64            _guildID{ 0 };
        SocialResult      _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GuildService
     * @brief 길드 로직입니다. 서비스 스레드에서 씁니다.
     */
    class SW_GF_API GuildService
    {
    public:
        GuildService();

        void initialize( IServiceStore* pStore );
        void shutdown();

        void submit( const GuildRequest& request, uint64 requestTag );
        /** @brief 로그인한 계정의 길드를 찾아 `MemberAttached` 이벤트를 냅니다. 완료는 없습니다. */
        void attachAccount( AccountID accountID, int64 nowMs );
        /** @brief 이벤트를 받을 델리게이트입니다. 게임 조립 단계가 지정하며 비어 있으면 이벤트를 버립니다. */
        void setEventDelegate( const GuildEventDelegate& onEvent ) { _onEvent = onEvent; }

        void  drainCompletions( vector<GuildCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void  drainNotifications( vector<SocialNotification>& outListNotification ) { _notificationBuffer.drainTo( outListNotification ); }
        int32 getPendingCount() const { return _pendingCount; }

        /** @brief 이름 규칙을 검사합니다. 1~24바이트이고, 올바른 UTF-8 이며, 제어 문자가 없고, 앞뒤 공백이 없어야 합니다. */
        static bool isValidName( string_view name );

        /** @brief 작업의 `complete` 가 호출합니다. 키트 안에서만 씁니다. */
        void applyOutcome( const GuildRequest& request, uint64 requestTag, GuildOutcome&& outcome );

    private:
        void raiseEvent( AccountID accountID, uint64 guildID, GuildEvent::Kind kind );

        EventBuffer<GuildCompletion>    _completionBuffer;
        EventBuffer<SocialNotification> _notificationBuffer;
        GuildEventDelegate              _onEvent;
        IServiceStore*                  _pStore;
        int32                           _pendingCount;
    };
} // namespace sw
