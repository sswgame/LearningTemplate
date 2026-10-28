/**
 * @file Engine/Foundation/HostSubsystemNames.h
 * @brief 호스트 범위 서브시스템 중 다른 서브시스템이 의존 칸에 적는 이름입니다.
 * @details 이름을 낮은 층의 이 헤더에 두는 까닭은 구현한 쪽(`GameModuleManager`)과 의존하는 쪽(`Engine/Automation`)이 서로의 헤더를 include 하지 않고 같은 이름을 쓰게 하려는 것입니다.
 *          의존하는 쪽이 자기 의존 칸(`getSubsystemDependencies`)에 이름을 적는 방향만 쓰고, 구현한 쪽은 누가 자기 뒤에 서는지 모릅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 호스트 범위 서브시스템 이름 모음입니다. */
    struct HostSubsystemNames
    {
        /** @brief 모듈 라이브러리 창구(`ModuleLoaderSubsystem`)입니다. 자동화 구동기는 모듈이 올라온 뒤에 시작하므로 이것보다 뒤에 섭니다. */
        static constexpr const utf8* kModuleLoader = "ModuleLoader";
    };
} // namespace sw
