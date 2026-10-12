#include "pch.h"

#include "Games/ThemeParkTycoon/ThemeParkTycoonGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"

#include "Games/ThemeParkTycoon/ParkDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "ThemeParkTycoonGame" );

    namespace
    {
        struct ThemeParkTycoonGameInternal
        {
            static constexpr const utf8* kAudioEvents = "game/themepark/audio/themepark.audioevents.xml";
        };
    } // namespace

    ThemeParkTycoonGame::ThemeParkTycoonGame()
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        registerDirector<ParkDirectorComponent>();
    }

    ThemeParkTycoonGame::~ThemeParkTycoonGame() = default;

    bool ThemeParkTycoonGame::onInitialize()
    {
        // 사운드 이벤트 — 모두 2D 다(위에서 내려다보는 공원).
        if ( GameSound::loadEvents( ThemeParkTycoonGameInternal::kAudioEvents ) == false )
            SW_LOG_WARNING( "[Park] %# could not be loaded - sounds stay silent", ThemeParkTycoonGameInternal::kAudioEvents );
        // 공원 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void ThemeParkTycoonGame::onShutdown()
    {
        GameSound::unloadEvents( ThemeParkTycoonGameInternal::kAudioEvents );
    }

} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::ThemeParkTycoonGame );
