/**
 * @file FriendListModel.h
 * @brief 친구 목록 화면이 읽는 친구 목록 상태입니다. `SocialClient::listLinks` 와 `queryFriendPresence` 의 응답과 접속 상태 알림을 받아 친구 줄(계정, 접속 상태, 활동)과 받은 신청 수를 보관합니다(온라인 키트 U1).
 * @details 화면(`FriendListScreen`)은 요청을 직접 보내지 않고 이 객체를 읽는다. 목록은 두 요청(관계 목록 → 접속 상태)이 모두 끝나야 `Loaded` 이고, 어느 쪽이든 실패하면 `Failed` 로 끝나며 이전 목록을 남기지 않는다.
 *          `update` 가 클라이언트의 알림을 꺼내 쓴다 — 접속 상태 변화는 줄에 바로 반영하고, 친구가 늘거나 줄거나 신청이 오면 목록을 다시 묻는다(이미 한 번 받은 뒤에만).
 *          알림 버퍼를 꺼내는 쪽이 하나여야 하므로 한 클라이언트에 모델 하나만 붙을 수 있다(`initialize` 가 false). 접속 상태가 `Offline` 이 아닌 친구가 앞에 오고 같은 쪽끼리는 계정 번호 순이다.
 *          이 객체는 클라이언트와 함께(또는 먼저 요청이 끝난 뒤) 소멸해야 한다. 목록이 바뀔 때마다 `getRevision` 이 올라가 화면이 다시 그릴지 판단한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialClient.h"

namespace sw
{
    /** @brief 목록을 받는 단계입니다. */
    enum class FriendListState : uint8
    {
        Idle = 0, ///< 아직 묻지 않았다
        Loading,
        Loaded,
        Failed ///< 오류는 `getErrorCode` · `getResult`
    };

    /** @brief 친구 한 줄입니다. */
    struct FriendEntry
    {
        string               _activity{};
        AccountID            _accountID{ kInvalidAccountID };
        SocialPresenceStatus _status{ SocialPresenceStatus::Offline };
    };
} // namespace sw

namespace sw
{
    /**
     * @class FriendListModel
     * @brief 친구 목록 한 벌과 그 상태입니다.
     */
    class SW_KIT_API FriendListModel
    {
    public:
        FriendListModel();
        FriendListModel( const FriendListModel& )            = delete; // 응답 델리게이트가 this 를 가리킨다
        FriendListModel& operator=( const FriendListModel& ) = delete;

        /**
         * @brief 클라이언트를 빌려 쓰고 그 알림 버퍼를 꺼낼 권한을 받습니다(`SocialClient` 가 서비스 클라이언트에 등록 · 초기화된 것).
         * @details 이미 다른 모델이 이 클라이언트의 버퍼를 꺼내고 있으면 false 이고 클라이언트를 붙이지 않습니다(두 모델이 알림을 서로 가져가는 것을 막는다). nullptr 이면 떼어냅니다.
         */
        [[nodiscard]] bool initialize( SocialClient* pClient );
        /** @brief 목록을 묻습니다. 이미 묻는 중이면 보내지 않고 false 입니다(응답이 겹쳐 낡은 것이 새것을 덮지 않게 한다). 클라이언트가 없으면 `Failed` 로 끝나고 false 입니다. */
        [[nodiscard]] bool refresh();
        /** @brief 클라이언트의 알림을 꺼내 반영합니다. 화면이 틱마다 부릅니다. */
        void update();

        // 클라이언트의 응답과 알림이 들어오는 길입니다. 시험도 같은 길로 값을 넣습니다.
        void onLinksReply( const SocialClient::Reply& reply );
        /** @brief 관계 목록 응답에서 친구 줄(접속 상태는 아직 `Offline`)과 받은 신청 수를 만듭니다. 상태는 바꾸지 않는다 — 접속 상태 응답이 와야 `Loaded` 다. */
        void applyLinks( const SocialReply& reply );
        void onPresenceReply( const SocialClient::Reply& reply );
        void applyNotification( const SocialNotification& notification );

        FriendListState            getState() const { return _state; }
        uint16                     getErrorCode() const { return _errorCode; }
        SocialResult               getResult() const { return _result; }
        uint64                     getRevision() const { return _revision; }
        uint32                     getPendingRequestCount() const { return _pendingRequestCount; }
        const vector<FriendEntry>& getEntries() const { return _listEntry; }

    private:
        void fail( uint16 errorCode, SocialResult result );
        void sortEntries();

        vector<FriendEntry>        _listEntry;
        vector<SocialNotification> _listDrained;
        OnlineDrainLease           _drainLease;
        SocialClient*              _pClient;
        uint64                     _revision;
        uint32                     _pendingRequestCount;
        uint16                     _errorCode;
        SocialResult               _result;
        FriendListState            _state;
        bool                       _bStale; ///< 목록을 다시 물어야 한다(친구가 바뀌었다는 알림을 받았다)
    };
} // namespace sw
