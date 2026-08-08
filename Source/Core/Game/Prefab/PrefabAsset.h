#pragma once
/**
 * @file PrefabAsset.h
 * @brief Prefab XML load/save (GameObject template)
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"

namespace sw
{
	class GameObject;
	class GameObjectManager;

	class SW_API PrefabAsset
	{
	public:
		PrefabAsset();

		bool loadFromXmlFile( const std::string& assetRelativePath );
		bool loadFromJsonFile( const std::string& assetRelativePath );
		bool saveToXmlFile( const std::string& assetRelativePath ) const;
		bool saveToJsonFile( const std::string& assetRelativePath ) const;

		/** @brief Shipping cook: magic + name + xml body as length-prefixed blob (.prefab.bin) */
		bool loadFromBinaryFile( const std::string& assetRelativePath );
		bool saveToBinaryFile( const std::string& assetRelativePath ) const;

		const std::string& getName() const { return _name; }
		const std::string& getXmlBody() const { return _xmlBody; }
		bool			   isValid() const { return _bValid != 0; }

		void setFromGameObject( const GameObject* gameObject );

	private:
		std::string _name;
		std::string _xmlBody;
		uint8		_bValid : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};

	class SW_API PrefabManager
	{
	public:
		PrefabManager();
		~PrefabManager() = default;

		PrefabManager( const PrefabManager& )			 = delete;
		PrefabManager& operator=( const PrefabManager& ) = delete;

		static PrefabManager& get();

		/** @brief Dev: .xml/.json; Shipping: prefer .prefab.bin beside path */
		PrefabAsset* loadPrefab( const std::string& assetRelativePath );
		GameObject*	 spawn( GameObjectManager* objects, const std::string& assetRelativePath, const char* instanceName = nullptr );
		/** @brief Cook .prefab.xml or .prefab.json → .prefab.bin (PFB1) */
		bool cookPrefabToBinary( const std::string& sourceRelativePath, const std::string& binRelativePath );

	private:
		std::unordered_map<std::string, std::unique_ptr<PrefabAsset>> _cache;
	};
} // namespace sw
