/**
 * @file AssetRedirect.h
 * @brief 도메인의 리다이렉트 표(`redirects.xml`)입니다 — 정체성 대체(GUID → GUID) 줄과, 저장된 기록(세이브)의 옛 경로를 한 번 푸는 줄.
 * @details 참조는 GUID 를 적으므로 옮기기는 표에 줄을 더하지 않는다(유니티 · Godot 과 같다). 표에 남는 것은 둘이다.
 *          - **정체성 대체** `<IdentityRedirect from="<32자>" to="<32자>" />` — 에셋 둘을 하나로 합치거나(한쪽을 지운다) 다른 에셋으로 대신할 때만 적는다
 *            (언리얼 Replace References 가 남기는 리다이렉터 자리, 도구는 `py -3 -m Scripts asset-ids --redirect`). 저작 색인(`AssetDatabase`)과 팩 색인(`AssetPackIndex`)이
 *            이 줄을 들고 런타임 색인(`AssetRegistry`)이 GUID 를 찾지 못할 때만 따라간다 — 살아 있는 에셋이 먼저다.
 *          - **저장된 기록의 옛 경로** `<Redirect from="<옛 경로>" to="<경로>" />` · `<PrefixRedirect from="<옛 폴더>" to="<폴더>" />` — 경로를 적던 때의 세이브를 읽을 때만 쓴다
 *            (`readSavedReference`). 늘지 않는 마지막 표다: 옮기기는 새 줄을 더하지 않고 `to` 만 새 자리로 고친다(`move-asset`). 리소스 입구(`AssetIO`)는 이 줄을 보지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetRegistry.h"

namespace sw
{
    /**
     * @struct AssetRedirect
     * @brief 표의 읽기와, 프로세스의 옛 경로 표입니다(팩 설정 단계 `PackEngineSettingsSubsystem` 이 검색 순서의 도메인마다 읽는다). 조회는 로드 워커에서도 온다.
     */
    struct SW_API AssetRedirect
    {
        static constexpr const utf8* kRootElement = "Redirects";
        /** @brief 도메인 뿌리 아래 표 파일 이름입니다(`engine/redirects.xml` · `game/<팩>/redirects.xml`). */
        static constexpr const utf8* kFileName = "redirects.xml";

        /** @brief 리소스 id @p assetID 가 속한 도메인의 표 id(`<도메인>/redirects.xml`)입니다. 도메인은 첫 마디이고 `game/<팩>` · `dlc/<묶음>` 은 두 마디다. */
        static string makeTableID( string_view assetID );
        /** @brief @p assetID 가 표 파일이면 true 입니다 — 표는 옛 경로를 적는 글이라 참조 검사 · 옮기기의 참조 고침이 훑지 않는다. */
        static bool isTable( string_view assetID );

        /**
         * @brief 표 글 @p text 의 정체성 대체 줄입니다(글 순서). 저장된 기록의 옛 경로 줄은 보지 않습니다.
         * @return 틀린 글(모르는 원소 · 글 표기가 아닌 GUID · 자기 자신으로 · 같은 GUID 를 두 번)이면 오류를 남기고 그 까닭입니다. 순환은 색인을 합칠 때 본다(`AssetRegistry::merge`).
         */
        [[nodiscard]] static expected<vector<AssetIdentityRedirect>> parseIdentityRows( string_view text, string_view sourceName );

        /**
         * @brief 표 글 여럿의 옛 경로 줄을 읽어 지금 표를 바꿉니다. @p listText 와 @p listSourceName 은 같은 길이입니다.
         * @details 틀린 글(모르는 원소 · 빠진 칸 · 맞춤 꼴이 아닌 id · 자기 자신으로 · 같은 옛 id 를 두 번 · 순환)이면 오류를 남기고 false 이며 표는 그대로다.
         *          사슬(A→B, B→C)은 받는다 — 읽을 때 끝(A→C)으로 접어 둔다.
         */
        [[nodiscard]] static bool loadFromXMLTexts( const vector<string>& listText, const vector<string>& listSourceName );
        /** @brief 검색 순서의 도메인마다 `redirects.xml` 을 읽습니다(없는 도메인은 줄이 없는 것이다). 틀린 표가 있으면 false 입니다. */
        [[nodiscard]] static bool loadFromSearchDomains();
        /** @brief 표를 비웁니다(엔진 종료 · 테스트). */
        static void clear();

        /**
         * @brief 저장된 기록(세이브)에 적힌 참조 글 @p text 를 풉니다 — GUID 참조는 지금 경로로, 경로 글(경로를 적던 때의 기록)은 옛 경로 표로 한 번 풀어 지금 경로로.
         * @details 다시 저장하면 GUID 로 적힌다(`AssetRegistry::makeReferenceText`) — 옛 기록은 읽을 때 한 번만 풀린다. 파일의 참조 칸은 이것을 쓰지 않는다(`AssetRegistry::readReferenceText`).
         */
        [[nodiscard]] static expected<AssetReferenceTarget> readSavedReference( string_view text );
    };
} // namespace sw
