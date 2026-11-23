/**
 * @file Shooter3DGame.cpp
 * @brief 빈 게임입니다 — 규칙 · 화면 · 온라인 흐름은 Playground 의 사격장 · 온라인 매치(`Source/Games/Playground/Shooter3D` · `Online`)로 옮겼다.
 */
#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 빈 게임입니다. */
    class Shooter3DGame : public GameInstance
    {
    };
} // namespace sw
SW_IMPLEMENT_GAME_MODULE( sw::Shooter3DGame );
