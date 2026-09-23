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
        static constexpr AssetFormatVersion kRenderPipeline   = 0;
        static constexpr AssetFormatVersion kRenderPass       = 0;
        static constexpr AssetFormatVersion kPrefab           = 0;
        static constexpr AssetFormatVersion kScene            = 0;
    };

    using XmlAssetMigrator = bool ( * )( XmlDocument& doc, XmlNode& root );

    /** @brief 런타임이 쿠킹된 바이너리를 쓰는지 반환합니다. Shipping 은 쿠킹된 바이너리를, Dev 는 XML 저작본을 로드합니다. */
    constexpr bool usesCookedBinaryAtRuntime() noexcept
    {
#if defined( SW_SHIPPING )
        return true;
#else
        return false;
#endif
    }

    /// @brief 에셋 종류별 formatVersion 과 N→N+1 migrator 등록부입니다.
    class SW_API AssetFormatRegistry
    {
    public:
        static constexpr auto kXmlAttrName = "formatVersion";

        /** @brief 빈 등록부로 만듭니다. */
        AssetFormatRegistry() = default;

        /** @brief 엔진 내장 N→N+1 migrator 를 등록합니다. 멱등입니다(지금은 내장 migrator 가 없습니다). */
        void ensureBuiltins();

        /** @brief XML migrator 를 등록합니다. */
        void registerXmlMigrator( AssetKind kind, AssetFormatVersion fromVersion, XmlAssetMigrator migrator );

        /** @brief 루트의 formatVersion 을 읽습니다. */
        AssetFormatVersion readXmlVersion( XmlNode root ) const;
        /** @brief 루트에 formatVersion 을 씁니다. */
        void writeXmlVersion( XmlNode root, AssetFormatVersion version ) const;

        /**
         * @brief 버전을 추정합니다. `formatVersion` 이 없으면 세대 0 으로 봅니다.
         */
        AssetFormatVersion inferXmlVersion( AssetKind kind, XmlNode root ) const;

        /**
         * @brief migrator 를 `currentVersion` 까지 차례로 실행하고, 성공하면 `formatVersion` 을 찍습니다.
         * @return 파일이 지원하는 것보다 새것이거나, 필요한 migrator 가 없거나 실패하면 false 입니다.
         */
        bool upgradeXml( AssetKind kind, XmlDocument& doc, XmlNode& root, AssetFormatVersion currentVersion, AssetFormatVersion* pOutSourceVersion = nullptr );

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
