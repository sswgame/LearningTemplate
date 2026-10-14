#include "pch.h"

#include "Games/StarSkirmish/StarSkirmishGame.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"

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

    StarSkirmishGame::StarSkirmishGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        registerDirector<SkirmishDirectorComponent>();
    }

    StarSkirmishGame::~StarSkirmishGame() = default;

    bool StarSkirmishGame::onInitialize()
    {
        // 사운드 이벤트 — 알림은 ui 버스 2D, 부서진 유닛은 그 자리(직교 카메라 = 화면 평면 팬).
        if ( GameSound::loadEvents( StarSkirmishGameInternal::kAudioEvents ) == false )
            SW_LOG_WARNING( "[Skirmish] %# could not be loaded - sounds stay silent", StarSkirmishGameInternal::kAudioEvents );
        // 패드만으로 고르기 · 명령을 쓰게 가상 커서를 켠다(오른쪽 스틱 = 커서, A/B = 마우스 왼쪽/오른쪽). 맵의 패드 A/B 바인딩은 그대로 둔다.
        if ( InputManager* pInput = game::getService<InputManager>(); pInput != nullptr )
            pInput->getVirtualCursor().setEnabled( true );
        // 전장 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void StarSkirmishGame::onShutdown()
    {
        if ( InputManager* pInput = game::getService<InputManager>(); pInput != nullptr )
            pInput->getVirtualCursor().setEnabled( false );
        GameSound::unloadEvents( StarSkirmishGameInternal::kAudioEvents );
    }

} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::StarSkirmishGame );
