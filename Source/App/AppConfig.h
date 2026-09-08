/**
 * @file AppConfig.h
 * @brief App 이 부팅할 때 읽는 설정 — 어떤 게임플레이 키트 모듈을 올릴지.
 * @details Shipping 은 모든 모듈이 정적 링크라 이 목록을 쓰지 않는다 (App::startModules 참고).
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 게임플레이 키트 모듈 하나 (예: GF_TurnBattle) 와 그 선행 모듈 목록. */
    REFLECT()
    struct GameKitConfig
    {
        REFLECT_BODY();

        /** @brief LiveReload 에 등록할 모듈(=CMake 타깃) 이름. */
        PROPERTY()
        string _name;

        /** @brief 이 키트보다 먼저 올라와야 하는 모듈들. 비어 있으면 GameFramework 로 채운다. */
        PROPERTY()
        vector<string> _listDependencyModule;
    };

    REFLECT()
    struct AppConfig : IConfig
    {
        REFLECT_BODY();

        /** @brief SWGame 보다 먼저 올릴 게임플레이 키트 모듈 목록. */
        PROPERTY()
        vector<GameKitConfig> _listGameKitModule;
    };
} // namespace sw
