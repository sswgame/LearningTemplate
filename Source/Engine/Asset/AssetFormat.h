/**
 * @file AssetFormat.h
 * @brief XML 에셋 스키마 판의 정확 일치 검사와 저작 소스 · 쿠킹본 경로 규칙입니다. 둘 다 에셋 종류 표(`AssetTypeRegistry`)에서 파생합니다.
 * @details 판은 루트 속성 `formatVersion` 에 적습니다(XML 선언이 아닙니다). 없으면 세대 0 입니다.
 *          스키마가 깨지면 그 종류의 정의(`AssetTypeDef::_formatVersion`)를 올리고 저장소의 데이터를 다시 씁니다. 첫 출시 전이라 이관 함수도 옛 판 리더도 없고,
 *          판이 다르면 읽지 않고 오류로 멈춥니다(`AssetXMLFormat::isCurrentXMLVersion`).
 *          저작 기본 포맷은 XML 입니다. JSON 은 도구 · 설정을 주고받는 데 쓰고, JSON 접미사는 쿠킹 입력으로만 에셋 종류가 된다(`AssetTypeRegistry::isSuffixFormatAllowed`).
 *          Shipping 런타임은 쿠킹된 바이너리(PFB2 등)를 로드합니다.
 */
#pragma once
#include "Core/Hash/HashUtil.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    /**
     * @brief 저작 소스 경로와 쿠킹본 경로 사이의 이름 규칙입니다. 로더(씬 · 프리팹)와 쿠커가 모두 여기를 지납니다.
     * @details 규칙은 종류 표(`AssetTypeRegistry`)의 접미사와 역할에서 나온다 — 씬 로더 · 씬 쿠커 · 프리팹 로더 · 프리팹 쿠커 · 팩 쿠커 · 에디터가 각자 확장자를
     *          적으면 쿠킹하는 이름과 읽는 이름이 어긋난다. 접미사는 가장 긴 것이 먼저 맞는다(`.scene.xml` 이 `.scene` 보다 먼저).
     */
    struct SW_API AssetCookPath
    {
        /**
         * @brief 경로의 쿠킹본 경로입니다. 쿠킹하는 것이 아니면 빈 글입니다.
         * @details `.scene.xml` → `.scene.bin`, `.prefab.xml` · `.prefab.json` → `.prefab.bin`. 이미 쿠킹본이면 그대로, 확장자 없는
         *          `.scene` · `.prefab` 은 `.bin` 을 붙입니다. 대소문자는 가리지 않습니다.
         */
        static string toCookedPath( string_view path );
        /** @brief 쿠커가 쿠킹하는 저작 소스(`.scene.xml` · `.prefab.xml` · `.prefab.json`)이면 true 입니다. */
        static bool isCookableSource( string_view path );
        /** @brief 쿠커가 만드는 쿠킹본 이름(`.scene.bin` · `.prefab.bin`)이면 true 입니다. 소스 트리에서 보이면 낡은 잔재입니다(팩 쿠커가 넣지 않는다). */
        static bool isCookedOutput( string_view path );
        /**
         * @brief @p path 가 @p type(씬 · 프리팹 등)의 저작 소스이면 true 입니다. 쿠킹하지 않는 종류는 늘 false 입니다.
         * @details 에디터가 "이것은 씬 · 프리팹인가" 를 이것으로 묻는다(`EditorAssetTypeRegistry`). 에디터가 접미사 표를 따로 들면
         *          쿠커가 쿠킹하지 않는 이름(`_scene.xml` · 확장자 없는 `.scene` · 쿠킹본 `.prefab.bin` 등)도 씬 · 프리팹으로 열고 저장해,
         *          에디터에서는 되는데 배포본에는 없게 된다.
         */
        static bool isCookableSource( string_view path, const AssetTypeDef& type );
        /** @brief @p type 의 저작 소스 접미사를 정의 순서대로 @p outListSuffix 에 더합니다(정적 문자열이라 들고 있어도 됩니다). */
        static void appendSourceSuffixes( const AssetTypeDef& type, vector<string_view>& outListSuffix );
        /**
         * @brief @p path 를 @p type 의 저작 소스 이름으로 만듭니다 — 이미 그렇다면 그대로, 쿠킹본 · 확장자 없는 이름(`.scene.bin` · `.scene`)은 원본 접미사로
         *        바꾸고, 그 밖에는 원본 접미사(`.scene.xml`, 정의의 첫 저작 소스 접미사)를 붙입니다(끝의 `.xml` 은 겹치지 않게 그 자리에서).
         *        쿠킹하지 않는 종류면 그대로입니다.
         * @details 씬을 쓰는 자리(`SceneManager::saveActiveScene`)가 이것을 지난다 — 저장 대화상자에 "level" 을 적거나 `.scene.bin` 을 다시
         *          저장해도 쿠커가 쿠킹하는 이름이 된다.
         */
        static string toSourcePath( string_view path, const AssetTypeDef& type );
        /**
         * @brief 리소스 id @p relativePath 가 활성 게임(@p activePackRoot, `game/<팩>`)이 아닌 게임 팩 아래면 true 입니다. 활성 게임이 비면(모든 게임) false 입니다.
         * @details 다른 게임의 팩은 그 게임 모듈의 컴포넌트를 쓴다 - 쿠커(씬 · 내비 · VAT)가 같은 규칙으로 건너뛴다(예전에는 쿠커마다 사본이 있었다).
         */
        static bool isInInactiveGamePack( string_view relativePath, string_view activePackRoot );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief XML 에셋의 판(`formatVersion`) 읽기 · 쓰기와 정확 일치 검사입니다. 상태가 없어 서비스 없이 부를 수 있습니다.
     * @details 판이 다르면 이관하지 않고 거절한다. 형식이 바뀌면 저장소의 데이터를 다시 쓰고 그 종류의 `AssetTypeDef::_formatVersion` 을 올린다.
     */
    class SW_API AssetXMLFormat
    {
    public:
        static constexpr auto kXMLAttrName = "formatVersion";
        /** @brief 태그가 없는 파일의 판(세대 0)입니다. */
        static constexpr AssetFormatVersion kUnversioned = 0;

        /** @brief 루트의 formatVersion 을 읽습니다. 태그가 없으면 `kUnversioned`, 읽지 못하거나 담기지 않으면 가장 큰 판입니다(어느 쪽이든 현재 판과 달라 거절됩니다). */
        static AssetFormatVersion readXMLVersion( XMLNode root );
        /** @brief 루트에 formatVersion 을 씁니다. */
        static void writeXMLVersion( XMLNode root, AssetFormatVersion version );

        /**
         * @brief 루트의 판이 @p type 의 현재 판(`AssetTypeDef::_formatVersion`)과 **같을 때만** true 입니다.
         * @details 다르면(옛 판 · 새 판 · 태그 없음 · 읽을 수 없는 값) 파일 종류와 두 판을 적어 오류를 알리고 false 입니다. 범위로 받지 않는다.
         */
        [[nodiscard]] static bool isCurrentXMLVersion( const AssetTypeDef& type, XMLNode root );
    };
} // namespace sw
