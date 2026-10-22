/**
 * @file AssetFormat.h
 * @brief XML 에셋 스키마 판의 N→N+1 이관 레지스트리와 저작 소스 · 쿠킹본 경로 규칙입니다. 둘 다 에셋 종류 표(`AssetTypeRegistry`)에서 파생합니다.
 * @details 판은 루트 속성 `formatVersion` 에 적습니다(XML 선언이 아닙니다). 없으면 세대 0(현재 기준)입니다.
 *          스키마가 깨지면 그 종류의 정의(`AssetTypeDef::_formatVersion`)를 올리고 registerXMLMigrator 로 N→N+1 을 등록합니다 — 판 상수 표를 따로 두지 않는다.
 *          저작 기본 포맷은 XML 입니다. JSON 은 도구 · 설정을 주고받는 데 쓰고, JSON 접미사는 쿠킹 입력으로만 에셋 종류가 된다(`AssetTypeRegistry::isSuffixFormatAllowed`).
 *          Shipping 런타임은 쿠킹된 바이너리(PFB2 등)를 로드합니다.
 */
#pragma once
#include "Core/Common/HashUtil.h"

#include "Engine/Common/AssetTypeRegistry.h"
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    using XMLAssetMigrator = bool ( * )( XMLDocument& doc, XMLNode& root );

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
    };
} // namespace sw

namespace sw
{
    /// @brief 에셋 종류별 N→N+1 migrator 레지스트리입니다. 키는 종류 이름(`AssetTypeDef::_name`)과 출발 판입니다.
    class SW_API AssetFormatRegistry
    {
    public:
        static constexpr auto kXMLAttrName = "formatVersion";
        /** @brief 태그가 없는 파일의 판(세대 0)입니다. */
        static constexpr AssetFormatVersion kUnversioned = 0;

        /** @brief 빈 레지스트리로 만듭니다. */
        AssetFormatRegistry() = default;

        /** @brief 엔진 내장 N→N+1 migrator 를 등록합니다. 멱등입니다. 지금은 등록할 것이 없습니다. */
        void ensureBuiltins();

        /** @brief @p type 의 XML migrator(@p fromVersion → +1)를 등록합니다. */
        void registerXMLMigrator( const AssetTypeDef& type, AssetFormatVersion fromVersion, XMLAssetMigrator migrator );

        /** @brief 루트의 formatVersion 을 읽습니다. */
        static AssetFormatVersion readXMLVersion( XMLNode root );
        /** @brief 루트에 formatVersion 을 씁니다(레지스트리 상태를 쓰지 않아 서비스 없이 부를 수 있습니다). */
        static void writeXMLVersion( XMLNode root, AssetFormatVersion version );

        /**
         * @brief 판을 추정합니다. `formatVersion` 이 없으면 세대 0 으로 봅니다.
         */
        AssetFormatVersion inferXMLVersion( const AssetTypeDef& type, XMLNode root ) const;

        /**
         * @brief migrator 를 그 종류의 현재 판(`AssetTypeDef::_formatVersion`)까지 차례로 실행하고, 성공하면 `formatVersion` 을 찍습니다.
         * @return 파일이 지원하는 것보다 새것이거나, 필요한 migrator 가 없거나 실패하면 false 입니다.
         */
        bool upgradeXML( const AssetTypeDef& type, XMLDocument& doc, XMLNode& root, AssetFormatVersion* pOutSourceVersion = nullptr );

        /**
         * @brief 엔진 서비스가 묶여 있으면 `AssetManager` 의 레지스트리로, 아니면(단독 도구 · 테스트) 내장 migrator 만 든 임시 레지스트리로 `upgradeXML` 을 부릅니다.
         * @details 에셋 로더(씬 · 프리팹)는 이것을 부릅니다 — 서비스 없이 `getAssetManager()` 를 부르면 assert 입니다. 등록해 둔 migrator 는 서비스가
         *          묶여 있을 때만 쓰이고, 지원하는 것보다 새 파일은 어느 쪽이든 거절합니다.
         */
        static bool upgradeXMLWithActiveRegistry( const AssetTypeDef& type, XMLDocument& doc, XMLNode& root );

    private:
        /// @brief (종류 이름, fromVersion) → migrator 조회 키입니다.
        struct MigratorKey
        {
            string_view        _typeName;
            AssetFormatVersion _fromVersion{ 0 };

            /** @brief 두 키가 같으면 true 입니다. */
            bool operator==( const MigratorKey& other ) const { return _fromVersion == other._fromVersion && _typeName == other._typeName; }

            /** @brief 키의 해시입니다(`unordered_map` 의 기본 해시 `DefaultHash` 가 부릅니다). */
            [[nodiscard]] uint64 computeHash() const { return HashUtil::computeStringHash64( _typeName, false, static_cast<uint64>( _fromVersion ) ); }
        };

        unordered_map<MigratorKey, XMLAssetMigrator> _mapMigrator;
        bool                                         _bBuiltins{ false };
    };
} // namespace sw
