/**
 * @file PrefabAsset.h
 * @brief 프리팹 에셋 로드/저장/스폰 (GameObject 템플릿 - XML, JSON, SCN/PFB 바이너리)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include <shared_mutex>

namespace sw
{
	class GameObject;
	class GameObjectManager;

	/// @brief 프리팹 에셋 (루트 GameObject 상태 템플릿)
	class SW_API PrefabAsset
	{
	public:
		/** @brief 빈 프리팹 (미로드). */
		PrefabAsset();

		/** @brief XML에서 프리팹을 로드합니다 (<Prefab formatVersion="0" name="...">). */
		bool loadFromXmlFile( string_view assetRelativePath );
		/** @brief JSON에서 프리팹을 로드합니다 (표준 JSON 또는 래퍼 JSON). */
		bool loadFromJsonFile( string_view assetRelativePath );
		/** @brief 바이너리에서 프리팹을 로드합니다. PFB2(쿠킹). */
		bool loadFromBinaryFile( string_view assetRelativePath );
		/** @brief XML로 저장합니다 (<Prefab formatVersion="0" name="...">). */
		bool saveToXmlFile( string_view assetRelativePath ) const;
		/** @brief JSON으로 저장합니다 ({ "formatVersion": 0, "name": "...", "GameObject": { ... } }). */
		bool saveToJsonFile( string_view assetRelativePath ) const;
		/** @brief Shipping cook: PFB2 (magic + version + name + state data). */
		bool saveToBinaryFile( string_view assetRelativePath ) const;
		/** @brief GameObject 상태에서 프리팹을 채웁니다. */
		void setFromGameObject( const GameObject* pGameObject );

		/** @brief 프리팹 이름을 반환합니다. */
		const string& getName() const { return _name; }
		/** @brief 직렬화된 본문 상태 데이터(XML 또는 JSON)를 반환합니다. */
		const string& getStateData() const { return _stateData; }
		/** @brief 하위 호환성용 alias */
		const string& getXmlBody() const { return _stateData; }
		/** @brief 로드에 성공했으면 true. */
		bool isValid() const { return _bValid != 0; }
		/** @brief 상태 XML/JSON 안의 `.prefab` 경로를 수집합니다. */
		void collectReferencedPrefabPaths( vector<string>& outPathList ) const;

	private:
		string				   _name;
		string				   _stateData;
		uint8				   _bValid	 : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};

	/// @brief 프리팹 로드/스폰 캐시
	class SW_API PrefabManager
	{
	public:
		/** @brief 빈 프리팹 캐시. */
		PrefabManager() = default;
		/** @brief 캐시된 프리팹을 정리합니다. */
		~PrefabManager() = default;

		/** @brief 복사를 금지합니다. */
		PrefabManager( const PrefabManager& ) = delete;
		/** @brief 대입을 금지합니다. */
		PrefabManager& operator=( const PrefabManager& ) = delete;

		/** @brief Dev: XML/JSON 저작본 로드. Shipping: 쿠킹된 .prefab.bin만 로드. 캐시 키는 확장자 없는 정규화 경로. */
		PrefabAsset* loadPrefab( string_view assetRelativePath );
		/**
		 * @brief 프리팹을 스폰합니다. instanceDiff가 있으면 루트 GO TypeInfo에 적용합니다.
		 */
		GameObject* spawn( GameObjectManager* pGameObjectManager, string_view assetRelativePath,
						   const utf8* pInstanceName = nullptr, const uint8* pInstanceDiff = nullptr,
						   size_t instanceDiffSize = 0 );
		/** @brief 저작본을 PFB2 binary로 쿠킹합니다. */
		bool cookPrefabToBinary( string_view sourceRelativePath, string_view binRelativePath );

	private:
		mutable std::shared_mutex					   _mapCacheMutex;
		unordered_map<string, unique_ptr<PrefabAsset>> _mapCache;
	};
} // namespace sw
