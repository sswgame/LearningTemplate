#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Serialization/Object/ObjectDiffSerializer.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Resource/AssetFormat.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{

	namespace
	{


		constexpr const char* kRoot			   = "Prefab";
		constexpr const char* kName			   = "name";
		constexpr const char* kGameObjectState  = "GameObjectState";
		constexpr const char* kDefaultInstance  = "PrefabInstance";
		constexpr uint32	  kPrefabBinMagic2  = 0x50464232u; // 'PFB2'
		constexpr uint32	  kPrefabBinVersion = 0;


		string makePrefabCacheKey( string_view assetRelativePath )
		{
			string key = FileUtil::normalizePath( assetRelativePath );
			if ( FileUtil::hasExtension( key, ".bin" ) )
				key.resize( key.size() - 4 );
			else if ( FileUtil::hasExtension( key, ".xml" ) )
				key.resize( key.size() - 4 );
			else if ( FileUtil::hasExtension( key, ".json" ) )
				key.resize( key.size() - 5 );
			return key;
		}

		bool upgradePrefabXmlBody( string& xmlBody )
		{
			if ( xmlBody.empty() )
				return false;

			string wrapped = "<Prefab>";
			wrapped += xmlBody;
			wrapped += "</Prefab>";
			XmlDocument wrapDoc;
			if ( wrapDoc.parse( wrapped ) == false )
				return false;
			XmlNode wrapRoot = wrapDoc.root( kRoot );
			if ( wrapRoot.isValid() == false )
				return false;
			if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::Prefab, wrapDoc, wrapRoot,
																				   AssetFormatVersions::kPrefab ) == false )
				return false;
			XmlNode bodyNode = wrapRoot.child( kGameObjectState );
			if ( bodyNode.isValid() == false )
				return false;
			xmlBody = bodyNode.toString();
			return true;
		}


	} // namespace

	PrefabAsset::PrefabAsset()
		: _name{}
		, _xmlBody{}
		, _bValid{ 0 }
		, _reserved{ 0 } {}

	namespace
	{
		static bool readU32Val( const vector<uint8>& listBlob, size_t& offset, uint32& outValue )
		{
			if ( offset + 4 > listBlob.size() )
				return false;
			outValue = static_cast<uint32>( listBlob[offset] ) | ( static_cast<uint32>( listBlob[offset + 1] ) << 8 ) |
					   ( static_cast<uint32>( listBlob[offset + 2] ) << 16 ) | ( static_cast<uint32>( listBlob[offset + 3] ) << 24 );
			offset += 4;
			return true;
		}

		static bool readStrVal( const vector<uint8>& listBlob, size_t& offset, string& outString )
		{
			uint32 len{ 0 };
			if ( readU32Val( listBlob, offset, len ) == false || offset + len > listBlob.size() )
				return false;
			outString.assign( reinterpret_cast<const utf8*>( listBlob.data() + offset ), len );
			offset += len;
			return true;
		}

		static void appendU32Val( vector<uint8>& listBlob, uint32 value )
		{
			listBlob.push_back( static_cast<uint8>( value & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 8 ) & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 16 ) & 0xFFu ) );
			listBlob.push_back( static_cast<uint8>( ( value >> 24 ) & 0xFFu ) );
		}

		static void appendStrVal( vector<uint8>& listBlob, string_view text )
		{
			appendU32Val( listBlob, static_cast<uint32>( text.size() ) );
			listBlob.insert( listBlob.end(), text.begin(), text.end() );
		}
	} // namespace

	bool PrefabAsset::loadFromXmlFile( string_view assetRelativePath )
	{
		_bValid		   = 0;
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::fileExists( absPath ) == false )
		{
			SW_LOG_ERROR( "[PrefabAsset] Not found: %#", absPath );
			return false;
		}

		vector<uint8> listFileData;
		if ( FileUtil::readFile( absPath, listFileData ) == false || listFileData.empty() )
			return false;

		string		xmlStr( reinterpret_cast<const utf8*>( listFileData.data() ), listFileData.size() );
		XmlDocument doc;
		if ( doc.parse( xmlStr ) == false )
		{
			SW_LOG_ERROR( "[PrefabAsset] XML parse failed: %#", absPath );
			return false;
		}

		XmlNode root = doc.root( kRoot );
		if ( root.isValid() == false )
		{
			SW_LOG_ERROR( "[PrefabAsset] Missing <Prefab>: %#", absPath );
			return false;
		}

		if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::Prefab, doc, root, AssetFormatVersions::kPrefab ) ==
			 false )
		{
			SW_LOG_ERROR( "[PrefabAsset] formatVersion upgrade failed: %#", absPath );
			return false;
		}

		const utf8* pNameAttr = root.attr( kName );
		if ( pNameAttr != nullptr )
			_name = pNameAttr;
		else
		{
			const utf8* pNameNode = root.childText( kName );
			if ( pNameNode != nullptr )
				_name = pNameNode;
		}

		XmlNode bodyNode = root.child( kGameObjectState );
		if ( bodyNode.isValid() == false )
		{
			SW_LOG_ERROR( "[PrefabAsset] Missing <GameObjectState>: %#", absPath );
			return false;
		}
		_xmlBody = bodyNode.toString();

		_bValid = 1;
		SW_LOG_INFO( "[PrefabAsset] Loaded '%#' from %#", _name, absPath );
		return true;
	}

	bool PrefabAsset::loadFromJsonFile( string_view assetRelativePath )
	{
		_bValid		   = 0;
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::fileExists( absPath ) == false )
		{
			SW_LOG_ERROR( "[PrefabAsset] Not found: %#", absPath );
			return false;
		}

		vector<uint8> listFileData;
		if ( FileUtil::readFile( absPath, listFileData ) == false || listFileData.empty() )
			return false;

		const string json( listFileData.begin(), listFileData.end() );
		JsonDocument doc;
		if ( doc.parse( json ) == false )
			return false;
		_name	 = doc.root().get( "Name" ).asString();
		_xmlBody = json;

		if ( _name.empty() )
		{
			SW_LOG_ERROR( "[PrefabAsset] Missing Name in JSON: %#", absPath );
			return false;
		}

		_bValid = 1;
		return true;
	}

	bool PrefabAsset::loadFromBinaryFile( string_view assetRelativePath )
	{
		_bValid		   = 0;
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		vector<uint8> listBlob;
		if ( FileUtil::readFile( absPath, listBlob ) == false || listBlob.size() < 12 )
		{
			SW_LOG_ERROR( "[PrefabAsset] Binary read failed or too small: %#", absPath );
			return false;
		}

		size_t offset{ 0 };

		uint32 magic{ 0 };
		if ( readU32Val( listBlob, offset, magic ) == false || magic != kPrefabBinMagic2 )
		{
			SW_LOG_ERROR( "[PrefabAsset] Bad binary magic: %#", absPath );
			return false;
		}
		{
			uint32 version{ 0 };
			if ( readU32Val( listBlob, offset, version ) == false )
			{
				SW_LOG_ERROR( "[PrefabAsset] Binary version truncated: %#", absPath );
				return false;
			}
			if ( version > kPrefabBinVersion )
			{
				SW_LOG_ERROR( "[PrefabAsset] Unsupported binary version %# in %#", version, absPath );
				return false;
			}
		}
		if ( readStrVal( listBlob, offset, _name ) == false || readStrVal( listBlob, offset, _xmlBody ) == false )
		{
			SW_LOG_ERROR( "[PrefabAsset] Binary payload truncated: %#", absPath );
			return false;
		}

		if ( upgradePrefabXmlBody( _xmlBody ) == false )
		{
			SW_LOG_ERROR( "[PrefabAsset] formatVersion upgrade failed: %#", absPath );
			return false;
		}

		_bValid = 1;
		SW_LOG_INFO( "[PrefabAsset] Loaded '%#' from binary %#", _name, absPath );
		return true;
	}

	bool PrefabAsset::saveToXmlFile( string_view assetRelativePath ) const
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		// _xmlBody 는 완전한 <GameObjectState>...</GameObjectState> 조각.
		XmlDocument doc;
		XmlNode		root = doc.appendRoot( kRoot );
		engine::getResourceManager().getAssetFormatRegistry().writeXmlVersion( root, AssetFormatVersions::kPrefab );
		root.setAttr( kName, _name.c_str() );
		string shell = doc.saveToString();
		while ( shell.empty() == false && ( shell.back() == '\n' || shell.back() == '\r' || shell.back() == ' ' || shell.back() == '\t' ) )
		{
			shell.pop_back();
		}

		string				  xmlStr;
		constexpr string_view kClose = "</Prefab>";
		if ( shell.size() >= kClose.size() && shell.compare( shell.size() - kClose.size(), kClose.size(), kClose.data() ) == 0 )
		{
			xmlStr.reserve( shell.size() + _xmlBody.size() );
			xmlStr.append( shell.data(), shell.size() - kClose.size() );
		}
		else if ( shell.size() >= 2 && shell.compare( shell.size() - 2, 2, "/>" ) == 0 )
		{
			size_t trimLen = shell.size() - 2;
			while ( trimLen > 0 && shell[trimLen - 1] == ' ' )
			{
				trimLen--;
			}
			xmlStr.reserve( trimLen + 1 + _xmlBody.size() + kClose.size() );
			xmlStr.append( shell.data(), trimLen );
			xmlStr += ">";
		}
		else
			return false;

		xmlStr += _xmlBody;
		xmlStr += "</Prefab>";

		const bool writeOk = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( xmlStr.data() ),
												  xmlStr.size() );
		if ( writeOk )
			SW_LOG_INFO( "[PrefabAsset] Saved '%#' -> %#", _name, absPath );
		return writeOk;
	}

	bool PrefabAsset::saveToJsonFile( string_view assetRelativePath ) const
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		JsonDocument doc;
		JsonValue	 root = doc.makeObject();
		root.set( "Name" ).setString( _name );
		root.set( "xmlBody" ).setString( _xmlBody );
		const string json = doc.dump( 1 );

		const bool writeOk = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( json.data() ),
												  json.size() );
		if ( writeOk )
			SW_LOG_INFO( "[PrefabAsset] Saved '%#' JSON %#", _name, absPath );
		return writeOk;
	}

	bool PrefabAsset::saveToBinaryFile( string_view assetRelativePath ) const
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		vector<uint8> listBlob;

		appendU32Val( listBlob, kPrefabBinMagic2 );
		appendU32Val( listBlob, kPrefabBinVersion );
		appendStrVal( listBlob, _name );
		appendStrVal( listBlob, _xmlBody );
		return FileUtil::writeFile( absPath, listBlob.data(), listBlob.size() );
	}

	void PrefabAsset::setFromGameObject( const GameObject* pGameObject )
	{
		if ( pGameObject == nullptr )
		{
			_bValid = 0;
			return;
		}
		_name	 = pGameObject->getName().c_str();
		_xmlBody = ObjectStateSerializer::saveToXmlString( pGameObject );
		_bValid	 = _xmlBody.empty() == false ? 1 : 0;
	}

	PrefabAsset* PrefabManager::loadPrefab( string_view assetRelativePath )
	{
		const string cacheKey = makePrefabCacheKey( assetRelativePath );
		{
			std::shared_lock<std::shared_mutex> readLock{ _mapCacheMutex };
			const auto							cacheIt = _mapCache.find( cacheKey );
			if ( cacheIt != _mapCache.end() )
				return cacheIt->second.get();
		}

		unique_ptr<PrefabAsset> asset = make_unique<PrefabAsset>();

		string	   binPath( assetRelativePath );
		const bool bJson = FileUtil::hasExtension( binPath, ".json" );
		const bool bXml	 = FileUtil::hasExtension( binPath, ".xml" );
		if ( bXml )
			binPath.replace( binPath.size() - 4, 4, ".bin" );
		else if ( bJson )
			binPath.replace( binPath.size() - 5, 5, ".bin" );
		else if ( binPath.find( ".prefab" ) != string::npos )
			binPath += ".bin";

#if defined( SW_SHIPPING )
		if ( asset->loadFromBinaryFile( binPath ) == false )
		{
			SW_LOG_ERROR( "[PrefabManager] Shipping requires cooked binary: %#", binPath );
			return nullptr;
		}
#else
		if ( bJson )
		{
			if ( asset->loadFromJsonFile( assetRelativePath ) == false && asset->loadFromBinaryFile( binPath ) == false )
				return nullptr;
		}
		else
		{
			if ( asset->loadFromXmlFile( assetRelativePath ) == false && asset->loadFromBinaryFile( binPath ) == false )
				return nullptr;
		}
#endif

		std::unique_lock<std::shared_mutex> writeLock{ _mapCacheMutex };
		const auto							cacheIt = _mapCache.find( cacheKey );
		if ( cacheIt != _mapCache.end() )
			return cacheIt->second.get();

		PrefabAsset* pCached = asset.get();
		_mapCache.emplace( cacheKey, std::move( asset ) );
		return pCached;
	}

	GameObject* PrefabManager::spawn( GameObjectManager* pGameObjectManager, string_view assetRelativePath, const utf8* pInstanceName,
									  const uint8* pInstanceDiff, size_t instanceDiffSize )
	{
		if ( pGameObjectManager == nullptr )
			return nullptr;

		const hashed_string				   pathKey( assetRelativePath.data(), static_cast<uint32>( assetRelativePath.size() ) );
		thread_local vector<hashed_string> t_listSpawnStack;

		if ( std::find( t_listSpawnStack.begin(), t_listSpawnStack.end(), pathKey ) != t_listSpawnStack.end() )
		{
			SW_LOG_ERROR( "[PrefabManager] Circular prefab reference detected for '%#' — spawn aborted to prevent recursion overflow",
						  assetRelativePath );
			return nullptr;
		}

		t_listSpawnStack.push_back( pathKey );
		struct StackGuard
		{
			vector<hashed_string>& _stack;
			~StackGuard()
			{
				_stack.pop_back();
			}
		} guard{ t_listSpawnStack };

		PrefabAsset* pAsset = loadPrefab( assetRelativePath );
		if ( pAsset == nullptr || pAsset->isValid() == false )
			return nullptr;

		const utf8* pInstanceNameUtf8 = pInstanceName != nullptr ? pInstanceName : pAsset->getName().c_str();
		if ( pInstanceNameUtf8 == nullptr || pInstanceNameUtf8[0] == '\0' )
			pInstanceNameUtf8 = kDefaultInstance;

		GameObject* pGameObject = pGameObjectManager->createGameObject( hashed_string( pInstanceNameUtf8 ) );
		if ( pGameObject == nullptr )
			return nullptr;

		if ( pAsset->getXmlBody().empty() == false )
		{
			bool   bLoadSuccess{ false };
			string bodyStr = StringUtil::trim( pAsset->getXmlBody().c_str() );
			if ( bodyStr.empty() == false && bodyStr.front() == '{' )
				bLoadSuccess = ObjectStateSerializer::loadFromJsonString( pGameObject, pAsset->getXmlBody() );
			else if ( bodyStr.empty() == false )
				bLoadSuccess = ObjectStateSerializer::loadFromXmlString( pGameObject, pAsset->getXmlBody() );
			else
				bLoadSuccess = true;

			if ( bLoadSuccess == false )
			{
				SW_LOG_ERROR( "[PrefabManager] ObjectState apply failed for '%#' — spawn aborted", pInstanceNameUtf8 );
				pGameObjectManager->destroyObject( pGameObject );
				return nullptr;
			}
			pGameObject->setName( hashed_string( pInstanceNameUtf8 ) );
		}

		if ( pInstanceDiff != nullptr && instanceDiffSize > 0 )
		{
			const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
			if ( pTypeInfo != nullptr )
			{
				if ( ObjectDiffSerializer::deserializeDiff( pGameObject, *pTypeInfo, pInstanceDiff, instanceDiffSize ) == false )
				{
					SW_LOG_ERROR( "[PrefabManager] Instance diff apply failed for '%#' — spawn aborted", pInstanceNameUtf8 );
					pGameObjectManager->destroyObject( pGameObject );
					return nullptr;
				}
			}
		}

		return pGameObject;
	}

	bool PrefabManager::cookPrefabToBinary( string_view sourceRelativePath, string_view binRelativePath )
	{
		PrefabAsset asset;
		const bool	bLoaded = FileUtil::hasExtension( sourceRelativePath, ".json" )
								? asset.loadFromJsonFile( sourceRelativePath )
								: asset.loadFromXmlFile( sourceRelativePath );
		if ( bLoaded == false )
			return false;
		return asset.saveToBinaryFile( binRelativePath );
	}
} // namespace sw
