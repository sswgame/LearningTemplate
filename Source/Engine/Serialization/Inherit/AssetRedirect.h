/**
 * @file AssetRedirect.h
 * @brief 도메인의 정체성 대체 표(`redirects.xml` — GUID → GUID)입니다.
 * @details 참조는 GUID 를 적으므로 옮기기는 표에 줄을 더하지 않는다(유니티 · Godot 과 같다). 줄 `<IdentityRedirect from="<32자>" to="<32자>" />` 은 에셋 둘을 하나로 합치거나
 *          (한쪽을 지운다) 다른 에셋으로 대신할 때만 적는다(언리얼 Replace References 가 남기는 리다이렉터 자리, 도구는 `py -3 -m Scripts asset-ids --redirect`).
 *          저작 색인(`AssetDatabase`)과 팩 색인(`AssetPackIndex`)이 이 줄을 들고 런타임 색인(`AssetRegistry`)이 GUID 를 찾지 못할 때만 따라간다 — 살아 있는 에셋이 먼저다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Content/AssetRegistry.h"

namespace sw
{
    /** @brief 정체성 대체 표의 이름과 읽기입니다. */
    struct SW_API AssetRedirect
    {
        static constexpr const utf8* kRootElement = "Redirects";
        /** @brief 도메인 뿌리 아래 표 파일 이름입니다(`engine/redirects.xml` · `game/<팩>/redirects.xml`). */
        static constexpr const utf8* kFileName = "redirects.xml";

        /** @brief 리소스 id @p assetID 가 속한 도메인의 표 id(`<도메인>/redirects.xml`)입니다. 도메인은 첫 마디이고 `game/<팩>` · `dlc/<묶음>` 은 두 마디다. */
        static string makeTableID( string_view assetID );
        /** @brief @p assetID 가 표 파일이면 true 입니다 — 표는 지운 에셋의 GUID 를 적는 글이라 참조 검사 · 옮기기의 참조 고침이 훑지 않는다. */
        static bool isTable( string_view assetID );

        /**
         * @brief 표 글 @p text 의 정체성 대체 줄입니다(글 순서).
         * @return 틀린 글(모르는 원소 · 글 표기가 아닌 GUID · 자기 자신으로 · 같은 GUID 를 두 번)이면 오류를 남기고 그 까닭입니다. 순환은 색인을 합칠 때 본다(`AssetRegistry::merge`).
         */
        [[nodiscard]] static expected<vector<AssetIdentityRedirect>> parseIdentityRows( string_view text, string_view sourceName );
    };
} // namespace sw
