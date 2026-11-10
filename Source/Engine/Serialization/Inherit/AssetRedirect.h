/**
 * @file AssetRedirect.h
 * @brief 에셋 리다이렉터 — 옮기거나 이름을 바꾼 에셋의 옛 id 를 새 id 로 풉니다(NEXT 5-5 R1).
 * @details 표는 도메인 뿌리마다 `redirects.xml` 하나다: `<Redirects><Redirect from="game/empty/maps/old.scene.xml" to="game/empty/maps/new.scene.xml" /></Redirects>`.
 *          리소스 입구(`AssetIO`)가 찾기 전에 한 번 풀므로, 세이브 · 서버 · 다른 에셋에 적힌 옛 경로가 그대로 산다. 경로 교체(`ResourceRemap`, 조건부)보다 먼저 푼다.
 *          GUID 전환은 기각했다 — 경로 id 가 언리얼 SoftObjectPath 자리이고, 언리얼도 옮긴 에셋을 리다이렉터(`UObjectRedirector` · `[CoreRedirects]`)로 잇는다.
 *          쿠킹 작업 안에서도 푼다(경로 교체와 달리 기계마다 다를 것이 없는 고정 표다). 남은 옛 참조는 린트(`asset-redirect-refs`)가 보고한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
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
