/**
 * @file AssetFormat.h
 * @brief XML 에셋 스키마 버전 + N→N+1 마이그레이션 레지스트리
 * @details 루트 attribute `formatVersion` (XML 선언이 아님). 없으면 세대 0 (현재 기준).
 *          스키마가 깨지면 종류별 상수를 올리고 registerXmlMigrator로 N→N+1을 등록합니다.
 *          저작 기본 포맷은 XML입니다. JSON은 도구/설정 interchange입니다.
 *          Shipping 런타임은 쿠킹된 binary(PFB2 등)를 로드합니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	using AssetFormatVersion = uint32;

	/// @brief 에셋 Kind 종류를 정의하는 열거형입니다.
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

	/** @brief 에셋 종류별 현재 디스크 스키마 (저장 형태가 비호환으로 바뀌면 올립니다). */
	struct AssetFormatVersions
	{
		static constexpr AssetFormatVersion kUnversioned	  = 0; ///< 태그 없음 = 세대 0
		static constexpr AssetFormatVersion kMaterial		  = 0;
		static constexpr AssetFormatVersion kMaterialInstance = 0;
		static constexpr AssetFormatVersion kRenderPipeline	  = 0;
		static constexpr AssetFormatVersion kRenderPass		  = 0;
		static constexpr AssetFormatVersion kPrefab			  = 0;
		static constexpr AssetFormatVersion kScene			  = 0;
	};

	using XmlAssetMigrator = bool ( * )( XmlDocument& doc, XmlNode& root );

	/** @brief Shipping 런타임은 쿠킹된 binary를 씁니다. Dev는 XML 저작본을 로드합니다. */
	constexpr bool usesCookedBinaryAtRuntime() noexcept
	{
#if defined( SW_SHIPPING )
		return true;
#else
		return false;
#endif
	}

	/// @brief 에셋 종류별 formatVersion과 N→N+1 migrator
	class SW_API AssetFormatRegistry
	{
	public:
		static constexpr auto kXmlAttrName = "formatVersion";

		/** @brief 빈 레지스트리. */
		AssetFormatRegistry() = default;

		/** @brief 엔진 내장 N→N+1 migrator를 멱등 등록합니다. */
		void ensureBuiltins();

		/** @brief XML migrator를 등록합니다. */
		void registerXmlMigrator( AssetKind kind, AssetFormatVersion fromVersion, XmlAssetMigrator migrator );

		/** @brief 루트의 formatVersion을 읽습니다. */
		AssetFormatVersion readXmlVersion( XmlNode root ) const;
		/** @brief 루트에 formatVersion을 씁니다. */
		void writeXmlVersion( XmlNode root, AssetFormatVersion version ) const;

		/**
		 * @brief `formatVersion`이 없으면 세대 0으로 봅니다.
		 */
		AssetFormatVersion inferXmlVersion( AssetKind kind, XmlNode root ) const;

		/**
		 * @brief migrator를 `currentVersion`까지 실행하고, 성공 시 `formatVersion`을 찍습니다.
		 * @return 파일이 지원보다 새거나 필요한 migrator가 없/실패하면 false.
		 */
		bool upgradeXml( AssetKind kind, XmlDocument& doc, XmlNode& root, AssetFormatVersion currentVersion, AssetFormatVersion* pOutSourceVersion = nullptr );

		/// @brief (종류, fromVersion) → migrator 키
		struct MigratorKey
		{
			AssetKind		   _kind = AssetKind::Material;
			AssetFormatVersion _fromVersion{ 0 };

			/** @brief 같으면 true를 반환합니다. */
			bool operator==( const MigratorKey& o ) const
			{
				return _kind == o._kind && _fromVersion == o._fromVersion;
			}
		};

		/// @brief MigratorKey 해시
		struct MigratorKeyHash
		{
			/** @brief 호출 연산자입니다. */
			size_t operator()( const MigratorKey& k ) const
			{
				return ( static_cast<size_t>( k._kind ) << 16 ) ^ static_cast<size_t>( k._fromVersion );
			}
		};

		unordered_map<MigratorKey, XmlAssetMigrator, MigratorKeyHash> _mapMigrator;
		bool														  _bBuiltins{ false };
	};
} // namespace sw
