/**
 * @file PlaygroundGame.cpp
 * @brief 최소 SWGame 템플릿 (키트 없음)
 *
 * @details 새 게임의 출발점이다. 실제 게임 로직은 없고, 측정용 벤치 하네스(`Client/Dev/StressTestScene`)를
 *          `-gv_stressTestMeshes=N` 이 주어졌을 때만 깨운다. 이 폴더를 복사해 새 게임을 시작할
 *          때는 `Client/` 폴더와 아래 `_clientPart` 를 지우면 된다.
 *          벤치 · 렌더 확장 시연 · UI 시연은 그리는 개발 장치라 클라이언트 전용 · 개발 전용 소스(`Client/Dev/`)다 — 게임은 그 부분을 `IGameClientPart` 로만 들고,
 *          전용 서버와 배포 구성은 그 소스를 짓지 않아(공장이 없다) 첫 씬을 연다.
 *          장르 키트 시연(`KitDemo/`)은 씬 하나씩이고 미니게임(`minigames` 의 `.minigame.xml`, `MinigameFlow`)으로 들어간다 — 키트가 범위에 걸 것(액션 룸의 몬스터 정의)은
 *          미니게임이 게임 기능으로 켜고 끈다(`SW_GAME_FEATURE`, 들어갈 때 걸고 나올 때 거둔다).
 *          도시 건설 미니게임(`FrontierTown/` — 키트 둘의 디렉터를 한 씬에 섞는다)의 도시 쪽은 키트, RTS 쪽(일꾼 · 순찰)은 이 게임의 디렉터다.
 *          RTS 매치 미니게임(`StarSkirmish/`)의 디렉터는 RTS 키트의 1 대 1 디렉터에 락스텝 키트를 섞은 파생이다(키트끼리는 서로 모른다).
 *          액션 어드벤처 미니게임(`Velmora/` — 지역을 걷는 키트와 룸 전투 키트를 한 씬에 섞는다)도 지역 전투만 이 게임의 디렉터다.
 *          초원 마을 미니게임(`MeadowVillage/`)은 하우징 · 생물 · 농장 키트를 한 씬에 섞는다 — 집 자동 짓기 디렉터와 한 프로세스 재생(`MeadowNetSimGame`)이 이 게임의 몫이다.
 *          마을(`maps/hub`)은 팩의 시작 맵이다 — 전용 서버가 띄우면 허브 서버(`Hub/` — 계정마다 진행을 저장하는 공유 월드, NetMMO 복제)가 서고, 클라이언트는
 *          `net.ticketconnect <로그인 주소> <계정> <비밀번호> <서버 이름> <게임 주소>` 로 붙는다(`py -3 -m Scripts net-pair mmo`). 지역 효과 시연 · 마을의 이펙트 · 세트(`data/abilities.xml`)와 기본 어빌리티 클래스는 gamesettings `gameFeatures` 의
 *          게임 기능(`PlaygroundAbilitiesGameFeature.cpp`)이 게임 범위에 건다.
 *          사격장 미니게임(`Shooter3D/`)의 무기는 게임 기능 `Shooter3D` 가 건다. 온라인(`Online/PlaygroundOnlineFlow` — 로그인 · 우편 · 상점 · 매치 세션 · 순위표)은 이 게임이 멤버로 들고
 *          게임 범위에 건다 — 매치형 미니게임(사격장 온라인 매치)의 매칭 창구이기도 하다. 연결은 세션(`OnlineSession`)이 맡고 게임이 열기 전에는 혼자 하는 허브다.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Net/NetMessageChannel.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModSession.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IContentConfiguration.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IGameClientPart.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/GameStateSnapshot.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

#include "Games/Playground/Hub/HubNetLink.h"
#include "Games/Playground/Hub/HubPawnReplicationComponent.h"
#include "Games/Playground/Online/PlaygroundOnlineFlow.h"
#include "Games/Playground/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Playground/Shooter3D/ShooterNetPawnComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 기동만 지정하는 빈 게임 팩 */
    class PlaygroundGame : public GameInstance
    {
    public:
        PlaygroundGame();
        ~PlaygroundGame() override;

    protected:
        bool onPrepareContent( const ContentPreparationToken& token ) override;
        bool onInitialize() override;
        /** @brief 클라이언트 부분(벤치)이 자기 장면을 띄웠으면 첫 씬을 열지 않습니다. */
        void startFirstScene() override;
        void onUpdate( float32 deltaTime ) override;
        /** @brief 허브 엔티티 상태(`HubEntityNetState`)와 폰 상태 · 모습(`HubPawnState` · `HubPawnPose`), 사격장의 걸음 · 적 · 소유자 상태의 복제 스키마를 종류 번호마다 핸드셰이크에 올린다 — 칸이 다른 빌드는 접속에서 갈린다. */
        [[nodiscard]] bool onRegisterReplicatedTypes( NetMessageChannel& outChannel ) override;
        /** @brief 상태 스냅샷을 만들기 직전에 부릅니다. 벤치는 절차적으로 생성한 오브젝트라서 정리해 스냅샷에 싣지 않습니다. */
        void onBeforeStateSerialize() override;
        /** @brief 상태 복원이 씬을 교체한 뒤에 부릅니다. 벤치는 스냅샷에 없으므로 다시 만듭니다. */
        void onAfterStateDeserialize() override;

    private:
        /**
         * @brief 이 세션의 모드(`-gv_modList`, 시연 모드 `Test/Mods/demo_lantern` · 벨모라 확장 팩 `Test/Mods/velmora_tidecaves`)입니다. 첫 씬을 열기 전에 적용하고 인스턴스와 함께 뗀다(시나리오 체인의 다음 단계에 남지 않는다).
         *        서버 구성에도 있다 — 서버도 같은 모드 구성으로 서야 클라이언트와 구성 해시가 맞는다.
         */
        unique_ptr<ModSession> _mods;
        /** @brief 클라이언트 전용 개발 부분(벤치 · 렌더 확장 시연, `Client/Dev/PlaygroundClientPart.cpp`)입니다. 공장이 없는 구성(전용 서버 · 배포)에는 nullptr 이다. */
        unique_ptr<IGameClientPart> _clientPart;
        PlaygroundOnlineFlow        _onlineFlow; ///< 온라인 흐름(게임 범위 서비스 — 매치형 미니게임의 매칭 창구이기도 하다). 연결은 세션(`OnlineSession`)이 맡는다
    };
} // namespace sw

namespace sw
{
    PlaygroundGame::PlaygroundGame()
        : _mods{ nullptr }
        , _clientPart{ nullptr }
        , _onlineFlow{}
    {
        // 키트 시연 디렉터(GameModeComponent 파생 — KitDemoComponent · ActionPlatformerGameModeComponent …)의 판 상태를 세이브 · 핫 리로드에 싣는다. 바탕 형식 하나로 올리면
        // 파생이 모두 실린다(순회는 파생을 포함하고 상태는 가상 serializeState). 올리지 않으면 game.save · game.load 가 판을 조용히 새로 연다(kitdemo_respawn 4).
        getStateSnapshot().registerGameMode<GameModeComponent>();
    }

    PlaygroundGame::~PlaygroundGame() = default;

    bool PlaygroundGame::onPrepareContent( const ContentPreparationToken& token )
    {
        // 모드는 시작 때 한 번 고정한다 — 넷 세션을 열기 전(구성 해시가 와이어 버전에 섞인다) · 첫 씬과 그 리소스를 읽기 전이라 덮어쓴 리소스를 쥔 캐시가 없다.
        // 확장 팩의 층을 카탈로그가 읽기 전이기도 하다(카탈로그는 미니게임에 들어갈 때 읽는다). 켠 모드가 없으면 아무것도 하지 않는다. 요청한 모드를 적용하지 못하면 게임을 띄우지 않는다(요청과 다른 구성으로 조용히 돌지 않는다).
        // 키트 명령(mod.status)은 ModSession 으로, 기동 · 세이브는 IContentConfiguration 으로 찾는다.
        _mods = make_unique<ModSession>( game::getSubsystem<AssetManager>() );
        return _mods->applyForGame( token, game::getSubsystem<const GameConfig>()._packRoot.get() ) && getGameScope().bind( _mods.get() ) &&
               getGameScope().bind<IContentConfiguration>( _mods.get() );
    }

    bool PlaygroundGame::onInitialize()
    {
        _onlineFlow.initialize( getSubsystem<MinigameFlow>() );
        return bindScopedService( &_onlineFlow ) && bindScopedService<IMinigameMatchmaking>( &_onlineFlow );
    }

    void PlaygroundGame::startFirstScene()
    {
        if ( IGameClientPartFactory* const pFactory = game::findSubsystem<IGameClientPartFactory>() )
            _clientPart = pFactory->createClientPart();
        const bool bBenchRunning = _clientPart != nullptr && _clientPart->initialize();

        // 벤치가 아니면 처음 씬을 엽니다(바탕의 기본 — 실행 설정의 시작 씬, GameSettings 의 타이틀 씬, 시작 맵 순).
        // 에디터 밖에서 실행해도 씬이 없는 채로 뜨지 않게 하고, 배포본이 씬 로드 경로(SCN1, 프리팹 경로 해석)를 실제로 실행하는 곳이기도 합니다.
        if ( bBenchRunning == false )
            GameInstance::startFirstScene();
    }

    void PlaygroundGame::onUpdate( float32 deltaTime )
    {
        GameInstance::onUpdate( deltaTime );
        _onlineFlow.update( getSubsystem<OnlineSession>() );

        if ( _clientPart != nullptr )
            _clientPart->update( deltaTime );
    }

    bool PlaygroundGame::onRegisterReplicatedTypes( NetMessageChannel& outChannel )
    {
        return outChannel.registerReplicatedType<HubEntityNetState>( static_cast<uint8>( HubEntityType::kAvatar ) ) &&
               outChannel.registerReplicatedType<HubEntityNetState>( static_cast<uint8>( HubEntityType::kCreature ) ) &&
               outChannel.registerReplicatedType<HubEntityNetState>( static_cast<uint8>( HubEntityType::kHerb ) ) &&
               outChannel.registerReplicatedType<HubPawnState>( static_cast<uint8>( HubPawnReplicationComponent::kOwnerStateSchemaID ) ) &&
               outChannel.registerReplicatedType<HubPawnPose>( static_cast<uint8>( HubPawnReplicationComponent::kPawnTypeID ) ) &&
               outChannel.registerReplicatedType<ShooterMoveState>( static_cast<uint8>( ShooterNetPawnComponent::kPawnTypeID ) ) &&
               outChannel.registerReplicatedType<ShooterEnemyNetState>( static_cast<uint8>( ShooterNetPawnComponent::kEnemyTypeID ) ) &&
               outChannel.registerReplicatedType<ShooterOwnerNetState>( static_cast<uint8>( ShooterNetPawnComponent::kOwnerStateSchemaID ) );
    }

    void PlaygroundGame::onBeforeStateSerialize()
    {
        if ( _clientPart != nullptr )
            _clientPart->onBeforeStateSerialize();
    }

    void PlaygroundGame::onAfterStateDeserialize()
    {
        if ( _clientPart != nullptr )
            _clientPart->onAfterStateDeserialize();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::PlaygroundGame );
