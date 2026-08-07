/**
 * @file ObjectStateSerializer.cpp
 * @brief ObjectStateSerializer 구현 (stable SceneTransforms keys)
 */
#include "pch.h"
#include "ObjectStateSerializer.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/Component.h"
#include "Core/Object/SceneComponent.h"
#include "Core/Object/TagSystem.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/Scene.h"
#include "Core/Common/CoreServices.h"
#include "Core/Reflection/Serializer.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	namespace
	{
		std::string formatTagId( const TagID& tag )
		{
			// id:parentHash:ancestorCount:a0,a1,...
			std::string out = std::to_string( tag._id );
			out += ':';
			out += std::to_string( tag._parentHash );
			out += ':';
			out += std::to_string( static_cast<uint32>( tag._ancestorCount ) );
			for ( uint8 i = 0; i < tag._ancestorCount; ++i )
			{
				out += ':';
				out += std::to_string( tag._ancestorHashes[i] );
			}
			return out;
		}

		bool parseTagId( std::string_view text, TagID& outTag )
		{
			outTag = TagID{};
			if ( text.empty() )
				return false;

			std::vector<uint64> parts;
			size_t				start = 0;
			while ( start <= text.size() )
			{
				const size_t sep = text.find( ':', start );
				const auto	 token = text.substr( start, sep == std::string_view::npos ? std::string_view::npos : sep - start );
				if ( token.empty() == false )
				{
					try
					{
						parts.push_back( std::stoull( std::string( token ) ) );
					}
					catch ( ... )
					{
						return false;
					}
				}
				if ( sep == std::string_view::npos )
					break;
				start = sep + 1;
			}

			if ( parts.empty() )
				return false;

			outTag._id		   = parts[0];
			outTag._parentHash = parts.size() > 1 ? parts[1] : 0;
			const uint32 ancCount = parts.size() > 2 ? static_cast<uint32>( parts[2] ) : 0;
			outTag._ancestorCount = static_cast<uint8>( std::min<uint32>( ancCount, TagID::kMaxAncestors ) );
			for ( uint8 i = 0; i < outTag._ancestorCount; ++i )
			{
				const size_t idx = 3u + static_cast<size_t>( i );
				outTag._ancestorHashes[i] = idx < parts.size() ? parts[idx] : 0;
			}
			return outTag.isValid();
		}

		std::string formatFloat3( const float3& v )
		{
			char buf[128];
			std::snprintf( buf, sizeof( buf ), "%g,%g,%g",
						   static_cast<double>( v._x ),
						   static_cast<double>( v._y ),
						   static_cast<double>( v._z ) );
			return buf;
		}

		bool parseFloat3( std::string_view text, float3& out )
		{
			out = float3{};
			float32 vals[3]{};
			size_t	start = 0;
			for ( int i = 0; i < 3; ++i )
			{
				const size_t sep = text.find( ',', start );
				const auto	 token = text.substr( start, sep == std::string_view::npos ? std::string_view::npos : sep - start );
				if ( token.empty() )
					return false;
				try
				{
					vals[i] = static_cast<float32>( std::stof( std::string( token ) ) );
				}
				catch ( ... )
				{
					return false;
				}
				if ( sep == std::string_view::npos )
				{
					if ( i != 2 )
						return false;
					break;
				}
				start = sep + 1;
			}
			out = float3( vals[0], vals[1], vals[2] );
			return true;
		}

		/** @brief Type FQN, else component name, else "Component". */
		std::string componentTypeBaseName( const Component* comp )
		{
			if ( comp == nullptr )
				return "Component";

			if ( const TypeInfo* typeInfo = comp->getTypeInfo() )
			{
				if ( typeInfo->_fullyQualifiedName.empty() == false )
					return typeInfo->_fullyQualifiedName.c_str();
			}

			if ( comp->getComponentName().empty() == false )
				return comp->getComponentName().c_str();

			return "Component";
		}

		/**
		 * @brief Stable SceneTransforms map key for a component on its owner GO.
		 * @details Prefer component name when set; otherwise typeName. Always suffix
		 *          occurrence index among components sharing that base on the same GO.
		 */
		std::string makeStableComponentKey( const Component* comp, int32 occurrenceIndex )
		{
			std::string base;
			if ( comp != nullptr && comp->getComponentName().empty() == false )
				base = comp->getComponentName().c_str();
			else
				base = componentTypeBaseName( comp );

			base += '#';
			base += std::to_string( occurrenceIndex );
			return base;
		}

		/** @brief Build stableKey → Component* for all components on a GO (occurrence by base name). */
		void buildStableComponentKeyMap( const GameObject* gameObject,
										 std::unordered_map<std::string, Component*>& outMap )
		{
			outMap.clear();
			if ( gameObject == nullptr )
				return;

			std::unordered_map<std::string, int32> occurrence;
			for ( Component* comp : gameObject->getAllComponents() )
			{
				if ( comp == nullptr )
					continue;

				std::string base;
				if ( comp->getComponentName().empty() == false )
					base = comp->getComponentName().c_str();
				else
					base = componentTypeBaseName( comp );

				const int32 occ = occurrence[base]++;
				outMap.emplace( makeStableComponentKey( comp, occ ), comp );
			}
		}

		uint32 countSceneComponents( const GameObject* gameObject )
		{
			if ( gameObject == nullptr )
				return 0;

			uint32 count = 0;
			for ( Component* comp : gameObject->getAllComponents() )
			{
				if ( comp != nullptr && comp->asSceneComponent() != nullptr )
					++count;
			}
			return count;
		}

		GameObjectManager* findActiveObjectManager()
		{
			Scene* scene = core::getSceneManager().getActiveScene();
			return scene != nullptr ? scene->getObjectManager() : nullptr;
		}

		SceneComponent* resolveParentSceneComponent( GameObject* childOwner,
													 std::string_view parentOwnerName,
													 std::string_view parentStableKey )
		{
			if ( parentStableKey.empty() )
				return nullptr;

			GameObject* parentOwner = childOwner;
			if ( parentOwnerName.empty() == false )
			{
				const bool bSameOwner = childOwner != nullptr &&
										std::string_view( childOwner->getName().c_str() ) == parentOwnerName;
				if ( bSameOwner == false )
				{
					GameObjectManager* manager = findActiveObjectManager();
					if ( manager == nullptr )
						return nullptr;
					parentOwner = manager->findGameObjectByName(
						hashed_string( std::string( parentOwnerName ).c_str() ) );
				}
			}

			if ( parentOwner == nullptr )
				return nullptr;

			std::unordered_map<std::string, Component*> keyMap;
			buildStableComponentKeyMap( parentOwner, keyMap );
			const auto it = keyMap.find( std::string( parentStableKey ) );
			if ( it == keyMap.end() || it->second == nullptr )
				return nullptr;
			return it->second->asSceneComponent();
		}

		/** @brief local TRS + parent ref: empty = root; "ownerName/stableKey" = parent. */
		std::string formatSceneTransform( const SceneComponent* sceneComp )
		{
			std::string out = formatFloat3( sceneComp->getLocalPosition() );
			out += ';';
			out += formatFloat3( sceneComp->getLocalRotation() );
			out += ';';
			out += formatFloat3( sceneComp->getLocalScale() );
			out += ';';

			SceneComponent* parent = sceneComp->getParent();
			if ( parent == nullptr )
				return out;

			GameObject* parentOwner = parent->getOwner();
			if ( parentOwner == nullptr )
				return out;

			std::unordered_map<std::string, Component*> parentKeyMap;
			buildStableComponentKeyMap( parentOwner, parentKeyMap );

			std::string parentKey;
			for ( const auto& entry : parentKeyMap )
			{
				if ( entry.second == parent )
				{
					parentKey = entry.first;
					break;
				}
			}
			if ( parentKey.empty() )
				return out;

			out += parentOwner->getName().c_str();
			out += '/';
			out += parentKey;
			return out;
		}

		bool parseSceneTransform( std::string_view text,
								  float3&		   outPos,
								  float3&		   outRot,
								  float3&		   outScl,
								  std::string&	   outParentOwner,
								  std::string&	   outParentKey,
								  int32&		   outLegacyParentIdx )
		{
			outPos			   = float3{};
			outRot			   = float3{};
			outScl			   = float3( 1.0f, 1.0f, 1.0f );
			outParentOwner.clear();
			outParentKey.clear();
			outLegacyParentIdx = -1;

			std::string_view parts[4];
			size_t			 start	   = 0;
			size_t			 partCount = 0;
			while ( partCount < 4 && start <= text.size() )
			{
				const size_t sep	   = text.find( ';', start );
				parts[partCount++] = text.substr( start, sep == std::string_view::npos ? std::string_view::npos : sep - start );
				if ( sep == std::string_view::npos )
					break;
				start = sep + 1;
			}
			if ( partCount < 3 )
				return false;

			if ( parseFloat3( parts[0], outPos ) == false ||
				 parseFloat3( parts[1], outRot ) == false ||
				 parseFloat3( parts[2], outScl ) == false )
				return false;

			if ( partCount < 4 || parts[3].empty() )
				return true;

			const std::string_view parentRef = parts[3];
			const size_t		   slash	 = parentRef.find( '/' );
			if ( slash != std::string_view::npos )
			{
				outParentOwner.assign( parentRef.substr( 0, slash ) );
				outParentKey.assign( parentRef.substr( slash + 1 ) );
				return true;
			}

			// Legacy: same-GO flat component index.
			bool bAllDigits = parentRef.empty() == false;
			for ( char c : parentRef )
			{
				if ( c < '0' || c > '9' )
				{
					if ( c != '-' )
					{
						bAllDigits = false;
						break;
					}
				}
			}
			if ( bAllDigits )
			{
				try
				{
					outLegacyParentIdx = static_cast<int32>( std::stoi( std::string( parentRef ) ) );
				}
				catch ( ... )
				{
					outLegacyParentIdx = -1;
				}
				return true;
			}

			// Same-GO stable key without owner prefix.
			outParentKey.assign( parentRef );
			return true;
		}

		struct PendingAttach
		{
			SceneComponent* child			= nullptr;
			std::string		parentOwnerName;
			std::string		parentStableKey;
			int32			legacyParentIdx = -1;
		};

		void applyPendingAttaches( GameObject* gameObject, const std::vector<PendingAttach>& pendingAttaches )
		{
			if ( gameObject == nullptr || pendingAttaches.empty() )
				return;

			const std::vector<Component*>& comps = gameObject->getAllComponents();

			for ( const PendingAttach& link : pendingAttaches )
			{
				if ( link.child == nullptr )
					continue;

				SceneComponent* parent = nullptr;
				if ( link.legacyParentIdx >= 0 )
				{
					if ( static_cast<size_t>( link.legacyParentIdx ) < comps.size() &&
						 comps[static_cast<size_t>( link.legacyParentIdx )] != nullptr )
						parent = comps[static_cast<size_t>( link.legacyParentIdx )]->asSceneComponent();
				}
				else if ( link.parentStableKey.empty() == false )
				{
					parent = resolveParentSceneComponent( gameObject, link.parentOwnerName, link.parentStableKey );
				}

				if ( parent == nullptr )
				{
					SW_LOG_ERROR( "[ObjectStateSerializer] Failed to resolve SceneComponent parent "
								  "(owner='%#' key='%#' legacyIdx=%#) for child on '%#'.",
								  link.parentOwnerName.c_str(),
								  link.parentStableKey.c_str(),
								  link.legacyParentIdx,
								  gameObject->getName().c_str() );
					continue;
				}

				link.child->attachToComponent( parent );
			}
		}

		/**
		 * @brief Attach GO to ParentGO by stable name (empty = root).
		 * @details Detaches first so stale play-time parents are cleared before rebind.
		 *          Missing parent is expected on first pass of multi-GO restore; log only when requested.
		 */
		void applyParentGO( GameObject* gameObject, std::string_view parentName, bool bLogMissing )
		{
			if ( gameObject == nullptr )
				return;

			gameObject->detachFromParent();
			if ( parentName.empty() )
				return;

			GameObjectManager* manager = findActiveObjectManager();
			if ( manager == nullptr )
			{
				if ( bLogMissing )
				{
					SW_LOG_ERROR( "[ObjectStateSerializer] No active GameObjectManager to resolve ParentGO '%#' for '%#'.",
								  std::string( parentName ).c_str(),
								  gameObject->getName().c_str() );
				}
				return;
			}

			GameObject* parent = manager->findGameObjectByName(
				hashed_string( std::string( parentName ).c_str() ) );
			if ( parent == nullptr )
			{
				if ( bLogMissing )
				{
					SW_LOG_ERROR( "[ObjectStateSerializer] Failed to resolve ParentGO '%#' for '%#'.",
								  std::string( parentName ).c_str(),
								  gameObject->getName().c_str() );
				}
				return;
			}

			if ( gameObject->attachToParent( parent ) == false && bLogMissing )
			{
				SW_LOG_ERROR( "[ObjectStateSerializer] attachToParent failed for '%#' -> '%#'.",
							  gameObject->getName().c_str(),
							  parent->getName().c_str() );
			}
		}

		void readAndApplyParentGO( GameObject* gameObject, RapidXmlBackend& xmlBackend, bool bLogMissing )
		{
			if ( gameObject == nullptr )
				return;

			std::string parentName;
			xmlBackend.readValue( "ParentGO", parentName ); // missing key → empty → root
			applyParentGO( gameObject, parentName, bLogMissing );
		}

		bool collectAndApplySceneTransforms( GameObject*										gameObject,
											 RapidXmlBackend&									xmlBackend,
											 std::vector<PendingAttach>*						outPendingAttaches,
											 bool												bApplyTransforms )
		{
			if ( gameObject == nullptr )
				return false;

			std::unordered_map<std::string, Component*> keyMap;
			buildStableComponentKeyMap( gameObject, keyMap );

			const uint32 sceneCompCount = countSceneComponents( gameObject );
			uint32		 xformCount		= 0;
			uint32		 appliedCount	= 0;

			std::vector<PendingAttach> localPending;
			std::vector<PendingAttach>& pending = outPendingAttaches != nullptr ? *outPendingAttaches : localPending;

			XmlMapItemDelegate xformCb = SW_DELEGATE_LAMBDA(
				XmlMapItemDelegate,
				[gameObject, &keyMap, &xformCount, &appliedCount, &pending, bApplyTransforms]( std::string_view keyStr,
																							   std::string_view valStr )
			{
				if ( keyStr.empty() )
					return;

				++xformCount;

				Component* comp = nullptr;
				const auto it	= keyMap.find( std::string( keyStr ) );
				if ( it != keyMap.end() )
					comp = it->second;

				// Legacy numeric flat-index keys.
				if ( comp == nullptr )
				{
					bool bNumeric = true;
					for ( char c : keyStr )
					{
						if ( c < '0' || c > '9' )
						{
							bNumeric = false;
							break;
						}
					}
					if ( bNumeric )
					{
						try
						{
							const int32 flatIdx = static_cast<int32>( std::stoi( std::string( keyStr ) ) );
							const auto& comps	= gameObject->getAllComponents();
							if ( flatIdx >= 0 && static_cast<size_t>( flatIdx ) < comps.size() )
								comp = comps[static_cast<size_t>( flatIdx )];
						}
						catch ( ... )
						{
						}
					}
				}

				if ( comp == nullptr )
				{
					const std::string key( keyStr );
					SW_LOG_ERROR( "[ObjectStateSerializer] SceneTransform key '%#' not found on GameObject '%#'.",
								  key.c_str(),
								  gameObject->getName().c_str() );
					return;
				}

				SceneComponent* sceneComp = comp->asSceneComponent();
				if ( sceneComp == nullptr )
				{
					const std::string key( keyStr );
					SW_LOG_ERROR( "[ObjectStateSerializer] SceneTransform key '%#' is not a SceneComponent on '%#'.",
								  key.c_str(),
								  gameObject->getName().c_str() );
					return;
				}

				if ( valStr.empty() )
					return;

				float3		pos{};
				float3		rot{};
				float3		scl{ 1.0f, 1.0f, 1.0f };
				std::string parentOwner;
				std::string parentKey;
				int32		legacyParentIdx = -1;
				if ( parseSceneTransform( valStr, pos, rot, scl, parentOwner, parentKey, legacyParentIdx ) == false )
				{
					const std::string key( keyStr );
					SW_LOG_ERROR( "[ObjectStateSerializer] Failed to parse SceneTransform for key '%#' on '%#'.",
								  key.c_str(),
								  gameObject->getName().c_str() );
					return;
				}

				if ( bApplyTransforms )
				{
					sceneComp->setLocalPosition( pos );
					sceneComp->setLocalRotation( rot );
					sceneComp->setLocalScale( scl );
				}

				++appliedCount;

				if ( legacyParentIdx >= 0 || parentKey.empty() == false )
				{
					PendingAttach link;
					link.child			  = sceneComp;
					link.parentOwnerName  = std::move( parentOwner );
					link.parentStableKey  = std::move( parentKey );
					link.legacyParentIdx  = legacyParentIdx;
					pending.push_back( std::move( link ) );
				}
			} );
			xmlBackend.iterateMap( "SceneTransforms", xformCb );

			if ( xformCount != sceneCompCount )
			{
				SW_LOG_ERROR( "[ObjectStateSerializer] SceneTransforms count mismatch on '%#': "
							  "xmlEntries=%# sceneComponents=%# applied=%#.",
							  gameObject->getName().c_str(),
							  xformCount,
							  sceneCompCount,
							  appliedCount );
			}

			if ( outPendingAttaches == nullptr )
				applyPendingAttaches( gameObject, pending );

			return true;
		}
	} // namespace

	std::string ObjectStateSerializer::saveToXmlString( const GameObject* gameObject )
	{
		if ( gameObject == nullptr )
			return {};

		RapidXmlBackend xmlBackend;
		xmlBackend.initXmlSerialization( "GameObjectState" );
		xmlBackend.writeValue( "Name", gameObject->getName().c_str() );
		// ObjectId is emitted for debugging/diff only. Runtime IDs are process-local and must not be restored
		// (would collide with GameObject::_s_nextObjectId and break manager id maps).
		xmlBackend.writeValue( "ObjectId", std::to_string( gameObject->getObjectId() ).c_str() );
		xmlBackend.writeValue( "IsActive", gameObject->isActive() ? "true" : "false" );
		// Stable parent name (empty = root). Rebind after multi-GO load via rebindSceneHierarchy.
		const GameObject* parentGo = gameObject->getParent();
		xmlBackend.writeValue( "ParentGO", parentGo != nullptr ? parentGo->getName().c_str() : "" );

		// When TypeInfo is registered, also emit reflected PROPERTY fields via XmlSerializer
		// into a nested document stored as ReflectedXml (keeps GameObjectState root/aliases).
		if ( const TypeInfo* typeInfo = gameObject->getTypeInfo() )
		{
			const std::string reflected = XmlSerializer::serialize( gameObject, *typeInfo );
			if ( reflected.empty() == false )
				xmlBackend.writeValue( "ReflectedXml", reflected.c_str() );
		}

		xmlBackend.beginArray( "Tags" );
		for ( const TagID& tag : gameObject->getTags().getTags() )
			xmlBackend.writeArrayItem( formatTagId( tag ).c_str() );
		xmlBackend.endArray();

		const std::vector<Component*>& comps = gameObject->getAllComponents();

		xmlBackend.beginMap( "Components" );
		for ( Component* comp : comps )
		{
			if ( comp == nullptr )
				continue;

			xmlBackend.beginMapEntry();
			xmlBackend.writeMapKey( comp->getComponentName().c_str() );

			std::string reflected;
			if ( const TypeInfo* typeInfo = comp->getTypeInfo() )
				reflected = XmlSerializer::serialize( comp, *typeInfo );
			xmlBackend.writeMapValue( reflected.c_str() );
			xmlBackend.endMapEntry();
		}
		xmlBackend.endMap();

		// SceneComponent local TRS + parent attach, keyed by stable component id.
		xmlBackend.beginMap( "SceneTransforms" );
		{
			std::unordered_map<std::string, int32> occurrence;
			for ( Component* comp : comps )
			{
				if ( comp == nullptr )
					continue;
				SceneComponent* sceneComp = comp->asSceneComponent();
				if ( sceneComp == nullptr )
					continue;

				std::string base;
				if ( comp->getComponentName().empty() == false )
					base = comp->getComponentName().c_str();
				else
					base = componentTypeBaseName( comp );

				const int32		  occ = occurrence[base]++;
				const std::string key = makeStableComponentKey( comp, occ );

				xmlBackend.beginMapEntry();
				xmlBackend.writeMapKey( key.c_str() );
				xmlBackend.writeMapValue( formatSceneTransform( sceneComp ).c_str() );
				xmlBackend.endMapEntry();
			}
		}
		xmlBackend.endMap();

		return xmlBackend.endSerialize();
	}

	bool ObjectStateSerializer::loadFromXmlString( GameObject* gameObject, std::string_view xmlString )
	{
		if ( gameObject == nullptr || xmlString.empty() )
			return false;

		std::string		xmlCopy( xmlString );
		RapidXmlBackend xmlBackend;
		if ( xmlBackend.initXmlDeserialization( xmlCopy.c_str(), "GameObjectState" ) == false )
			return false;

		// Drop stale components / tags before applying saved state (avoids duplicates).
		gameObject->clearComponents();
		gameObject->clearTags();

		std::string nameStr;
		if ( xmlBackend.readValue( "Name", nameStr ) && nameStr.empty() == false )
		{
			gameObject->setName( hashed_string( nameStr.c_str() ) );
		}

		// Intentionally skip ObjectId restore — IDs are runtime-allocated and not stable across sessions.

		std::string activeStr;
		if ( xmlBackend.readValue( "IsActive", activeStr ) && activeStr.empty() == false )
		{
			gameObject->setActive( activeStr == "true" );
		}

		std::string reflectedXml;
		if ( xmlBackend.readValue( "ReflectedXml", reflectedXml ) && reflectedXml.empty() == false )
		{
			if ( const TypeInfo* typeInfo = gameObject->getTypeInfo() )
			{
				if ( XmlSerializer::deserialize( gameObject, *typeInfo, reflectedXml ) == false )
					SW_LOG_WARNING( "[ObjectStateSerializer] ReflectedXml deserialize failed for %#", typeInfo->_fullyQualifiedName.c_str() );
			}
		}

		XmlArrayItemDelegate tagCb = SW_DELEGATE_LAMBDA( XmlArrayItemDelegate, [gameObject]( std::string_view itemStr )
		{
			TagID tag{};
			if ( parseTagId( itemStr, tag ) )
				gameObject->addTag( tag );
		} );
		xmlBackend.iterateArray( "Tags", tagCb );

		XmlMapItemDelegate compCb = SW_DELEGATE_LAMBDA( XmlMapItemDelegate, [gameObject]( std::string_view keyStr, std::string_view valStr )
		{
			if ( keyStr.empty() )
				return;

			Component* comp = gameObject->addComponentByName( hashed_string( std::string( keyStr ).c_str() ) );
			if ( comp == nullptr )
			{
				const std::string typeName( keyStr );
				SW_LOG_WARNING( "[ObjectStateSerializer] Failed to recreate component '%#'", typeName.c_str() );
				return;
			}

			if ( valStr.empty() == false )
			{
				if ( const TypeInfo* typeInfo = comp->getTypeInfo() )
				{
					if ( XmlSerializer::deserialize( comp, *typeInfo, std::string( valStr ) ) == false )
						SW_LOG_WARNING( "[ObjectStateSerializer] Component ReflectedXml deserialize failed for %#",
										typeInfo->_fullyQualifiedName.c_str() );
				}
			}
		} );
		xmlBackend.iterateMap( "Components", compCb );

		std::vector<PendingAttach> pendingAttaches;
		collectAndApplySceneTransforms( gameObject, xmlBackend, &pendingAttaches, true );
		applyPendingAttaches( gameObject, pendingAttaches );

		// ParentGO may be missing on first pass of multi-GO restore; rebindSceneHierarchy fixes it.
		readAndApplyParentGO( gameObject, xmlBackend, false );

		return true;
	}

	bool ObjectStateSerializer::rebindSceneHierarchy( GameObject* gameObject, std::string_view xmlString )
	{
		if ( gameObject == nullptr || xmlString.empty() )
			return false;

		std::string		xmlCopy( xmlString );
		RapidXmlBackend xmlBackend;
		if ( xmlBackend.initXmlDeserialization( xmlCopy.c_str(), "GameObjectState" ) == false )
			return false;

		// Re-resolve parents only (transforms already applied during load). Needed after multi-GO restore
		// so cross-GO attaches see fully rebuilt component lists.
		std::vector<PendingAttach> pendingAttaches;
		collectAndApplySceneTransforms( gameObject, xmlBackend, &pendingAttaches, false );
		applyPendingAttaches( gameObject, pendingAttaches );

		// Second pass: GameObject parent by stable name (all GOs must exist).
		readAndApplyParentGO( gameObject, xmlBackend, true );
		return true;
	}

	bool ObjectStateSerializer::saveToXmlFile( const GameObject* gameObject, const std::string_view filePath )
	{
		std::string xmlStr = saveToXmlString( gameObject );
		if ( xmlStr.empty() )
			return false;

		return FileUtil::writeFile( std::string{ filePath }, reinterpret_cast<const uint8*>( xmlStr.data() ), xmlStr.size() );
	}

	bool ObjectStateSerializer::loadFromXmlFile( GameObject* gameObject, const std::string_view filePath )
	{
		std::vector<uint8> data;
		if ( FileUtil::readFile( std::string{ filePath }, data ) == false || data.empty() )
			return false;

		std::string_view xmlStr( reinterpret_cast<const char*>( data.data() ), data.size() );
		return loadFromXmlString( gameObject, xmlStr );
	}

	void ObjectStateSerializer::openSaveFileDialog( const GameObject* gameObject, FileDialogDelegate onSaveDone )
	{
		FileDialogParams params;
		params._type				= FileDialogParams::Type::Save;
		params._description			= "GameObject State XML File (*.xml)";
		params._filterExtensionList = { "xml" };
		params._bEnableMultiselect	= false;

		FileDialogDelegate del = SW_DELEGATE_LAMBDA( FileDialogDelegate, [gameObject, onSaveDone]( const std::vector<std::string>& fileNames )
		{
			if ( fileNames.empty() == false && gameObject != nullptr )
			{
				saveToXmlFile( gameObject, fileNames.front() );
			}
			if ( onSaveDone.isBound() )
			{
				onSaveDone( fileNames );
			}
		} );
		FileUtil::openFileDialog( params, del );
	}

	void ObjectStateSerializer::openLoadFileDialog( GameObject* gameObject, FileDialogDelegate onLoadDone )
	{
		FileDialogParams params;
		params._type				= FileDialogParams::Type::Open;
		params._description			= "GameObject State XML File (*.xml)";
		params._filterExtensionList = { "xml" };
		params._bEnableMultiselect	= false;

		FileDialogDelegate del = SW_DELEGATE_LAMBDA( FileDialogDelegate, [gameObject, onLoadDone]( const std::vector<std::string>& fileNames )
		{
			if ( fileNames.empty() == false && gameObject != nullptr )
			{
				loadFromXmlFile( gameObject, fileNames.front() );
			}
			if ( onLoadDone.isBound() )
			{
				onLoadDone( fileNames );
			}
		} );
		FileUtil::openFileDialog( params, del );
	}
} // namespace sw
