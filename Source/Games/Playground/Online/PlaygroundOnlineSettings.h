/**
 * @file PlaygroundOnlineSettings.h
 * @brief Playground 온라인 흐름이 쓰는 에셋(`game/playground/data/online.xml`)입니다 — 상점의 상품 카탈로그와 상점 화면 문서.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
    /** @struct PlaygroundOnlineSettings @brief 파일 머리말 참고. 흐름(`PlaygroundOnlineFlow`)이 상점을 처음 열 때 읽는다. */
    REFLECT()
    struct PlaygroundOnlineSettings
    {
        REFLECT_BODY();

        static constexpr const utf8* kDataKind = "playgroundonline"; ///< 데이터 종류 이름(`SW_ASSET_TYPE`) — 경로는 종류의 팩 안 규칙과 게임 팩이 정한다

        PROPERTY( Required )
        DataPath _offerCatalog; ///< 상점 화면의 상품(서버 경제 키트와 같은 파일 — `Config/Server/Online/Playground.online.json`)
        PROPERTY( Required )
        DataPath _shopDocument; ///< 상점 화면 문서(`ShopScreen`)

        /** @brief 지금 게임 팩의 설정을 @p outSettings 에 읽습니다. 못 읽으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool loadForGame( PlaygroundOnlineSettings& outSettings );
    };
} // namespace sw
