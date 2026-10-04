#include "pch.h"

#include "Games/StarSkirmish/StarSkirmishGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"

#include "Games/StarSkirmish/SkirmishDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "StarSkirmishGame" );

    namespace
    {
        struct StarSkirmishGameInternal
        {
            static constexpr const utf8* kAudioEvents = "game/starskirmish/audio/starskirmish.audioevents.xml";
        };
    } // namespace

    StarSkirmishGame::StarSkirmishGame() = default;

    StarSkirmishGame::~StarSkirmishGame() = default;

    bool StarSkirmishGame::onInitialize()
    {
        // 사운드 이벤트 — 알림은 ui 버스 2D, 부서진 유닛은 그 자리(직교 카메라 = 화면 평면 팬).
        if ( GameSound::loadEvents( StarSkirmishGameInternal::kAudioEvents ) == false )
            SW_LOG_WARNING( "[Skirmish] %# could not be loaded - sounds stay silent", StarSkirmishGameInternal::kAudioEvents );
        // 전장 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void StarSkirmishGame::onShutdown()
    {
        GameSound::unloadEvents( StarSkirmishGameInternal::kAudioEvents );
    }

    void StarSkirmishGame::onBeforeStateSerialize()
    {
        // 디렉터가 세운 절벽 · 유닛은 판 상태의 모습일 뿐이다 — 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다.
        // 걷어 두면(삭제 대기는 스냅샷이 건너뛴다) 다시 만든 디렉터가 시작하며 세우고, 남은 디렉터는 다음 틱에 다시 세운다.
        SceneManager*      pSceneManager = game::getService<SceneManager>();
        Scene*             pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* pManager      = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 순회 콜백 안에서는 오브젝트를 지울 수 없다(매니저 잠금 안) — 디렉터를 모은 뒤 걷는다.
        vector<ComponentHandle> listDirector;
        pManager->forEachComponentOfType<SkirmishDirectorComponent>( [&listDirector]( SkirmishDirectorComponent* pDirector )
        { listDirector.push_back( pDirector->getHandle() ); } );
        for ( const ComponentHandle& handle : listDirector )
        {
            SkirmishDirectorComponent* pDirector = static_cast<SkirmishDirectorComponent*>( pManager->resolveComponent( handle ) );
            if ( pDirector != nullptr )
                pDirector->despawnViews();
        }
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::StarSkirmishGame );
