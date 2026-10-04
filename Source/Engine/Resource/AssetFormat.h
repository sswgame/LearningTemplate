/**
 * @file AssetFormat.h
 * @brief XML 에셋 스키마 버전과 N→N+1 이관 등록부입니다.
 * @details 버전은 루트 속성 `formatVersion` 에 적습니다(XML 선언이 아닙니다). 없으면 세대 0(현재 기준)입니다.
 *          스키마가 깨지면 종류별 상수를 올리고 registerXmlMigrator 로 N→N+1 을 등록합니다.
 *          저작 기본 포맷은 XML 입니다. JSON 은 도구 · 설정을 주고받는 데 씁니다.
 *          Shipping 런타임은 쿠킹된 바이너리(PFB2 등)를 로드합니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    using AssetFormatVersion = uint32;

    /// @brief 에셋 종류입니다.
    enum class AssetKind : uint32
    {
        Material = 0,
        MaterialInstance,
        RenderPipeline,
        RenderPass,
        Prefab,
        Scene,
        Count
    };

    /** @brief 에셋 종류별 현재 디스크 스키마입니다(저장 형태가 호환되지 않게 바뀌면 올립니다). */
    struct AssetFormatVersions
    {
        static constexpr AssetFormatVersion kUnversioned      = 0; ///< 태그 없음 = 세대 0
        static constexpr AssetFormatVersion kMaterial         = 0;
        static constexpr AssetFormatVersion kMaterialInstance = 0;
        static constexpr AssetFormatVersion kPrefab           = 0;
        /// 1: 프리팹 엔티티가 프리팹 경로 + 덮어쓴 것(`<PrefabOverrides>`)을 싣는다. 판이 0 인(또는 판이 없는) 씬 파일은 읽지 않는다.
        static constexpr AssetFormatVersion kScene = 1;
    };
} // namespace sw

namespace sw
{
    using XmlAssetMigrator = bool ( * )( XmlDocument& doc, XmlNode& root );

    /**
     * @brief 저작 소스 경로와 쿠킹본 경로 사이의 이름 규칙입니다. 로더(씬 · 프리팹)와 쿠커가 모두 여기를 지납니다.
     * @details 규칙은 여기 한 벌입니다 — 씬 로더 · 씬 쿠커 · 프리팹 로더 · 프리팹 쿠커가 각자 확장자를 바꾸면 쿠킹하는 이름과 읽는 이름이 어긋난다.
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
        /**
         * @brief @p path 가 @p kind(`Scene` · `Prefab`)의 저작 소스이면 true 입니다. 다른 종류는 늘 false 입니다.
         * @details 에디터가 "이것은 씬 · 프리팹인가" 를 이것으로 묻는다(`EditorAssetTypeRegistry`). 에디터가 접미사 표를 따로 들면
         *          쿠커가 쿠킹하지 않는 이름(`_scene.xml` · 확장자 없는 `.scene` · 쿠킹본 `.prefab.bin` 등)도 씬 · 프리팹으로 열고 저장해,
         *          에디터에서는 되는데 배포본에는 없게 된다.
         */
        static bool isCookableSource( string_view path, AssetKind kind );
        /** @brief @p kind 의 저작 소스 접미사를 표 순서대로 @p outListSuffix 에 더합니다(정적 문자열이라 들고 있어도 됩니다). */
        static void appendSourceSuffixes( AssetKind kind, vector<string_view>& outListSuffix );
        /**
         * @brief @p path 를 @p kind 의 저작 소스 이름으로 만듭니다 — 이미 그렇다면 그대로, 쿠킹본 · 확장자 없는 이름(`.scene.bin` · `.scene`)은 정본 접미사로
         *        바꾸고, 그 밖에는 정본 접미사(`.scene.xml`)를 붙입니다(끝의 `.xml` 은 겹치지 않게 그 자리에서). 씬 · 프리팹이 아니면 그대로입니다.
         * @details 씬을 쓰는 자리(`SceneManager::saveActiveScene`)가 이것을 지난다 — 저장 대화상자에 "level" 을 적거나 `.scene.bin` 을 다시
         *          저장해도 쿠커가 쿠킹하는 이름이 된다.
         */
        static string toSourcePath( string_view path, AssetKind kind );
    };
} // namespace sw

namespace sw
{
    /// @brief 에셋 종류별 formatVersion 과 N→N+1 migrator 등록부입니다.
    class SW_API AssetFormatRegistry
    {
    public:
        static constexpr auto kXmlAttrName = "formatVersion";

        /** @brief 빈 등록부로 만듭니다. */
        AssetFormatRegistry() = default;

        /** @brief 엔진 내장 N→N+1 migrator 를 등록합니다. 멱등입니다. 지금은 등록할 것이 없습니다. */
        void ensureBuiltins();

        /** @brief XML migrator 를 등록합니다. */
        void registerXmlMigrator( AssetKind kind, AssetFormatVersion fromVersion, XmlAssetMigrator migrator );

        /** @brief 루트의 formatVersion 을 읽습니다. */
        static AssetFormatVersion readXmlVersion( XmlNode root );
        /** @brief 루트에 formatVersion 을 씁니다(등록부 상태를 쓰지 않아 서비스 없이 부를 수 있습니다). */
        static void writeXmlVersion( XmlNode root, AssetFormatVersion version );

        /**
         * @brief 버전을 추정합니다. `formatVersion` 이 없으면 세대 0 으로 봅니다.
         */
        AssetFormatVersion inferXmlVersion( AssetKind kind, XmlNode root ) const;

        /**
         * @brief migrator 를 `currentVersion` 까지 차례로 실행하고, 성공하면 `formatVersion` 을 찍습니다.
         * @return 파일이 지원하는 것보다 새것이거나, 필요한 migrator 가 없거나 실패하면 false 입니다.
         */
        bool upgradeXml( AssetKind kind, XmlDocument& doc, XmlNode& root, AssetFormatVersion currentVersion, AssetFormatVersion* pOutSourceVersion = nullptr );

        /**
         * @brief 엔진 서비스가 묶여 있으면 `AssetManager` 의 등록부로, 아니면(단독 도구 · 테스트) 내장 migrator 만 든 임시 등록부로 `upgradeXml` 을 부릅니다.
         * @details 에셋 로더(씬 · 프리팹)는 이것을 부릅니다 — 서비스 없이 `getAssetManager()` 를 부르면 assert 입니다. 등록해 둔 migrator 는 서비스가
         *          묶여 있을 때만 쓰이고, 지원하는 것보다 새 파일은 어느 쪽이든 거절합니다.
         */
        static bool upgradeXmlWithActiveRegistry( AssetKind kind, XmlDocument& doc, XmlNode& root, AssetFormatVersion currentVersion );

    private:
        /// @brief (종류, fromVersion) → migrator 조회 키입니다.
        struct MigratorKey
        {
            AssetKind          _kind{ AssetKind::Material };
            AssetFormatVersion _fromVersion{ 0 };

            /** @brief 두 키가 같으면 true 입니다. */
            bool operator==( const MigratorKey& other ) const
            {
                return _kind == other._kind && _fromVersion == other._fromVersion;
            }
        };

        /// @brief MigratorKey 해시입니다.
        struct MigratorKeyHash
        {
            /** @brief 키의 해시를 반환합니다. */
            size_t operator()( const MigratorKey& key ) const
            {
                return ( static_cast<size_t>( key._kind ) << 16 ) ^ static_cast<size_t>( key._fromVersion );
            }
        };

        unordered_map<MigratorKey, XmlAssetMigrator, MigratorKeyHash> _mapMigrator;
        bool                                                          _bBuiltins{ false };
    };
} // namespace sw
