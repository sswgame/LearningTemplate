/**
 * @file HousingNetSessionComponent.h
 * @brief 씬의 하우징 부지를 그 월드의 넷 링크(`SceneNetLink`)에 잇습니다 — 서버는 편집 요청을 판정하고 부지 상태를 방송하며, 클라이언트는 요청을 보내고 상태를 받습니다.
 * @details 씬에 하나 둔다. 역할은 넷 링크가 정한다(전용 서버 · 리슨 호스트 · 클라이언트, 에디터의 한 프로세스 재생 월드). 링크가 없으면(혼자 하는 게임) 아무것도 하지 않는다.
 *          부지는 같은 매니저의 컴포넌트 등록표(`HousingPlotComponent`)에서 틱마다 찾는다 — 부지 포인터를 들지 않아 부지가 먼저 사라져도 남는 포인터가 없다.
 *          틱은 자기 할 일만 하고 일은 틱 뒤 큐 한 번에 건다: 부지 여럿(남의 오브젝트)의 레이아웃을 쓰고 요청 줄을 꺼내기 때문이다(씬에 하나라 프레임에 한 일 —
 *          `Gameplay.md` 규칙 6 의 프레임 시스템이 큐에 거는 한 일과 같은 자리). 판정 · 방송 규칙은 `HousingReplication.h` 를 보세요.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Housing/Net/HousingReplication.h"

namespace sw
{
    class GameObjectManager;
    class HousingPlotComponent;

    /**
     * @class HousingNetSessionComponent
     * @brief 파일 머리말 참고.
     */
    REFLECT( Category = "Housing", DisplayName = "Housing Net Session", Tooltip = "Server-authoritative housing over this world's net link: clients send edit requests, the server judges them and broadcasts plot states" )
    class SW_KIT_API HousingNetSessionComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( HousingNetSessionComponent );

        HousingNetSessionComponent();
        ~HousingNetSessionComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 서버가 요청자의 자리를 얻는 길입니다(서버의 아바타). `_maxReach` 가 0 보다 큰 부지가 있으면 게임이 줘야 한다. */
        void setCallerLocator( const HousingCallerLocator& locator );

        bool                            isActive() const { return _routerSubscription.isActive(); }
        bool                            isServer() const { return _bServer == SW_TRUE; }
        const HousingReplicationServer& getServer() const { return _server; }
        const HousingReplicationClient& getClient() const { return _client; }

    private:
        /** @brief 틱 뒤(게임 스레드): 열린 부지를 모아 서버는 판정 · 방송하고, 클라이언트는 쌓인 요청을 보내고 받은 상태 · 판정 · 주인을 부지에 돌려준다. */
        void poll( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Housing", DisplayName = "Max Requests Per Tick", Min = 1, Tooltip = "Edit requests one connection may queue per server tick; the rest are answered RateLimited" )
        int32 _maxRequestsPerTick;

        HousingReplicationServer      _server;
        HousingReplicationClient      _client;
        HousingCallerLocator          _locateCaller;
        vector<HousingNetPlotRef>     _listPlot;          ///< 틱 뒤 한 번 동안만 쓰는 부지(빌린 것)
        vector<HousingPlotComponent*> _listPlotComponent; ///< `_listPlot` 과 같은 자리의 부지 컴포넌트
        vector<HousingEditRequest>    _listRequest;
        vector<HousingEditReply>      _listReply;
        Subscription                  _routerSubscription; ///< 라우터에 단 처리기(`_server` 또는 `_client`)의 등록. 처리기보다 뒤에 선언한다 — 먼저 풀린다
        uint8                         _bServer  : 1;
        uint8                         _bStarted : 1; ///< 처리기를 걸고 시작했다 — 그 뒤 구독이 풀리면 세션이 닫힌 것이다
        [[maybe_unused]] uint8        _reserved : 6;
    };
} // namespace sw
