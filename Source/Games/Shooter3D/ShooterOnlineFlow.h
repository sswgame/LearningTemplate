/**
 * @file ShooterOnlineFlow.h
 * @brief Shooter3D 의 온라인 흐름입니다. 세션(`OnlineSession`)으로 로그인 → 우편 목록 → 모두 받기 → 지갑 맞추기를 잇는 게임 쪽 코드이자, 게임이 온라인 키트를 어떻게 쓰는지 보여 주는 견본입니다.
 * @details 게임이 쓰는 것은 세션 하나와 키트 클라이언트의 `call<>` 뿐입니다. 연결 · 틱 · 키트 묶기는 세션이 맡습니다.
 *          우편 수령 응답의 잔액을 경제 클라이언트로 넘기는 일은 지금 게임 몫입니다(`EconomyClient::applyBalances`).
 *          화면은 아직 없고 개발 명령(`shooter.online.*`)과 감시 값(`Shooter3D.Online*`)으로 시나리오가 끝단을 판정합니다(`automation/onlinesession.scenario.xml`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountClient.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"
#include "GameFramework/Runtime/Online/Store/ServiceStoreTracker.h"

namespace sw
{
    class OnlineSession;

    /**
     * @class ShooterOnlineFlow
     * @brief 온라인 흐름 하나입니다. 게임이 멤버로 들고 게임 범위에 겁니다. 게임 스레드에서 씁니다.
     */
    class ShooterOnlineFlow final
    {
    public:
        static constexpr const utf8* kAssetGold = "cur.gold";

        ShooterOnlineFlow();

        /** @brief 계정 응답을 꺼내 실패를 알립니다. 게임이 프레임마다 부릅니다. */
        void update( OnlineSession& session );

        void requestLogin( OnlineSession& session, string_view loginName, string_view password );
        void requestMailList( OnlineSession& session );
        void requestClaimAll( OnlineSession& session );
        /** @brief 이 프로세스 안에 세운 서버의 저장소에 로그인한 계정 앞으로 금화 우편을 넣습니다. 개발 명령과 시나리오의 준비 단계입니다. @return 안에 세운 서버가 없거나 로그인 전이면 false 입니다. */
        [[nodiscard]] bool sendSelfMailOnLocalServer( OnlineSession& session, int64 amount, int64 nowMs );

        /** @brief 저장소가 맡긴 우편을 끝냈을 때 부릅니다. */
        void  recordSelfMailSent() { ++_selfMailSentCount; }
        int32 getSelfMailSentCount() const { return _selfMailSentCount; }

    private:
        void onClaimAll( const OnlineResult<MailboxClaimReply>& result );

    private:
        vector<AccountClientReply> _listReplyScratch;
        OnlineSession*             _pClaimSession; ///< 응답이 올 때 경제 클라이언트를 찾는 세션이다. 세션은 게임 인스턴스의 것이고 이 객체보다 오래 산다
        int32                      _selfMailSentCount;
        ServiceStoreTracker        _localStore; ///< 로컬 서버 저장소에 맡긴 일이다(세션이 빌려준다)
    };
} // namespace sw
