/**
 * @file ModuleManager/ModuleCatalogLoader.h
 * @brief 실행 파일 옆 `Modules/` 의 매니페스트를 읽어 호스트 대상(App = 빌드 마스크, Server = Server)으로 해석합니다. App · Server 가 같이 씁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Module/ModuleCatalog.h"

namespace sw
{
    /** @brief 매니페스트 읽기 · 해석을 호스트(App · Server)가 같이 쓰는 도우미입니다. */
    struct ModuleCatalogLoader
    {
        /**
         * @brief `<실행 파일 폴더>/Modules/` 의 `<모듈>.module.json` 을 모두 읽고 @p targetMask(`ModuleTarget` 비트)로 해석합니다. 꺼진 모듈과 까닭을 로그에 남깁니다.
         * @details 함께 지은 게임 팩(`Bin/Games/<게임>/`)을 골랐으면 그 매니페스트가 프로젝트다. 고른 게임이 닿지 않고 다른 게임 팩만 닿는 키트(와 그에 기대는 모듈)는
         *          카탈로그에 넣지 않고 `_listInactive` 에 "not used by <게임>" 으로 적는다 — CMake 가 쓰지 않는 키트의 매니페스트를 `Bin/Modules` 에 두지 않는 것과 같은 기준이다.
         *          아무 게임도 닿지 않는 키트(`SW_BUILD_UNUSED_KITS=ON`)는 남는다. Shipping 은 매니페스트를 복사하지 않는다(정적 링크) — 부르지 않습니다.
         */
        [[nodiscard]] static bool loadAndResolve( uint8 targetMask, ModuleCatalog& outCatalog, ModuleResolution& outResolution );
    };
} // namespace sw
