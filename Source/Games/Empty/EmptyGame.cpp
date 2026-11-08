/**
 * @file EmptyGame.cpp
 * @brief 최소 SWGame 템플릿 (키트 없음)
 *
 * @details 새 게임의 출발점이다. 실제 게임 로직은 없고, 측정용 벤치 하네스(`Client/Dev/StressTestScene`)를
 *          `-gv_stressTestMeshes=N` 이 주어졌을 때만 깨운다. 이 폴더를 복사해 새 게임을 시작할
 *          때는 `Client/` 폴더와 아래 `_clientPart` 를 지우면 된다.
 *          벤치 · 렌더 확장 시연 · UI 시연은 그리는 개발 장치라 클라이언트 전용 · 개발 전용 소스(`Client/Dev/`)다 — 게임은 그 부분을 `IGameClientPart` 로만 들고,
 *          전용 서버와 배포 구성은 그 소스를 짓지 않아(공장이 없다) 첫 씬을 연다.
 *          장르 키트 시연(액션 룸 · 액션 플랫포머 · 택틱스 SRPG — `KitDemo/`)은 씬 하나씩이고, 그 몬스터 정의(`data/monsters.xml`)를 여기서 게임 서비스로 건다.
 *          지역 효과 시연(`maps/zoneshowcase` · `maps/surfaceinteraction`)의 이펙트 · 세트(`data/abilities.xml`)와 기본 어빌리티 클래스도 여기서 건다 — 지역 프로필이 읽을 때 참조를 이 카탈로그로 본다.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/GameConfig.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModSession.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Catalog/MonsterCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IContentConfiguration.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IGameClientPart.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 기동만 지정하는 빈 게임 팩 */
    class EmptyGame : public GameInstance
    {
    public:
        EmptyGame();
        ~EmptyGame() override;

    protected:
        bool onPrepareContent() override;
        bool onInitialize() override;
        void onUpdate( float32 deltaTime ) override;
        /** @brief 상태 스냅샷을 만들기 직전에 부릅니다. 벤치는 절차적으로 생성한 오브젝트라서 정리해 스냅샷에 싣지 않습니다. */
        void onBeforeStateSerialize() override;
        /** @brief 상태 복원이 씬을 교체한 뒤에 부릅니다. 벤치는 스냅샷에 없으므로 다시 만듭니다. */
        void onAfterStateDeserialize() override;

    private:
        /**
         * @brief 이 세션의 모드(`-gv_modList`, 시연 모드 `Mods/demo_lantern`)입니다. 첫 씬을 열기 전에 적용하고 인스턴스와 함께 뗀다(시나리오 체인의 다음 단계에 남지 않는다).
         *        서버 구성에도 있다 — 서버도 같은 모드 구성으로 서야 클라이언트와 구성 해시가 맞는다.
         */
        unique_ptr<ModSession> _mods;
        /** @brief 클라이언트 전용 개발 부분(벤치 · 렌더 확장 시연, `Client/Dev/EmptyClientPart.cpp`)입니다. 공장이 없는 구성(전용 서버 · 배포)에는 nullptr 이다. */
        unique_ptr<IGameClientPart> _clientPart;

        /** @brief 장르 키트 시연(액션 룸 — `game/empty/maps/kitdemo_actioncombat`)의 몬스터 정의입니다. 액션 룸이 게임 서비스로 찾는다(공유 캐시의 한 벌). */
        shared_ptr<const MonsterCatalog> _pMonsterCatalog;
        /** @brief 지역 효과 시연의 어빌리티 클래스(기본 — Generic · Combat 세트, ApplyEffects)입니다. */
        AbilityClassRegistry _abilityClasses;
        /** @brief 지역 효과 시연의 이펙트 · 어빌리티 · 세트입니다(공유 캐시의 한 벌). 어빌리티 시스템과 지역 프로필이 게임 서비스로 찾는다. */
        shared_ptr<const AbilityCatalog> _pAbilityCatalog;
    };
} // namespace sw

namespace sw
{
    EmptyGame::EmptyGame()
        : _mods{ nullptr }
        , _clientPart{ nullptr }
        , _pMonsterCatalog{ nullptr }
        , _abilityClasses{}
        , _pAbilityCatalog{ nullptr }
    {
    }

    EmptyGame::~EmptyGame() = default;

    bool EmptyGame::onPrepareContent()
    {
        // 모드는 시작 때 한 번 고정한다 — 넷 세션을 열기 전(구성 해시가 와이어 버전에 섞인다) · 첫 씬과 그 리소스를 읽기 전이라 덮어쓴 리소스를 쥔 캐시가 없다.
        // 켠 모드가 없으면 아무것도 하지 않는다. 요청한 모드를 적용하지 못하면 게임을 띄우지 않는다(요청과 다른 구성으로 조용히 돌지 않는다).
        // 키트 명령(mod.status)은 ModSession 으로, 기동 · 세이브는 IContentConfiguration 으로 찾는다.
        _mods = make_unique<ModSession>( game::getSubsystem<AssetManager>() );
        return _mods->applyForGame( game::getSubsystem<const GameConfig>()._packRoot.get() ) && getGameScope().bind( _mods.get() ) &&
               getGameScope().bind<IContentConfiguration>( _mods.get() );
    }

    bool EmptyGame::onInitialize()
    {
        // 장르 키트 시연의 필수 데이터 — 액션 룸은 몬스터 정의를 게임 서비스로 찾는다(코드 폴백 없음). 못 읽으면 게임을 띄우지 않는다.
        if ( loadRequiredCatalog( _pMonsterCatalog ) == false )
            return false;
        // 지역 효과 시연의 필수 데이터 — 지역 프로필의 할 일이 이 카탈로그의 이펙트를 건다(읽을 때 참조를 본다). 못 읽으면 게임을 띄우지 않는다.
        if ( bindScopedService( &_abilityClasses ) == false || loadRequiredCatalog( _pAbilityCatalog ) == false )
            return false;

        if ( IGameClientPartFactory* const pFactory = game::findSubsystem<IGameClientPartFactory>() )
            _clientPart = pFactory->createClientPart();
        const bool bBenchRunning = _clientPart != nullptr && _clientPart->initialize();

        // 벤치가 아니면 처음 씬을 엽니다. 우선순위는 실행 설정의 시작 씬, GameSettings 의 타이틀 씬, 시작 맵 순입니다.
        // 에디터 밖에서 실행해도 씬이 없는 채로 뜨지 않게 하고, 배포본이 씬 로드 경로(SCN1, 프리팹 경로 해석)를 실제로 실행하는 곳이기도 합니다.
        // 에디터가 자기 시작 씬(-gv_editorStartupScene)을 열면 그 요청이 나중에 큐에 들어가므로 이깁니다. SceneManager 는 마지막 요청만 남깁니다.
        if ( bBenchRunning == false )
            (void)getSubsystem<GameSceneFlow>().requestFirstScene(); // 열 씬이 없으면 빈 씬으로 시작합니다. 요청이 실패하면 함수 안에서 알립니다.

        return true;
    }

    void EmptyGame::onUpdate( float32 deltaTime )
    {
        GameInstance::onUpdate( deltaTime );

        if ( _clientPart != nullptr )
            _clientPart->update( deltaTime );
    }

    void EmptyGame::onBeforeStateSerialize()
    {
        if ( _clientPart != nullptr )
            _clientPart->onBeforeStateSerialize();
    }

    void EmptyGame::onAfterStateDeserialize()
    {
        if ( _clientPart != nullptr )
            _clientPart->onAfterStateDeserialize();
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::EmptyGame );
