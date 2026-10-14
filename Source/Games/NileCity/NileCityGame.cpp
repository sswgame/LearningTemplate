#include "pch.h"

#include "Games/NileCity/NileCityGame.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"

#include "Games/NileCity/NileDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "NileCityGame" );

    namespace
    {
        struct NileCityGameInternal
        {
            static constexpr const utf8* kAudioEvents = "game/nilecity/audio/nilecity.audioevents.xml";
        };
    } // namespace

    NileCityGame::NileCityGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        registerDirector<NileDirectorComponent>();
    }

    NileCityGame::~NileCityGame() = default;

    bool NileCityGame::onInitialize()
    {
        // 사운드 이벤트 — 모두 2D 다(위에서 내려다보는 도시).
        if ( GameSound::loadEvents( NileCityGameInternal::kAudioEvents ) == false )
            SW_LOG_WARNING( "[Nile] %# could not be loaded - sounds stay silent", NileCityGameInternal::kAudioEvents );
        // 패드만으로 놓기 · 헐기를 쓰게 가상 커서를 켠다(오른쪽 스틱 = 커서, A/B = 마우스 왼쪽/오른쪽). 맵의 패드 A/B 바인딩은 그대로 둔다.
        if ( InputManager* pInput = game::getService<InputManager>(); pInput != nullptr )
            pInput->getVirtualCursor().setEnabled( true );
        // 도시 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void NileCityGame::onShutdown()
    {
        if ( InputManager* pInput = game::getService<InputManager>(); pInput != nullptr )
            pInput->getVirtualCursor().setEnabled( false );
        GameSound::unloadEvents( NileCityGameInternal::kAudioEvents );
    }

} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::NileCityGame );
