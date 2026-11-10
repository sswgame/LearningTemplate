/**
 * @file AssetRedirect.h
 * @brief 에셋 리다이렉터 — 옮기거나 이름을 바꾼 에셋의 옛 id 를 새 id 로 풉니다(NEXT 5-5 R1).
 * @details 표는 도메인 뿌리마다 `redirects.xml` 하나다: `<Redirects><Redirect from="game/empty/maps/old.scene.xml" to="game/empty/maps/new.scene.xml" /></Redirects>`.
 *          리소스 입구(`AssetIO`)가 찾기 전에 한 번 풀므로, 세이브 · 서버 · 다른 에셋에 적힌 옛 경로가 그대로 산다. 경로 교체(`ResourceRemap`, 조건부)보다 먼저 푼다.
 *          GUID 전환은 기각했다 — 경로 id 가 언리얼 SoftObjectPath 자리이고, 언리얼도 옮긴 에셋을 리다이렉터(`UObjectRedirector` · `[CoreRedirects]`)로 잇는다.
 *          쿠킹 작업 안에서도 푼다(경로 교체와 달리 기계마다 다를 것이 없는 고정 표다). 남은 옛 참조는 린트(`asset-redirect-refs`)가 보고한다.
 *          줄은 옮기는 쪽이 더한다 — 에디터 옮기기 · 이름 바꾸기(`EditorAssetFileCommands::applyAssetMoves`)와 `py -3 -m Scripts move-asset` 이 같은 규칙(`addToXMLText`)으로 쓴다
 *          (언리얼이 에셋을 옮길 때 리다이렉터를 남기는 것과 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct AssetRedirect
     * @brief 프로세스의 리다이렉트 표입니다(팩 설정 단계 `PackEngineSettingsSubsystem` 이 검색 순서의 도메인마다 `redirects.xml` 을 읽는다). 읽기는 로드 워커 · IO 스레드에서도 온다.
     */
    struct SW_API AssetRedirect
    {
        static constexpr const utf8* kRootElement = "Redirects";
        /** @brief 도메인 뿌리 아래 표 파일 이름입니다(`engine/redirects.xml` · `game/<팩>/redirects.xml`). */
        static constexpr const utf8* kFileName = "redirects.xml";
        /** @brief 줄이 없는 표 글입니다 — 도메인에 아직 표 파일이 없을 때 `addToXMLText` 에 넘긴다. */
        static constexpr const utf8* kEmptyTableText = "<Redirects>
                                                     < / Redirects >
                                                       ";

                                                       /** @brief 리소스 id @p assetID 가 속한 도메인의 표 id(`<도메인>/redirects.xml`)입니다. 도메인은 첫 마디이고 `game/<팩>` · `dlc/<묶음>` 은 두 마디다. */
                                                       static string
                                                       makeTableID( string_view assetID );
        /** @brief @p assetID 가 표 파일이면 true 입니다 — 표는 옛 id 를 적는 글이라 참조 검사 · 옮기기의 참조 고침이 훑지 않는다. */
        static bool isTable( string_view assetID );
        /**
         * @brief 표 글 @p tableText 에 옮기기(@p listFrom[i] → @p listTo[i])를 더한 글을 돌려줍니다. 있던 줄의 순서는 그대로다(사람이 리뷰하는 글).
         * @details 옮긴 자리(to)를 옛 id 로 둔 줄은 뺀다 — 그 id 에 다시 에셋이 살므로 남기면 새 에셋이 가려지고, 되돌린 옮기기(A→B 뒤 B→A)는 순환이 된다.
         *          같은 옛 id 의 줄은 새 줄로 바꾼다. 사슬(A→B 뒤 B→C)은 그대로 둔다(읽을 때 접는다).
         * @return 지금 표가 틀렸거나, 결과 표가 읽히지 않으면(맞춤 꼴이 아닌 id · 순환) 오류입니다 — 기동을 멈출 표를 쓰지 않는다.
         */
        [[nodiscard]] static expected<string> addToXMLText( string_view tableText, string_view sourceName, const vector<string>& listFrom, const vector<string>& listTo );

        /**
         * @brief 표 글 여럿을 읽어 지금 표를 바꿉니다. @p listText 와 @p listSourceName 은 같은 길이입니다.
         * @details 틀린 글(모르는 원소 · 빠진 칸 · 맞춤 꼴이 아닌 id · 자기 자신으로 · 같은 옛 id 를 두 번 · 순환)이면 오류를 남기고 false 이며 표는 그대로다.
         *          사슬(A→B, B→C)은 받는다 — 두 번 옮긴 에셋의 첫 이름도 살아야 해서다. 읽을 때 끝(A→C)으로 접어 둔다.
         */
        [[nodiscard]] static bool loadFromXMLTexts( const vector<string>& listText, const vector<string>& listSourceName );
        /** @brief 검색 순서의 도메인마다 `redirects.xml` 을 읽습니다(없는 도메인은 리다이렉트가 없는 것이다). 틀린 표가 있으면 false 입니다. */
        [[nodiscard]] static bool loadFromSearchDomains();
        /** @brief 표를 비웁니다(엔진 종료 · 테스트). */
        static void clear();

        /** @brief @p assetID 가 옛 id 면 @p outAssetID 에 새 id 를 쓰고 true 입니다. 아니면 false 입니다(표가 비면 잠금 없이 끝난다). */
        [[nodiscard]] static bool resolve( string_view assetID, string& outAssetID );
    };
} // namespace sw
