/**
 * @file GameSelectionBuildShipping.cpp
 * @brief 배포 구성의 게임 고르기 — 빌드 시점에 베이크한 게임(`config::kShippingGameName`) 하나이고 `-game=` 을 보지 않는다(모듈은 정적 링크).
 * @details 개발은 `GameSelectionBuild.cpp` 를 CMake 가 고른다(`Source/Engine/CMakeLists.txt`).
 */
#include "pch.h"

#include "Engine/Config/GameSelection.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    string GameSelection::readDefaultName()
    {
        return string{ config::kShippingGameName };
    }

    bool GameSelection::select( [[maybe_unused]] const CommandLineManager& commandLine )
    {
        storeName( config::kShippingGameName );
        return true;
    }
} // namespace sw
