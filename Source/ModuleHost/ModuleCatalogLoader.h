/**
 * @file ModuleCatalogLoader.h
 * @brief 실행 파일 옆 `Modules/` 의 매니페스트를 읽어 호스트 대상(App = 빌드 마스크, Server = Server)으로 해석합니다. App · Server 가 같이 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Module/ModuleCatalog.h"

namespace sw
{
    /** @brief 매니페스트 읽기 · 해석을 호스트(App · Server)가 같이 쓰는 도우미입니다. */
    struct ModuleCatalogLoader
    {
        /**
         * @brief `<실행 파일 폴더>/Modules/` 의 `<모듈>.module.json` 을 모두 읽고 @p targetMask(`ModuleTarget` 비트)로 해석합니다. 꺼진 모듈과 까닭을 로그에 남깁니다.
         * @details Shipping 은 매니페스트를 복사하지 않는다(정적 링크) — 부르지 않습니다.
         */
        [[nodiscard]] static bool loadAndResolve( uint8 targetMask, ModuleCatalog& outCatalog, ModuleResolution& outResolution );
    };
} // namespace sw
