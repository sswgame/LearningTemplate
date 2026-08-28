#include "pch.h"

#include "Engine/Object/Prefab/PrefabAsset.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Serialization/Object/ObjectDiffSerializer.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Resource/AssetFormat.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	SW_LOG_CALLER( "PrefabAsset" );

	namespace
	{

		constexpr const utf8* kRoot				= "Prefab";
		constexpr const utf8* kName				= "name";
		constexpr const utf8* kGameObject		= "GameObject";
		constexpr const utf8* kDefaultInstance	= "PrefabInstance";
		constexpr uint32	  kPrefabBinMagic2	= 0x50464232u; // 'PFB2'
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

		void collectPrefabRefsFromText( const utf8* pText, vector<string>& outPathList )
		{
			if ( pText == nullptr || pText[0] == '\0' )
				return;
			if ( StringUtil::stristr( pText, ".prefab" ) == nullptr )
				return;
			outPathList.push_back( pText );
		}

		void collectPrefabRefsFromXml( XmlNode node, vector<string>& outPathList )
		{
			if ( node.isValid() == false )
				return;
			collectPrefabRefsFromText( node.text(), outPathList );
			for ( XmlAttribute attr = node.firstAttr(); attr; attr = attr.next() )
				collectPrefabRefsFromText( attr.value(), outPathList );
			for ( XmlNode childNode = node.child(); childNode; childNode = childNode.next() )
				collectPrefabRefsFromXml( childNode, outPathList );
		}

		void collectPrefabRefsFromJson( JsonValue value, vector<string>& outPathList )
		{
			if ( value.isValid() == false )
				return;
			if ( value.isString() )
			{
				const string text = value.asString();
				collectPrefabRefsFromText( text.c_str(), outPathList );
				return;
			}
			if ( value.isObject() )
			{
				const vector<string> listKeys = value.memberNames();
				for ( const string& key : listKeys )
					collectPrefabRefsFromJson( value.get( key ), outPathList );
				return;
			}
			if ( value.isArray() )
			{
				const size_t count = value.size();
				for ( size_t index = 0; index < count; ++index )
					collectPrefabRefsFromJson( value.at( index ), outPathList );
			}
		}

		bool upgradePrefabXmlBody( string& xmlBody )
		{
			if ( xmlBody.empty() )
				return false;

			string bodyTrimmed = StringUtil::trim( xmlBody.c_str() );
			if ( bodyTrimmed.empty() == false && bodyTrimmed.front() == '{' )
				return true; // JSON 본문은 XML 업그레이드 대상이 아님

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
			XmlNode bodyNode = wrapRoot.child( kGameObject );
			if ( bodyNode.isValid() == false )
				return false;
			xmlBody = bodyNode.toString();
			return true;
		}

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

	PrefabAsset::PrefabAsset()
		: _name{}
		, _stateData{}
		, _bValid{ 0 }
		, _reserved{ 0 } {}

	bool PrefabAsset::loadFromXmlFile( string_view assetRelativePath )
	{
		_bValid = 0;
		string		absPath;
		XmlDocument doc;
		if ( doc.loadPath( assetRelativePath, &absPath ) == false )
		{
			SW_LOG_ERROR( "Not found: %#", assetRelativePath );
			return false;
		}

		XmlNode root = doc.root( kRoot );
		if ( root.isValid() )
		{
			if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::Prefab, doc, root, AssetFormatVersions::kPrefab ) ==
				 false )
			{
				SW_LOG_ERROR( "formatVersion upgrade failed: %#", absPath );
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

			XmlNode bodyNode = root.child( kGameObject );
			if ( bodyNode.isValid() )
				_stateData = bodyNode.toString();
			else
				_stateData = doc.saveToString();
		}
		else
		{
			// 루트가 <GameObject> 등 직접적인 XML인 경우 지원
			XmlNode goNode = doc.root( kGameObject );
			if ( goNode.isValid() )
			{
				const utf8* pNameAttr = goNode.attr( "_name" );
				if ( pNameAttr != nullptr )
					_name = pNameAttr;
				_stateData = doc.saveToString();
			}
			else
			{
				SW_LOG_ERROR( "Missing <Prefab> or <GameObject>: %#", absPath );
				return false;
			}
		}

		if ( _name.empty() )
			_name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );

		_bValid = 1;
		SW_LOG_INFO( "Loaded '%#' from %#", _name, absPath );
		return true;
	}

	bool PrefabAsset::loadFromJsonFile( string_view assetRelativePath )
	{
		_bValid = 0;
		string		 absPath;
		JsonDocument doc;
		if ( doc.loadPath( assetRelativePath, &absPath ) == false )
		{
			SW_LOG_ERROR( "Not found: %#", assetRelativePath );
			return false;
		}

		JsonValue root = doc.root();
		if ( root.has( "GameObject" ) )
		{
			_name = root.get( "name" ).asString();
			if ( _name.empty() )
				_name = root.get( "Name" ).asString();
			_stateData = root.get( "GameObject" ).dump( 0 );
		}
		else if ( root.has( "xmlBody" ) )
		{
			// 레거시 xmlBody 임베딩 호환
			_name = root.get( "name" ).asString();
			if ( _name.empty() )
				_name = root.get( "Name" ).asString();
			_stateData = root.get( "xmlBody" ).asString();
		}
		else
		{
			// 직접 GameObject JSON인 경우
			_name = root.get( "_name" ).asString();
			if ( _name.empty() )
				_name = root.get( "Name" ).asString();
			_stateData = doc.dump( 0 );
		}

		if ( _name.empty() )
			_name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );

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
			SW_LOG_ERROR( "Binary read failed or too small: %#", absPath );
			return false;
		}

		size_t offset{ 0 };

		uint32 magic{ 0 };
		if ( readU32Val( listBlob, offset, magic ) == false || magic != kPrefabBinMagic2 )
		{
			SW_LOG_ERROR( "Bad binary magic: %#", absPath );
			return false;
		}
		{
			uint32 version{ 0 };
			if ( readU32Val( listBlob, offset, version ) == false )
			{
				SW_LOG_ERROR( "Binary version truncated: %#", absPath );
				return false;
			}
			if ( version > kPrefabBinVersion )
			{
				SW_LOG_ERROR( "Unsupported binary version %# in %#", version, absPath );
				return false;
			}
		}
		if ( readStrVal( listBlob, offset, _name ) == false || readStrVal( listBlob, offset, _stateData ) == false )
		{
			SW_LOG_ERROR( "Binary payload truncated: %#", absPath );
			return false;
		}

		if ( upgradePrefabXmlBody( _stateData ) == false )
		{
			SW_LOG_ERROR( "formatVersion upgrade failed: %#", absPath );
			return false;
		}

		_bValid = 1;
		SW_LOG_INFO( "Loaded '%#' from binary %#", _name, absPath );
		return true;
	}

	bool PrefabAsset::saveToXmlFile( string_view assetRelativePath ) const
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		string xmlBody = _stateData;
		string trimmed = StringUtil::trim( xmlBody.c_str() );

		// JSON인 경우 GameObject를 통해 XML로 변환
		if ( trimmed.empty() == false && trimmed.front() == '{' )
		{
			GameObject tempObj( hashed_string( _name.c_str() ) );
			if ( ObjectStateSerializer::loadFromJsonString( &tempObj, trimmed ) )
				xmlBody = ObjectStateSerializer::saveToXmlString( &tempObj );
		}

		string xmlStr = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
		xmlStr += "<Prefab formatVersion=\"0\" name=\"";
		xmlStr += _name;
		xmlStr += "\">\n\t";
		xmlStr += xmlBody;
		xmlStr += "\n</Prefab>\n";

		const bool writeOk = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( xmlStr.data() ),
												  xmlStr.size() );
		if ( writeOk )
			SW_LOG_INFO( "Saved '%#' -> %#", _name, absPath );
		return writeOk;
	}

	bool PrefabAsset::saveToJsonFile( string_view assetRelativePath ) const
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		string jsonStr;
		string trimmed = StringUtil::trim( _stateData.c_str() );
		if ( trimmed.empty() == false && trimmed.front() == '{' )
		{
			jsonStr = _stateData;
		}
		else
		{
			// XML인 경우 GameObject를 통해 JSON으로 직렬화
			GameObject tempObj( hashed_string( _name.c_str() ) );
			if ( ObjectStateSerializer::loadFromXmlString( &tempObj, _stateData ) )
				jsonStr = ObjectStateSerializer::saveToJsonString( &tempObj );
			else
			{
				JsonDocument doc;
				JsonValue	 root = doc.makeObject();
				root.set( "formatVersion" ).setInt( 0 );
				root.set( "name" ).setString( _name );
				root.set( "xmlBody" ).setString( _stateData );
				jsonStr = doc.dump( 1 );
			}
		}

		const bool writeOk = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( jsonStr.data() ),
												  jsonStr.size() );
		if ( writeOk )
			SW_LOG_INFO( "Saved '%#' JSON %#", _name, absPath );
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
		appendStrVal( listBlob, _stateData );
		return FileUtil::writeFile( absPath, listBlob.data(), listBlob.size() );
	}

	void PrefabAsset::setFromGameObject( const GameObject* pGameObject )
	{
		if ( pGameObject == nullptr )
		{
			_bValid = 0;
			return;
		}
		_name	   = pGameObject->getName().c_str();
		_stateData = ObjectStateSerializer::saveToXmlString( pGameObject );
		_bValid	   = _stateData.empty() == false ? 1 : 0;
	}

	void PrefabAsset::collectReferencedPrefabPaths( vector<string>& outPathList ) const
	{
		outPathList.clear();
		const string trimmed = StringUtil::trim( _stateData.c_str() );
		if ( trimmed.empty() )
			return;
		if ( trimmed.front() == '{' )
		{
			JsonDocument doc;
			if ( doc.parse( trimmed ) == false )
				return;
			collectPrefabRefsFromJson( doc.root(), outPathList );
			return;
		}
		XmlDocument doc;
		if ( doc.parse( trimmed ) == false )
			return;
		collectPrefabRefsFromXml( doc.root(), outPathList );
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
			SW_LOG_ERROR( "Shipping requires cooked binary: %#", binPath );
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
			SW_LOG_ERROR( "Circular prefab reference detected for '%#' — spawn aborted to prevent recursion overflow",
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

		if ( pAsset->getStateData().empty() == false )
		{
			bool   bLoadSuccess{ false };
			string bodyStr = StringUtil::trim( pAsset->getStateData().c_str() );
			if ( bodyStr.empty() == false && bodyStr.front() == '{' )
				bLoadSuccess = ObjectStateSerializer::loadFromJsonString( pGameObject, pAsset->getStateData() );
			else if ( bodyStr.empty() == false )
				bLoadSuccess = ObjectStateSerializer::loadFromXmlString( pGameObject, pAsset->getStateData() );
			else
				bLoadSuccess = true;

			if ( bLoadSuccess == false )
			{
				SW_LOG_ERROR( "ObjectState apply failed for '%#' — spawn aborted", pInstanceNameUtf8 );
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
					SW_LOG_ERROR( "Instance diff apply failed for '%#' — spawn aborted", pInstanceNameUtf8 );
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
