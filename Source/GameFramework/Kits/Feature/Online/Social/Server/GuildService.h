/**
 * @file GuildService.h
 * @brief 길드 로직입니다. 만들기, 초대, 수락, 떠나기, 내보내기, 역할, 공지, 조회를 처리합니다. 모든 변경은 읽은 뒤 버전 조건으로 쓰는 트랜잭션이며 충돌이 나면 최대 4번 다시 시도합니다. 전송과 채팅은 모릅니다.
 * @details - 테이블은 여섯 개이다.
 *            길드는 `social_guild`, 멤버는 `social_guild_member`(`<길드>/<계정>`), 계정의 길드는 `social_account_guild` 에 둔다.
 *            이름은 `social_guild_name` 에 ASCII 소문자로 바꾼 바이트의 16진 문자열로 두므로 대소문자를 무시하고 유일하다.
 *            초대는 `social_guild_invite`(`<계정>/<길드>`, 7일)에, 순번은 `social_sequence/guild` 에 두며 순번은 만들기와 같은 트랜잭션에서 늘린다.
 *          - 한 계정은 한 길드에만 들어간다. 계정의 길드를 "없어야 함" 조건으로 쓰기 때문이다. 정원은 100명이다.
 *            길드장은 혼자일 때만 떠날 수 있고 그때 길드가 해산되며 이름도 지운다. 내보내기는 더 높은 역할만 할 수 있다.
 *          - 변경한 뒤 멤버 목록을 한 번 더 읽어 `GuildChanged` 알림을 보낸다. 받는 사람이 오프라인이면 호스트가 버린다.
 *          - 채팅 길드 채널은 이벤트(`GuildEvent`)로 친구 키트 서버 조립이 연결한다(`bindEventDelegate` → 기반 `IGroupChannelMembership`). 이벤트는 가입, 떠남, 로그인한 멤버이며 키트끼리는 서로 모른다.
 *            로그인한 계정의 길드는 바인딩(`SocialServer::onAccountJoined`)이 다음 틱에 `attachAccount` 를 불러 `MemberAttached` 로 받는다.
 *
 *          - 길드 창고는 원장의 맡김(`esc/guild/<길드 16진>`)이다. 맡기기는 멤버 누구나, 꺼내기는 임원 이상이 하며 둘 다 클라이언트 멱등 키로 만든 분개 키가 필수이다.
 *            이동은 멤버 레코드의 버전 조건과 한 트랜잭션에 들어가므로 그사이 내보내지거나 역할이 바뀌면 충돌로 다시 읽는다.
 *            맡기기는 길드 레코드를 다시 써서 해산과 겹치지 않게 하며, 창고에 남은 것이 있으면 해산(혼자 남은 길드장의 떠나기)은 `StorageNotEmpty` 이다.
 *
 *          PlayFab Groups 와 Nakama Groups(역할, 초대, 정원)에 해당한다.
 *          - 레벨은 경험치(`addExperience`, 서버 시스템만)와 서버 설정의 레벨 테이블로 응답할 때 정한다. 테이블이 바뀌면 저장된 데이터를 고치지 않아도 레벨이 따라간다.
 *          - 공개 길드(`_bOpenToApply`)에는 누구나 가입 신청을 내고 임원 이상이 받아들이거나 지운다. 신청은 `social_guild_application`(키 `<길드>/<계정>`)이며 7일 뒤 죽는다.
 *            길드마다 살아 있는 신청은 50개까지이고, 새 신청을 받을 때 죽은 신청을 같은 트랜잭션에서 지운다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/DelegateSlot.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialTypes.h"

namespace sw
{
    class ILedgerPolicy;
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
        Attach,           ///< 로그인한 계정의 길드를 찾는다. 완료는 없고 이벤트만 발생한다
        Deposit,          ///< 내 계정에서 길드 창고로 옮긴다
        Withdraw,         ///< 길드 창고에서 내 계정으로 옮긴다. 임원 이상
        ListStorage,      ///< 길드 창고의 잔액 목록이다
        SetJoinPolicy,    ///< 공개 길드를 켜고 끈다. 임원 이상
        Apply,            ///< 공개 길드에 가입 신청을 낸다. 길드가 없는 계정만
        ListApplications, ///< 살아 있는 신청 목록이다. 임원 이상
        Approve,          ///< 신청을 받아들인다. 임원 이상
        Reject,           ///< 신청을 지운다. 임원 이상
        AddExperience     ///< 서버 시스템이 길드 경험치를 늘린다(`addExperience`). 완료는 없다
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
        int64     _nowMs{ 0 }; ///< 이벤트를 낸 요청의 시각이다
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
        string         _assetID{};                      ///< Deposit · Withdraw 의 자산이다
        string         _journalKey{};                   ///< Deposit · Withdraw 의 분개 키이다. `LedgerJournalKey::makeFromIdempotency( makeAccountScope( 계정 ), 멱등 키 )`
        AccountID      _accountID{ kInvalidAccountID }; ///< 요청한 사람이다
        AccountID      _targetID{ kInvalidAccountID };  ///< 초대, 내보내기, 역할 변경의 대상이다
        uint64         _guildID{ 0 };                   ///< 수락할 길드이다
        int64          _amount{ 0 };                    ///< Deposit · Withdraw 의 양, AddExperience 의 경험치이다
        int64          _nowMs{ 0 };
        GuildRole      _role{ GuildRole::Member };
        GuildOperation _operation{ GuildOperation::Get };
        uint8          _bOpen{ SW_FALSE }; ///< SetJoinPolicy 의 값이다
    };
} // namespace sw

namespace sw
{
    /** @brief 요청 하나의 완료입니다. */
    struct GuildCompletion
    {
        GuildInfo                _info{};            ///< Create 에서는 id 와 이름이고 Get 에서는 멤버까지 담는다
        vector<GuildStorageItem> _listStorage{};     ///< ListStorage 는 창고 전체, Deposit · Withdraw 는 옮긴 자산의 창고 잔액 하나이다
        vector<GuildApplication> _listApplication{}; ///< ListApplications 의 결과이다
        uint64                   _requestTag{ 0 };
        SocialResult             _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 작업 하나가 정한 결과입니다. 서비스가 완료, 알림, 이벤트로 나눕니다. */
    struct GuildOutcome
    {
        GuildInfo                _info{};
        vector<GuildStorageItem> _listStorage{};
        vector<GuildApplication> _listApplication{};
        vector<AccountID>        _listNotifyMember{}; ///< GuildChanged 를 받을 멤버이다
        AccountID                _invitedID{ kInvalidAccountID };
        AccountID                _joinedID{ kInvalidAccountID };
        AccountID                _leftID{ kInvalidAccountID };
        AccountID                _attachedID{ kInvalidAccountID };
        uint64                   _guildID{ 0 };
        SocialResult             _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GuildService
     * @brief 길드 로직입니다. 서비스 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API GuildService
    {
    public:
        GuildService();

        void initialize( IServiceStore* pStore );
        void shutdown();

        void submit( const GuildRequest& request, uint64 requestTag );
        /** @brief 로그인한 계정의 길드를 찾아 `MemberAttached` 이벤트를 냅니다. 완료는 없습니다. */
        void attachAccount( AccountID accountID, int64 nowMs );
        /** @brief 서버 시스템(퀘스트, 전투)이 길드 경험치를 @p amount(1 이상)만큼 늘립니다. 완료는 없고 멤버에게 GuildChanged 를 알립니다. */
        void addExperience( uint64 guildID, int64 amount, int64 nowMs );
        /**
         * @brief 레벨 테이블을 정합니다. @p listLevelExperience 의 i 번째 값은 레벨 i + 2 가 되는 누적 경험치이며 1 이상이고 늘어나야 합니다.
         * @return 규칙에 맞지 않으면 false 이고 테이블을 바꾸지 않습니다. 비어 있으면 모든 길드가 레벨 1 입니다.
         */
        [[nodiscard]] bool setLevelTable( const vector<int64>& listLevelExperience );
        /** @brief 경험치에 맞는 레벨입니다(1 부터). */
        int32 computeLevel( int64 experience ) const;
        /** @brief 이벤트를 받을 델리게이트입니다. 게임 조립 단계가 지정하며 비어 있으면 이벤트를 버립니다. */
        /** @brief 길드 이벤트를 받을 함수를 겁니다. 칸 주인은 하나다(이미 걸려 있으면 단언) — 구독이 풀리면 빈다. */
        [[nodiscard]] Subscription bindEventDelegate( GuildEventDelegate onEvent ) { return _onEvent.bind( std::move( onEvent ) ); }
        /** @brief 꺼내기가 계정 잔액 상한을 볼 정책입니다(화폐 카탈로그). 빌려 쓰며 nullptr 이면 원장 상한(`kMaxBalance`)만 봅니다. */
        void                 setLedgerPolicy( const ILedgerPolicy* pPolicy ) { _pLedgerPolicy = pPolicy; }
        const ILedgerPolicy* getLedgerPolicy() const { return _pLedgerPolicy; }

        void  drainCompletions( vector<GuildCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void  drainNotifications( vector<SocialNotification>& outListNotification ) { _notificationBuffer.drainTo( outListNotification ); }
        int32 getPendingCount() const { return _pendingCount; }

        /** @brief 이름 규칙을 검사합니다. 1~24바이트이고, 올바른 UTF-8 이며, 제어 문자가 없고, 앞뒤 공백이 없어야 합니다. */
        static bool isValidName( string_view name );

        /** @brief 작업의 `complete` 가 호출합니다. 키트 안에서만 씁니다. */
        void applyOutcome( const GuildRequest& request, uint64 requestTag, GuildOutcome&& outcome );

    private:
        void raiseEvent( AccountID accountID, uint64 guildID, GuildEvent::Kind kind, int64 nowMs );

        EventBuffer<GuildCompletion>     _completionBuffer;
        EventBuffer<SocialNotification>  _notificationBuffer;
        DelegateSlot<GuildEventDelegate> _onEvent;
        vector<int64>                    _listLevelExperience; ///< 레벨 2 부터의 누적 경험치이다. 데이터(서버 설정)가 원본이다
        IServiceStore*                   _pStore;
        const ILedgerPolicy*             _pLedgerPolicy;
        int32                            _pendingCount;
    };
} // namespace sw
