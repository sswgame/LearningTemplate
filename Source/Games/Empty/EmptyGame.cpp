#include "pch.h"

#include "Games/Empty/EmptyGame.h"

#include "Games/Empty/BenchScene.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    EmptyGame::EmptyGame()
        : _benchScene{ nullptr }
    {
    }

    EmptyGame::~EmptyGame() = default;

    void EmptyGame::configureBootstrap( BootstrapConfig& outConfig )
    {
        outConfig._packRoot = "game/empty";
    }

    bool EmptyGame::onInitialize()
    {
        // 이 템플릿이 하는 일은 벤치 하네스를 깨우는 것뿐이다. 새 게임을 시작하면 아래 두 줄과
        // BenchScene.* 를 지우고 자기 씬을 세운다.
        _benchScene = make_unique<BenchScene>();
        if ( _benchScene->spawnFromGlobals() == false )
            _benchScene.reset();

        return true;
    }

    void EmptyGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );

        if ( _benchScene != nullptr )
            _benchScene->update( deltaTime );
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::EmptyGame );
