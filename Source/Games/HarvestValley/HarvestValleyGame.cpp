#include "pch.h"

#include "Games/HarvestValley/HarvestValleyGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"
#include "Games/HarvestValley/FarmVillageDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "HarvestValleyGame" );

    namespace
    {
        struct HarvestValleyGameInternal
        {
            static constexpr const utf8* kAudioEvents = "game/harvestvalley/audio/harvestvalley.audioevents.xml";
        };
    } // namespace

    HarvestValleyGame::HarvestValleyGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        registerStatefulComponent<SessionStateComponent>(); // 공유 상태가 디렉터보다 먼저 돌아온다
        registerDirector<FarmDirectorComponent>();
        registerDirector<FarmVillageDirectorComponent>(); // 농장 디렉터 뒤 — 같은 시계로 일정을 다시 세운다
    }

    HarvestValleyGame::~HarvestValleyGame() = default;

    bool HarvestValleyGame::onInitialize()
    {
        // 사운드 이벤트 — 모두 2D 다(위에서 내려다보는 농장).
        if ( GameSound::loadEvents( HarvestValleyGameInternal::kAudioEvents ) == false )
            SW_LOG_WARNING( "[Farm] %# could not be loaded - sounds stay silent", HarvestValleyGameInternal::kAudioEvents );
        // 농장 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void HarvestValleyGame::onShutdown()
    {
        GameSound::unloadEvents( HarvestValleyGameInternal::kAudioEvents );
    }

} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::HarvestValleyGame );
