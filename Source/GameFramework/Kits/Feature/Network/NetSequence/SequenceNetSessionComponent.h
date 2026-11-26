/**
 * @file SequenceNetSessionComponent.h
 * @brief 게임 인스턴스의 네트워크 세션(월드의 넷 링크 `SceneNetLink`)이 열려 있으면 그 씬의 컷씬 재생(시퀀스 트리거)을 복제합니다.
 * @details 씬에 오브젝트 하나로 둔다. 역할은 넷 링크가 정한다: 서버면 `SequenceReplicationServer`, 클라이언트면 `SequenceReplicationClient`, 링크가 없으면(혼자 하는 게임) 아무것도 하지 않는다.
 *          오브젝트 번호표는 플레이 시작 때 만들고 디렉터 · 폰과 같은 씬 문서에 둔다. 서버는 마지막 틱 그룹(`PostUpdate`)에서 보낸다 — 트리거가 이번 프레임에 튼 재생을
 *          같은 프레임에 보낸다. 클라이언트는 메시지를 게임 프레임 앞에서 받아 바로 틀고, 로컬 건너뛰기 요청은 틱 뒤에 보낸다.
 *          `GF_NetPairedAnimation` 의 세션 컴포넌트와 같은 꼴이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetSequence/SequenceReplication.h"

namespace sw
{
    /**
     * @class SequenceNetSessionComponent
     * @brief 씬 하나의 컷씬 재생 복제를 넷 링크에 잇습니다. 자세한 계약은 파일 머리말 · `SequenceReplication.h` 를 보세요.
     */
    REFLECT( Category = "Network", DisplayName = "Sequence Net Session", Tooltip = "Replicates the cutscenes this scene's sequence triggers play on the server over the world's network link" )
    class SW_KIT_API SequenceNetSessionComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 트리거 · 디렉터의 틱 뒤에 보낸다 — 마지막 그룹에서 돈다.

        SequenceNetSessionComponent();
        ~SequenceNetSessionComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        bool                             isServer() const { return _bServer == SW_TRUE; }
        bool                             isStarted() const { return _routerSubscription.isActive(); }
        const SequenceReplicationServer& getServer() const { return _server; }
        const SequenceReplicationClient& getClient() const { return _client; }

    private:
        SW_TRANSIENT SequenceReplicationServer _server;
        SW_TRANSIENT SequenceReplicationClient _client;
        Subscription                           _routerSubscription; ///< 라우터에 단 처리기의 등록. 없으면 시작하지 않았다(처리기보다 뒤에 선언 — 먼저 풀린다)
        SW_TRANSIENT uint8                     _bServer;            ///< 서버로 시작했다
    };
} // namespace sw
