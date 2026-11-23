/**
 * @file AssetPackIndex.h
 * @brief 팩 하나에 든 에셋의 GUID → 팩 안 경로 색인(`assetindex.xml`)입니다. 팩 쿠킹이 쓰고, 런타임이 마운트한 팩마다 읽어 합친다(`AssetManager`).
 * @details 배포본에는 `.meta` 가 없다(PackConfig 가 뺀다) — 정체성은 이 색인이 나른다. 팩마다 자기 색인을 들고, 패치 · DLC 팩은 자기 색인에 줄을 더할 뿐이다
 *          (옮기기용 리다이렉트 표처럼 옛 줄이 영구히 쌓이지 않는다 — 옮긴 에셋은 GUID 가 그대로라 새 팩의 줄이 새 경로를 적는다).
 *          유니티 Addressables 의 카탈로그(GUID → 번들 안 경로), 언리얼 에셋 레지스트리의 팩(.pak)별 조각과 같은 자리다.
 *          줄은 GUID 순이다(같은 입력은 같은 바이트 — 팩 쿠킹의 재사용 판정이 바이트로 본다). 한 팩 안에서 GUID · 경로가 겹치면 쓰지도 읽지도 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetGUID.h"
#include "Engine/Content/AssetRegistry.h"

namespace sw
{
    /** @brief 색인 한 줄입니다 — 팩 안 경로는 팩의 마운트 도메인 기준이다(`textures_raw/x.png` 가 아니라 팩에 담긴 그 이름). */
    struct AssetPackIndexEntry
    {
        AssetGUID _guid;
        string    _path;
    };

    /**
     * @struct AssetPackIndex
     * @brief 팩 하나의 색인입니다. 파일 머리말 참고.
     */
    struct SW_API AssetPackIndex
    {
        /** @brief 팩 루트의 색인 파일 이름입니다. */
        static constexpr string_view kFileName = "assetindex.xml";
        /** @brief 색인 글의 판(루트 `formatVersion`)입니다. 꼴이 바뀌면 올리고 읽는 쪽이 옛 판을 올린다. */
        static constexpr uint32 kFormatVersion = 0;

        vector<AssetPackIndexEntry>   _listEntry;
        vector<AssetIdentityRedirect> _listRedirect; ///< 도메인 리다이렉트 표의 정체성 대체 줄(`AssetRedirect::parseIdentityRows`)

        /** @brief 색인 글입니다(GUID 순). GUID · 경로가 겹치면 그 까닭입니다. */
        [[nodiscard]] expected<string> makeText() const;
        /** @brief @p text 를 읽은 색인입니다. 꼴이 틀리거나(모르는 판 · 칸) GUID · 경로가 겹치면 그 까닭입니다. */
        [[nodiscard]] static expected<AssetPackIndex> parseText( string_view text, string_view sourceName );
    };
} // namespace sw
