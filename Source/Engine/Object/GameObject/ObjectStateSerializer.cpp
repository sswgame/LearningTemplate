#include "pch.h"

#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/BinaryStream.h"
#include "Engine/Serialization/Core/Serializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	namespace
	{

		namespace
		{
			string formatTagId( const TagID& tag )
			{
				if ( tag._pString != nullptr )
					return string( "str:" ) + tag._pString;
				return to_string( tag._id );
			}

			bool parseTagId( string_view text, TagID& outTag )
			{
				outTag = TagID{};
				if ( text.empty() )
					return false;

				if ( text.size() >= 4 && text.substr( 0, 4 ) == "str:" )
				{
					outTag = requestTag( text.substr( 4 ) );
					return outTag.isValid();
				}

				const fixed_string<constant::kMaxBuffer64> tokenNt{ text };
				outTag._id = StringUtil::strtoull( tokenNt.c_str() );
				return outTag.isValid();
			}

			string formatFloat3( const float3& v )
			{
				StringBuilder<constant::kMaxBuffer64> sb;
				sb.append( v._x ).append( ',' ).append( v._y ).append( ',' ).append( v._z );
				return string{ sb.c_str(), sb.size() };
			}

			bool parseFloat3( string_view text, float3& out )
			{
				out = float3{};
				float32 vals[3]{};
				size_t	start{ 0 };
				for ( int32 axisIndex = 0; axisIndex < 3; ++axisIndex )
				{
					const size_t sep   = text.find( ',', start );
					string_view	 token = text.substr( start, sep == string_view::npos ? string_view::npos : sep - start );
					if ( token.empty() )
						return false;
					const fixed_string<constant::kMaxBuffer64> tokenNt{ token };
					vals[axisIndex] = static_cast<float32>( StringUtil::atof( tokenNt.c_str() ) );
					if ( sep == string_view::npos )
					{
						if ( axisIndex != 2 )
							return false;
						break;
					}
					start = sep + 1;
				}
				out = float3( vals[0], vals[1], vals[2] );
				return true;
			}

			/** @brief Type FQN, else component name, else "Component". */
			string componentTypeBaseName( const Component* pComp )
			{
				if ( pComp == nullptr )
					return "Component";

				const TypeInfo* pTypeInfo = pComp->getTypeInfo();
				if ( pTypeInfo != nullptr )
				{
					if ( pTypeInfo->_fullyQualifiedName.empty() == false )
						return pTypeInfo->_fullyQualifiedName.c_str();
				}

				if ( pComp->getComponentName().empty() == false )
					return pComp->getComponentName().c_str();

				return "Component";
			}

			/**
			 * @brief Stable SceneTransforms map key for a component on its owner GO.
			 * @details Prefer component name when set; otherwise typeName. Always suffix
			 *          occurrence index among components sharing that base on the same GO.
			 */
			string makeStableComponentKey( const Component* pComp, int32 occurrenceIndex )
			{
				string base;
				if ( pComp != nullptr && pComp->getComponentName().empty() == false )
					base = pComp->getComponentName().c_str();
				else
					base = componentTypeBaseName( pComp );

				base += '#';
				base += to_string( occurrenceIndex );
				return base;
			}

			void mergeJsonObject( JsonValue dest, const JsonValue src )
			{
				if ( dest.isObject() == false || src.isObject() == false )
					return;
				for ( const string& key : src.memberNames() )
				{
					dest.set( key, false ).assignFrom( src.get( key, false ) );
				}
			}

			string serializeComponentJson( Component* pComp )
			{
				JsonDocument	outDoc;
				JsonValue		outRoot	  = outDoc.makeObject();
				const TypeInfo* pTypeInfo = pComp->getTypeInfo();
				if ( pTypeInfo != nullptr )
				{
					const string reflected =
						JsonSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pComp, *pTypeInfo );
					JsonDocument src;
					if ( src.parse( reflected ) && src.root().isObject() )
						mergeJsonObject( outRoot, src.root() );
				}
				const Component::EcsDataView ecsData = pComp->ensureEcsData();
				if ( ecsData.instance != nullptr && ecsData.typeInfo != nullptr && ecsData.instance != pComp )
				{
					const string reflected =
						JsonSerializer::serializeVersioned( kObjectReflectedSchemaVersion, ecsData.instance, *ecsData.typeInfo );
					JsonDocument src;
					if ( src.parse( reflected ) && src.root().isObject() )
						mergeJsonObject( outRoot, src.root() );
				}
				return outDoc.dump();
			}

			void deserializeComponentJson( Component* pComp, string_view json )
			{
				if ( json.empty() )
					return;
				uint32			ver{ 0 };
				const TypeInfo* pTypeInfo = pComp->getTypeInfo();
				if ( pTypeInfo != nullptr )
					JsonSerializer::deserializeVersioned( ver, pComp, *pTypeInfo, json );
				const Component::EcsDataView ecsData = pComp->ensureEcsData();
				if ( ecsData.instance != nullptr && ecsData.typeInfo != nullptr && ecsData.instance != pComp )
				{
					ver = 0;
					JsonSerializer::deserializeVersioned( ver, ecsData.instance, *ecsData.typeInfo, json );
				}
			}

			string serializeComponentXml( Component* pComp )
			{
				string result;

				const TypeInfo* pTypeInfo = pComp->getTypeInfo();
				if ( pTypeInfo != nullptr )
					result = XmlSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pComp, *pTypeInfo );

				const auto [pInstance, pEcsTypeInfo] = pComp->ensureEcsData();
				if ( pInstance != nullptr && pEcsTypeInfo != nullptr && pInstance != pComp )
				{
					const string dataXml = XmlSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pInstance, *pEcsTypeInfo );
					if ( dataXml.empty() == false )
					{
						if ( result.empty() )
							result = dataXml;
						else
							result += dataXml;
					}
				}

				return result;
			}

			void deserializeComponentXml( Component* pComp, string_view xml )
			{
				if ( xml.empty() )
					return;
				uint32			schemaVer{ 0 };
				const TypeInfo* pTypeInfo = pComp->getTypeInfo();
				if ( pTypeInfo != nullptr )
				{
					XmlSerializer::deserializeVersioned( schemaVer, pComp, *pTypeInfo, string( xml ),
														 kObjectReflectedSchemaVersion );
				}
				const Component::EcsDataView ecsData = pComp->ensureEcsData();
				if ( ecsData.instance != nullptr && ecsData.typeInfo != nullptr && ecsData.instance != pComp )
				{
					schemaVer = 0;
					XmlSerializer::deserializeVersioned( schemaVer, ecsData.instance, *ecsData.typeInfo, string( xml ),
														 kObjectReflectedSchemaVersion );
				}
			}

			/** @brief Build stableKey -> Component* for all components on a GO (occurrence by base name). */
			void buildStableComponentKeyMap( const GameObject*					pGameObject,
											 unordered_map<string, Component*>& outMap )
			{
				outMap.clear();
				if ( pGameObject == nullptr )
					return;

				unordered_map<string, int32> occurrence;
				for ( Component* pComp : pGameObject->getAllComponents() )
				{
					if ( pComp == nullptr )
						continue;

					string base;
					if ( pComp->getComponentName().empty() == false )
						base = pComp->getComponentName().c_str();
					else
						base = componentTypeBaseName( pComp );

					const int32 occ = occurrence[base]++;
					outMap.emplace( makeStableComponentKey( pComp, occ ), pComp );
				}
			}

			uint32 countSceneComponents( const GameObject* pGameObject )
			{
				if ( pGameObject == nullptr )
					return 0;

				uint32 count{ 0 };
				for ( Component* pComp : pGameObject->getAllComponents() )
				{
					if ( pComp != nullptr && pComp->asSceneComponent() != nullptr )
						++count;
				}
				return count;
			}

			GameObjectManager* findActiveObjectManager()
			{
				Scene* pScene = engine::getSceneManager().getActiveScene();
				return pScene != nullptr ? pScene->getObjectManager() : nullptr;
			}

			SceneComponent* resolveParentSceneComponent( GameObject* pChildOwner,
														 string_view parentOwnerName,
														 string_view parentStableKey )
			{
				if ( parentStableKey.empty() )
					return nullptr;

				GameObject* pParentOwner = pChildOwner;
				if ( parentOwnerName.empty() == false )
				{
					const bool bSameOwner = pChildOwner != nullptr &&
											string_view( pChildOwner->getName().c_str() ) == parentOwnerName;
					if ( bSameOwner == false )
					{
						GameObjectManager* pManager = pChildOwner != nullptr ? pChildOwner->getManager() : findActiveObjectManager();
						if ( pManager == nullptr )
							return nullptr;
						pParentOwner = pManager->findGameObjectByName(
							hashed_string( parentOwnerName.data(), static_cast<uint32>( parentOwnerName.size() ) ) );
					}
				}

				if ( pParentOwner == nullptr )
					return nullptr;

				unordered_map<string, Component*> keyMap;
				buildStableComponentKeyMap( pParentOwner, keyMap );
				const auto it = keyMap.find( string( parentStableKey ) );
				if ( it == keyMap.end() || it->second == nullptr )
					return nullptr;
				return it->second->asSceneComponent();
			}

			/** @brief local TRS + parent ref: empty = root; "ownerName/stableKey" = parent. */
			string formatSceneTransform( const SceneComponent* pSceneComp )
			{
				string out = formatFloat3( pSceneComp->getLocalPosition() );
				out += ';';
				out += formatFloat3( pSceneComp->getLocalRotation() );
				out += ';';
				out += formatFloat3( pSceneComp->getLocalScale() );
				out += ';';

				SceneComponent* pParent = pSceneComp->getParent();
				if ( pParent == nullptr )
					return out;

				GameObject* pParentOwner = pParent->getOwner();
				if ( pParentOwner == nullptr )
					return out;

				unordered_map<string, Component*> parentKeyMap;
				buildStableComponentKeyMap( pParentOwner, parentKeyMap );

				string parentKey;
				for ( const auto& entry : parentKeyMap )
				{
					if ( entry.second == pParent )
					{
						parentKey = entry.first;
						break;
					}
				}
				if ( parentKey.empty() )
					return out;

				out += pParentOwner->getName().c_str();
				out += '/';
				out += parentKey;
				return out;
			}

			bool parseSceneTransform( string_view text,
									  float3&	  outPos,
									  float3&	  outRot,
									  float3&	  outScl,
									  string&	  outParentOwner,
									  string&	  outParentKey )
			{
				outPos = float3{};
				outRot = float3{};
				outScl = float3( 1.0f, 1.0f, 1.0f );
				outParentOwner.clear();
				outParentKey.clear();

				string_view parts[4];
				size_t		start{ 0 };
				size_t		partCount{ 0 };
				while ( partCount < 4 && start <= text.size() )
				{
					const size_t sep   = text.find( ';', start );
					parts[partCount++] = text.substr( start, sep == string_view::npos ? string_view::npos : sep - start );
					if ( sep == string_view::npos )
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

				string_view	 parentRef = parts[3];
				const size_t slash	   = parentRef.find( '/' );
				if ( slash != string_view::npos )
				{
					outParentOwner.assign( parentRef.substr( 0, slash ) );
					outParentKey.assign( parentRef.substr( slash + 1 ) );
					return true;
				}

				// Same-GO stable key without owner prefix.
				outParentKey.assign( parentRef );
				return true;
			}

			struct PendingAttach
			{
				SceneComponent* _pChild{ nullptr };
				string			_parentOwnerName;
				string			_parentStableKey;
			};

			void applyPendingAttaches( GameObject* pGameObject, const vector<PendingAttach>& pendingAttaches )
			{
				if ( pGameObject == nullptr || pendingAttaches.empty() )
					return;

				for ( const PendingAttach& link : pendingAttaches )
				{
					if ( link._pChild == nullptr || link._parentStableKey.empty() )
						continue;

					SceneComponent* pParent =
						resolveParentSceneComponent( pGameObject, link._parentOwnerName, link._parentStableKey );

					if ( pParent == nullptr || pParent == link._pChild )
					{
						if ( pParent == link._pChild )
						{
							SW_LOG_WARNING( "[ObjectStateSerializer] Cannot attach SceneComponent to itself on '%#'.", pGameObject->getName().c_str() );
							continue;
						}
						SW_LOG_ERROR( "[ObjectStateSerializer] Failed to resolve SceneComponent parent "
									  "(owner='%#' key='%#') for child on '%#'.",
									  link._parentOwnerName.c_str(),
									  link._parentStableKey.c_str(),
									  pGameObject->getName().c_str() );
						continue;
					}

					link._pChild->attachToComponent( pParent );
				}
			}

			/**
			 * @brief Attach GO to ParentGO by stable name (empty = root).
			 * @details Detaches first so stale play-time parents are cleared before rebind.
			 *          Missing parent is expected on first pass of multi-GO restore; log only when requested.
			 */
			void applyParentGO( GameObject* pGameObject, string_view parentName, bool bLogMissing )
			{
				if ( pGameObject == nullptr )
					return;

				pGameObject->detachFromParent();
				if ( parentName.empty() )
					return;

				GameObjectManager* pManager = pGameObject->getManager();
				if ( pManager == nullptr )
					pManager = findActiveObjectManager();
				if ( pManager == nullptr )
				{
					if ( bLogMissing )
					{
						SW_LOG_ERROR( "[ObjectStateSerializer] No active GameObjectManager to resolve ParentGO '%#' for '%#'.",
									  string( parentName ).c_str(),
									  pGameObject->getName().c_str() );
					}
					return;
				}

				GameObject* pParent = pManager->findGameObjectByName(
					hashed_string( parentName.data(), static_cast<uint32>( parentName.size() ) ) );
				if ( pParent == nullptr || pParent == pGameObject )
				{
					if ( pParent == pGameObject )
					{
						SW_LOG_WARNING( "[ObjectStateSerializer] Cannot attach GameObject to itself ('%#').", pGameObject->getName().c_str() );
						return;
					}
					if ( bLogMissing )
					{
						SW_LOG_ERROR( "[ObjectStateSerializer] Failed to resolve ParentGO '%#' for '%#'.",
									  string( parentName ).c_str(),
									  pGameObject->getName().c_str() );
					}
					return;
				}

				if ( pGameObject->attachToParent( pParent ) == false && bLogMissing )
				{
					SW_LOG_ERROR( "[ObjectStateSerializer] attachToParent failed for '%#' -> '%#'.",
								  pGameObject->getName().c_str(),
								  pParent->getName().c_str() );
				}
			}

			void readAndApplyParentGO( GameObject* pGameObject, XmlDocumentBackend& xmlBackend, bool bLogMissing )
			{
				if ( pGameObject == nullptr )
					return;

				string parentName;
				xmlBackend.readValue( "ParentGO", parentName ); // missing key => empty => root
				applyParentGO( pGameObject, parentName, bLogMissing );
			}

			bool collectAndApplySceneTransforms( GameObject*			pGameObject,
												 XmlDocumentBackend&	xmlBackend,
												 vector<PendingAttach>* outPendingAttaches,
												 bool					bApplyTransforms )
			{
				if ( pGameObject == nullptr )
					return false;

				unordered_map<string, Component*> keyMap;
				buildStableComponentKeyMap( pGameObject, keyMap );

				const uint32 sceneCompCount = countSceneComponents( pGameObject );
				uint32		 xformCount{ 0 };
				uint32		 appliedCount{ 0 };

				vector<PendingAttach>  listLocalPending;
				vector<PendingAttach>& pending = outPendingAttaches != nullptr ? *outPendingAttaches : listLocalPending;

				XmlMapItemDelegate xformCb = SW_DELEGATE_LAMBDA(
					XmlMapItemDelegate,
					[pGameObject, &keyMap, &xformCount, &appliedCount, &pending, bApplyTransforms]( string_view keyStr,
																									string_view valStr )
				{
					(void)pGameObject;
					if ( keyStr.empty() )
						return;

					++xformCount;

					Component* pComp{ nullptr };
					const auto it = keyMap.find( string( keyStr ) );
					if ( it != keyMap.end() )
						pComp = it->second;

					if ( pComp == nullptr )
					{
						const string key( keyStr );
						SW_LOG_ERROR( "[ObjectStateSerializer] SceneTransform key '%#' not found on GameObject '%#'.",
									  key.c_str(),
									  pGameObject->getName().c_str() );
						return;
					}

					SceneComponent* pSceneComp = pComp->asSceneComponent();
					if ( pSceneComp == nullptr )
					{
						const string key( keyStr );
						SW_LOG_ERROR( "[ObjectStateSerializer] SceneTransform key '%#' is not a SceneComponent on '%#'.",
									  key.c_str(),
									  pGameObject->getName().c_str() );
						return;
					}

					if ( valStr.empty() )
						return;

					float3 pos{};
					float3 rot{};
					float3 scl{ 1.0f, 1.0f, 1.0f };
					string parentOwner;
					string parentKey;
					if ( parseSceneTransform( valStr, pos, rot, scl, parentOwner, parentKey ) == false )
					{
						const string key( keyStr );
						SW_LOG_ERROR( "[ObjectStateSerializer] Failed to parse SceneTransform for key '%#' on '%#'.",
									  key.c_str(),
									  pGameObject->getName().c_str() );
						return;
					}

					if ( bApplyTransforms )
					{
						pSceneComp->setLocalPosition( pos );
						pSceneComp->setLocalRotation( rot );
						pSceneComp->setLocalScale( scl );
					}

					++appliedCount;

					if ( parentKey.empty() == false )
					{
						PendingAttach link;
						link._pChild		  = pSceneComp;
						link._parentOwnerName = std::move( parentOwner );
						link._parentStableKey = std::move( parentKey );
						pending.push_back( std::move( link ) );
					}
				} );
				xmlBackend.iterateMap( "SceneTransforms", xformCb );

				if ( xformCount != sceneCompCount )
				{
					SW_LOG_ERROR( "[ObjectStateSerializer] SceneTransforms count mismatch on '%#': "
								  "xmlEntries=%# sceneComponents=%# applied=%#.",
								  pGameObject->getName().c_str(),
								  xformCount,
								  sceneCompCount,
								  appliedCount );
				}

				if ( outPendingAttaches == nullptr )
					applyPendingAttaches( pGameObject, pending );

				return true;
			}

		} // namespace
	} // namespace

	string ObjectStateSerializer::saveToXmlString( const GameObject* pGameObject )
	{
		if ( pGameObject == nullptr )
			return {};

		XmlDocumentBackend xmlBackend;
		xmlBackend.initXmlSerialization( "GameObjectState" );
		xmlBackend.writeValue( "Name", pGameObject->getName().c_str() );
		// ObjectId is emitted for debugging/diff only. Runtime IDs are process-local and must not be restored
		// (would collide with GameObject::_s_nextObjectId and break manager id maps).
		xmlBackend.writeValue( "ObjectId", to_string( pGameObject->getObjectId() ).c_str() );
		xmlBackend.writeValue( "IsActive", pGameObject->isActive() ? "true" : "false" );
		// Stable parent name (empty = root). Rebind after multi-GO load via rebindSceneHierarchy.
		const GameObject* pParentGo = pGameObject->getParent();
		xmlBackend.writeValue( "ParentGO", pParentGo != nullptr ? pParentGo->getName().c_str() : "" );

		// When TypeInfo is registered, also emit reflected PROPERTY fields via XmlSerializer
		// into a nested document stored as ReflectedXml (keeps GameObjectState root/aliases).
		const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
		if ( pTypeInfo != nullptr )
		{
			const string reflected =
				XmlSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo );
			if ( reflected.empty() == false )
				xmlBackend.writeValue( "ReflectedXml", reflected.c_str() );
		}

		xmlBackend.beginArray( "Tags" );
		for ( const TagID& tag : pGameObject->getTags().getTags() )
		{
			xmlBackend.writeArrayItem( formatTagId( tag ).c_str() );
		}
		xmlBackend.endArray();

		const vector<Component*>& listComps = pGameObject->getAllComponents();

		xmlBackend.beginMap( "Components" );
		for ( Component* pComp : listComps )
		{
			if ( pComp == nullptr )
				continue;

			xmlBackend.beginMapEntry();
			xmlBackend.writeMapKey( pComp->getComponentName().c_str() );

			xmlBackend.writeMapValue( serializeComponentXml( pComp ).c_str() );
			xmlBackend.endMapEntry();
		}
		xmlBackend.endMap();

		// SceneComponent local TRS + parent attach, keyed by stable component id.
		xmlBackend.beginMap( "SceneTransforms" );
		{
			unordered_map<string, int32> occurrence;
			for ( Component* pComp : listComps )
			{
				if ( pComp == nullptr )
					continue;
				SceneComponent* pSceneComp = pComp->asSceneComponent();
				if ( pSceneComp == nullptr )
					continue;

				string base;
				if ( pComp->getComponentName().empty() == false )
					base = pComp->getComponentName().c_str();
				else
					base = componentTypeBaseName( pComp );

				const int32	 occ = occurrence[base]++;
				const string key = makeStableComponentKey( pComp, occ );

				xmlBackend.beginMapEntry();
				xmlBackend.writeMapKey( key.c_str() );
				xmlBackend.writeMapValue( formatSceneTransform( pSceneComp ).c_str() );
				xmlBackend.endMapEntry();
			}
		}
		xmlBackend.endMap();

		return xmlBackend.endSerialize();
	}

	string ObjectStateSerializer::saveToJsonString( const GameObject* pGameObject )
	{
		if ( pGameObject == nullptr )
			return {};

		JsonDocument doc;
		JsonValue	 root = doc.makeObject();
		root.set( "Name" ).setString( pGameObject->getName().c_str() );
		root.set( "ObjectId" ).setString( to_string( pGameObject->getObjectId() ) );
		root.set( "IsActive" ).setBool( pGameObject->isActive() );

		const GameObject* pParentGo = pGameObject->getParent();
		root.set( "ParentGO" ).setString( pParentGo != nullptr ? pParentGo->getName().c_str() : "" );

		const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
		if ( pTypeInfo != nullptr )
		{
			const string reflected = JsonSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo );
			if ( reflected.empty() == false && reflected != "{}" )
			{
				JsonDocument reflectedDoc;
				if ( reflectedDoc.parse( reflected ) )
					root.set( "ReflectedJson" ).assignFrom( reflectedDoc.root() );
			}
		}

		JsonValue tagsJson = root.set( "Tags" );
		tagsJson.setArray();
		const auto& tags = pGameObject->getTags().getTags();
		for ( size_t tagIndex = 0; tagIndex < tags.size(); ++tagIndex )
		{
			tagsJson.pushBack().setString( formatTagId( tags[tagIndex] ) );
		}

		const vector<Component*>& listComps = pGameObject->getAllComponents();

		JsonValue compsJson = root.set( "Components" );
		compsJson.setObject();
		unordered_map<string, int32> compOccurrence;
		for ( Component* pComp : listComps )
		{
			if ( pComp == nullptr )
				continue;

			string base;
			if ( pComp->getComponentName().empty() == false )
				base = pComp->getComponentName().c_str();
			else
				base = componentTypeBaseName( pComp );
			const int32	 occ = compOccurrence[base]++;
			const string key = makeStableComponentKey( pComp, occ );

			string		 reflected = serializeComponentJson( pComp );
			JsonDocument reflectedDoc;
			if ( reflectedDoc.parse( reflected ) )
				compsJson.set( key, false ).assignFrom( reflectedDoc.root() );
			else
				compsJson.set( key, false ).setObject();
		}

		JsonValue xformJson = root.set( "SceneTransforms" );
		xformJson.setObject();
		unordered_map<string, int32> occurrence;
		for ( Component* pComp : listComps )
		{
			if ( pComp == nullptr )
				continue;
			SceneComponent* pSceneComp = pComp->asSceneComponent();
			if ( pSceneComp == nullptr )
				continue;

			string base;
			if ( pComp->getComponentName().empty() == false )
				base = pComp->getComponentName().c_str();
			else
				base = componentTypeBaseName( pComp );
			const int32	 occ = occurrence[base]++;
			const string key = makeStableComponentKey( pComp, occ );
			xformJson.set( key, false ).setString( formatSceneTransform( pSceneComp ) );
		}

		return doc.dump( 1 );
	}

	bool ObjectStateSerializer::saveToBinaryBuffer( const GameObject* pGameObject, vector<uint8>& outBuffer )
	{
		if ( pGameObject == nullptr )
			return false;

		BinaryStreamWriter writer( outBuffer );

		// 1. Name
		writer.writeString( pGameObject->getName().c_str() );

		// 2. Parent Name
		string parentName;
		if ( pGameObject->getParent() != nullptr )
			parentName = pGameObject->getParent()->getName().c_str();
		writer.writeString( parentName );

		// 3. IsActive
		writer.write( static_cast<uint8>( pGameObject->isActive() ? 1 : 0 ) );

		// 4. Tags
		const auto& tags = pGameObject->getTags().getTags();
		writer.write( static_cast<uint32>( tags.size() ) );
		for ( TagID tag : tags )
		{
			writer.writeString( formatTagId( tag ) );
		}

		// 5. Components
		auto listComponents = pGameObject->getAllComponents();
		writer.write( static_cast<uint32>( listComponents.size() ) );
		vector<uint8> compDataList; // Hoisted for performance
		for ( Component* pComp : listComponents )
		{
			if ( pComp == nullptr )
			{
				writer.writeString( "" );
				continue;
			}
			string typeName = pComp->getComponentName().c_str();
			if ( typeName.empty() )
			{
				const TypeInfo* pTi = pComp->getTypeInfo();
				if ( pTi != nullptr )
					typeName = pTi->_fullyQualifiedName.c_str();
			}
			writer.writeString( typeName );

			compDataList.clear();
			const TypeInfo* pTypeInfo = pComp->getTypeInfo();
			if ( pTypeInfo != nullptr )
			{
				BinarySerializer::serializeVersioned( kObjectReflectedSchemaVersion, pComp, *pTypeInfo, compDataList );
			}
			else
			{
				const Component::EcsDataView ecsData = pComp->ensureEcsData();
				if ( ecsData.instance != nullptr && ecsData.typeInfo != nullptr && ecsData.instance != pComp )
					BinarySerializer::serializeVersioned( kObjectReflectedSchemaVersion, ecsData.instance, *ecsData.typeInfo, compDataList );
			}
			writer.writeBytes( compDataList );
		}

		// 6. SceneComponent Attach Hierarchy (intra-GameObject)
		struct AttachRecord
		{
			uint32 childIdx;
			uint32 parentIdx;
		};
		vector<AttachRecord> listAttaches;
		for ( uint32 componentIndex = 0; componentIndex < listComponents.size(); ++componentIndex )
		{
			SceneComponent* pSc = listComponents[componentIndex] != nullptr ? listComponents[componentIndex]->asSceneComponent() : nullptr;
			if ( pSc != nullptr )
			{
				SceneComponent* pParentSc = pSc->getParent();
				if ( pParentSc != nullptr )
				{
					auto it = std::find( listComponents.begin(), listComponents.end(), pParentSc );
					if ( it != listComponents.end() )
						listAttaches.push_back( { componentIndex, static_cast<uint32>( std::distance( listComponents.begin(), it ) ) } );
				}
			}
		}
		writer.write( static_cast<uint32>( listAttaches.size() ) );
		for ( const auto& att : listAttaches )
		{
			writer.write( att.childIdx );
			writer.write( att.parentIdx );
		}

		return true;
	}

	size_t ObjectStateSerializer::loadFromBinaryBuffer( GameObject* pGameObject, const uint8* pData, size_t size, string& outParentName )
	{
		if ( pGameObject == nullptr || pData == nullptr || size == 0 )
			return 0;

		BinaryStreamReader reader( pData, size );

		string name;
		if ( reader.readString( name ) == false )
			return 0;
		if ( name.empty() == false )
			pGameObject->setName( hashed_string( name.c_str() ) );

		if ( reader.readString( outParentName ) == false )
			return 0;
		// 핫리로드에서는 부모 관계를 씬 레벨에서 rebind 할 수 있도록 outParentName 반환

		uint8 isActive = 1;
		if ( reader.read( isActive ) == false )
			return 0;
		pGameObject->setActive( isActive != 0 );

		uint32 numTags = 0;
		if ( reader.read( numTags ) == false )
			return 0;
		pGameObject->clearTags();
		for ( uint32 tagIndex = 0; tagIndex < numTags; ++tagIndex )
		{
			string tagStr;
			if ( reader.readString( tagStr ) == false )
				return 0;
			TagID tag;
			if ( parseTagId( tagStr, tag ) )
				pGameObject->addTag( tag );
		}

		uint32 numComps = 0;
		if ( reader.read( numComps ) == false )
			return 0;
		vector<Component*> listLoadedComponents;
		listLoadedComponents.reserve( numComps );

		for ( uint32 compIndex = 0; compIndex < numComps; ++compIndex )
		{
			string typeName;
			if ( reader.readString( typeName ) == false )
				return 0;
			vector<uint8> listCompData;
			if ( reader.readBytes( listCompData ) == false )
				return 0;

			if ( typeName.empty() )
			{
				listLoadedComponents.push_back( nullptr );
				continue;
			}

			Component* pComp = pGameObject->addComponentByName( hashed_string( typeName.c_str() ), false );
			listLoadedComponents.push_back( pComp );
			if ( pComp == nullptr )
				continue;

			if ( listCompData.empty() == false )
			{
				uint32			ver{ 0 };
				const TypeInfo* pTypeInfo = pComp->getTypeInfo();
				if ( pTypeInfo != nullptr )
				{
					BinarySerializer::deserializeVersioned( ver, pComp, *pTypeInfo, listCompData.data(), listCompData.size(), kObjectReflectedSchemaVersion );
				}
				else
				{
					const Component::EcsDataView ecsData = pComp->ensureEcsData();
					if ( ecsData.instance != nullptr && ecsData.typeInfo != nullptr && ecsData.instance != pComp )
					{
						BinarySerializer::deserializeVersioned( ver, ecsData.instance, *ecsData.typeInfo, listCompData.data(), listCompData.size(), kObjectReflectedSchemaVersion );
					}
				}
			}
		}

		// 6. SceneComponent Attach Hierarchy (intra-GameObject)
		uint32 numAttaches = 0;
		if ( reader.read( numAttaches ) )
		{
			for ( uint32 attachIndex = 0; attachIndex < numAttaches; ++attachIndex )
			{
				uint32 childIdx = 0, parentIdx = 0;
				if ( reader.read( childIdx ) && reader.read( parentIdx ) )
				{
					if ( childIdx < listLoadedComponents.size() && parentIdx < listLoadedComponents.size() )
					{
						auto* pChildSc	= listLoadedComponents[childIdx] != nullptr ? listLoadedComponents[childIdx]->asSceneComponent() : nullptr;
						auto* pParentSc = listLoadedComponents[parentIdx] != nullptr ? listLoadedComponents[parentIdx]->asSceneComponent() : nullptr;
						if ( pChildSc != nullptr && pParentSc != nullptr )
							pChildSc->attachToComponent( pParentSc );
					}
				}
			}
		}

		return reader.getOffset();
	}

	bool ObjectStateSerializer::loadFromXmlString( GameObject* pGameObject, string_view xmlString )
	{
		if ( pGameObject == nullptr || xmlString.empty() )
			return false;

		string			   xmlCopy( xmlString );
		XmlDocumentBackend xmlBackend;
		if ( xmlBackend.initXmlDeserialization( xmlCopy.c_str(), "GameObjectState" ) == false )
			return false;

		// Clear old components on the game object if any
		pGameObject->clearComponents();

		string nameStr;
		if ( xmlBackend.readValue( "Name", nameStr ) && nameStr.empty() == false )
			pGameObject->setName( hashed_string( nameStr.c_str() ) );

		// Intentionally skip ObjectId restore: IDs are runtime-allocated and not stable across sessions.

		string activeStr;
		if ( xmlBackend.readValue( "IsActive", activeStr ) && activeStr.empty() == false )
			pGameObject->setActive( activeStr == "true" || activeStr == "1" || activeStr == "yes" || activeStr == "on" || activeStr == "TRUE" || activeStr == "True" );

		string reflectedXml;
		if ( xmlBackend.readValue( "ReflectedXml", reflectedXml ) && reflectedXml.empty() == false )
		{
			const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
			if ( pTypeInfo != nullptr )
			{
				uint32 schemaVer{ 0 };
				if ( XmlSerializer::deserializeVersioned( schemaVer, pGameObject, *pTypeInfo, reflectedXml,
														  kObjectReflectedSchemaVersion ) == false )
					SW_LOG_WARNING( "[ObjectStateSerializer] ReflectedXml deserialize failed for %#", pTypeInfo->_fullyQualifiedName.c_str() );
			}
		}

		XmlArrayItemDelegate tagCb = SW_DELEGATE_LAMBDA( XmlArrayItemDelegate, [pGameObject]( string_view itemStr )
		{
			TagID tag{};
			if ( parseTagId( itemStr, tag ) )
				pGameObject->addTag( tag );
		} );
		xmlBackend.iterateArray( "Tags", tagCb );

		XmlMapItemDelegate compCb = SW_DELEGATE_LAMBDA( XmlMapItemDelegate, [pGameObject]( string_view keyStr, string_view valStr )
		{
			if ( keyStr.empty() )
				return;

			Component* pComp = pGameObject->addComponentByName(
				hashed_string( keyStr.data(), static_cast<uint32>( keyStr.size() ) ), false );
			if ( pComp == nullptr )
			{
				const string typeName( keyStr );
				// [Temp] 핫리로드 시 불필요한 고아(Orphan) 로그 스팸 억제
				// SW_LOG_WARNING( "[ObjectStateSerializer] Failed to re_create component '%#'", typeName.c_str() );
				return;
			}

			if ( valStr.empty() == false )
				deserializeComponentXml( pComp, valStr );
		} );
		xmlBackend.iterateMap( "Components", compCb );

		vector<PendingAttach> listPendingAttaches;
		collectAndApplySceneTransforms( pGameObject, xmlBackend, &listPendingAttaches, true );
		applyPendingAttaches( pGameObject, listPendingAttaches );

		// ParentGO may be missing on first pass of multi-GO restore; rebindSceneHierarchy fixes it.
		readAndApplyParentGO( pGameObject, xmlBackend, false );

		return true;
	}

	bool ObjectStateSerializer::loadFromJsonString( GameObject* pGameObject, string_view jsonString )
	{
		if ( pGameObject == nullptr || jsonString.empty() )
			return false;

		JsonDocument doc;
		if ( doc.parse( jsonString ) == false || doc.root().isObject() == false )
			return false;

		const JsonValue root = doc.root();
		pGameObject->clearComponents();

		const string nameStr = root.get( "Name" ).asString();
		if ( nameStr.empty() == false )
			pGameObject->setName( hashed_string( nameStr.c_str() ) );

		const JsonValue active = root.get( "IsActive" );
		if ( active.isValid() )
			pGameObject->setActive( active.asBool( pGameObject->isActive() ) );

		const JsonValue tags = root.get( "Tags" );
		if ( tags.isArray() )
		{
			for ( size_t tagIndex = 0; tagIndex < tags.size(); ++tagIndex )
			{
				TagID parsedTag{};
				if ( parseTagId( tags.at( tagIndex ).asString(), parsedTag ) )
					pGameObject->addTag( parsedTag );
			}
		}

		const JsonValue reflected = root.get( "ReflectedJson" );
		if ( reflected.isObject() )
		{
			const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
			if ( pTypeInfo != nullptr )
			{
				uint32 ver{ 0 };
				JsonSerializer::deserializeVersioned( ver, pGameObject, *pTypeInfo, reflected.dump() );
			}
		}

		const JsonValue children = root.get( "Children" );
		if ( children.isArray() && pGameObject->getManager() != nullptr )
		{
			for ( size_t childIndex = 0; childIndex < children.size(); ++childIndex )
			{
				const JsonValue childJson = children.at( childIndex );
				string			childName = childJson.get( "Name" ).asString();
				if ( childName.empty() )
					childName = "Child";
				GameObject* pChildGo = pGameObject->getManager()->createGameObject( hashed_string( childName.c_str() ) );
				if ( pChildGo != nullptr )
				{
					loadFromJsonString( pChildGo, childJson.dump() );
					pChildGo->attachToParent( pGameObject );
				}
			}
		}

		const JsonValue compsJson = root.get( "Components" );
		if ( compsJson.isObject() )
		{
			for ( const string& compName : compsJson.memberNames() )
			{
				size_t hashPos = compName.find( '#' );
				string baseName;
				if ( hashPos != string::npos )
					baseName = compName.substr( 0, hashPos );
				else
					baseName = compName;
				hashed_string typeName( baseName.c_str(), static_cast<uint32>( baseName.size() ) );
				Component*	  pComp = pGameObject->addComponentByName( typeName, false );
				if ( pComp == nullptr )
				{
					SW_LOG_ERROR( "loadFromJsonString: Failed to add component %#", compName.c_str() );
					continue;
				}
				pComp->setComponentName( typeName );
				deserializeComponentJson( pComp, compsJson.get( compName, false ).dump() );
			}
		}

		const JsonValue				 xformJson = root.get( "SceneTransforms" );
		unordered_map<string, int32> occurrence;
		for ( Component* pComp : pGameObject->getAllComponents() )
		{
			if ( pComp == nullptr )
				continue;
			SceneComponent* pSceneComp = pComp->asSceneComponent();
			if ( pSceneComp == nullptr )
				continue;

			string			base  = pComp->getComponentName().empty() ? componentTypeBaseName( pComp ) : pComp->getComponentName().c_str();
			const int32		occ	  = occurrence[base]++;
			const string	key	  = makeStableComponentKey( pComp, occ );
			const JsonValue xform = xformJson.get( key, false );
			if ( xform.isValid() == false )
				continue;
			float3 pos, rot, scl;
			string pOwner, pKey;
			if ( parseSceneTransform( xform.asString(), pos, rot, scl, pOwner, pKey ) )
			{
				pSceneComp->setLocalPosition( pos );
				pSceneComp->setLocalRotation( rot );
				pSceneComp->setLocalScale( scl );
			}
		}

		return true;
	}

	bool ObjectStateSerializer::rebindSceneHierarchy( GameObject* pGameObject, string_view xmlString )
	{
		if ( pGameObject == nullptr || xmlString.empty() )
			return false;

		string			   xmlCopy( xmlString );
		XmlDocumentBackend xmlBackend;
		if ( xmlBackend.initXmlDeserialization( xmlCopy.c_str(), "GameObjectState" ) == false )
			return false;

		// Re-resolve parents only (transforms already applied during load). Needed after multi-GO restore
		// so cross-GO attaches see fully rebuilt component lists.
		vector<PendingAttach> listPendingAttaches;
		collectAndApplySceneTransforms( pGameObject, xmlBackend, &listPendingAttaches, false );
		applyPendingAttaches( pGameObject, listPendingAttaches );

		// Second pass: GameObject parent by stable name (all GOs must exist).
		readAndApplyParentGO( pGameObject, xmlBackend, true );
		return true;
	}

	bool ObjectStateSerializer::rebindSceneHierarchyFromJson( GameObject* pGameObject, string_view jsonString )
	{
		if ( pGameObject == nullptr || jsonString.empty() )
			return false;

		JsonDocument doc;
		if ( doc.parse( jsonString ) == false || doc.root().isObject() == false )
			return false;

		const JsonValue root	 = doc.root();
		const string	parentGo = root.get( "ParentGO" ).asString();
		if ( parentGo.empty() == false && pGameObject->getManager() != nullptr )
		{
			GameObject* pParent = pGameObject->getManager()->findGameObjectByName( hashed_string( parentGo.c_str() ) );
			if ( pParent != nullptr )
				pGameObject->attachToParent( pParent );
		}

		const JsonValue				 xformJson = root.get( "SceneTransforms" );
		unordered_map<string, int32> occurrence;
		for ( Component* pComp : pGameObject->getAllComponents() )
		{
			if ( pComp == nullptr )
				continue;
			SceneComponent* pSceneComp = pComp->asSceneComponent();
			if ( pSceneComp == nullptr )
				continue;

			string			base  = pComp->getComponentName().empty() ? componentTypeBaseName( pComp ) : pComp->getComponentName().c_str();
			const int32		occ	  = occurrence[base]++;
			const string	key	  = makeStableComponentKey( pComp, occ );
			const JsonValue xform = xformJson.get( key, false );
			if ( xform.isValid() == false )
				continue;
			float3 pos, rot, scl;
			string pOwner, pKey;
			if ( parseSceneTransform( xform.asString(), pos, rot, scl, pOwner, pKey ) )
			{
				if ( pOwner.empty() == false && pKey.empty() == false )
				{
					SceneComponent* pParentSceneComp = resolveParentSceneComponent( pGameObject, pOwner, pKey );
					if ( pParentSceneComp != nullptr )
						pSceneComp->attachToComponent( pParentSceneComp );
				}
			}
		}

		const JsonValue children = root.get( "Children" );
		if ( children.isArray() && pGameObject->getManager() != nullptr )
		{
			for ( size_t childIndex = 0; childIndex < children.size(); ++childIndex )
			{
				const JsonValue childJson = children.at( childIndex );
				const string	childName = childJson.get( "Name" ).asString();
				GameObject*		pChildGo  = pGameObject->getManager()->findGameObjectByName( hashed_string( childName.c_str() ) );
				if ( pChildGo != nullptr )
					rebindSceneHierarchyFromJson( pChildGo, childJson.dump() );
			}
		}

		return true;
	}

	bool ObjectStateSerializer::saveToXmlFile( const GameObject* pGameObject, string_view filePath )
	{
		string xmlStr = saveToXmlString( pGameObject );
		if ( xmlStr.empty() )
			return false;

		return FileUtil::writeFile( string{ filePath }, reinterpret_cast<const uint8*>( xmlStr.data() ), xmlStr.size() );
	}

	bool ObjectStateSerializer::loadFromXmlFile( GameObject* pGameObject, string_view filePath )
	{
		vector<uint8> listData;
		if ( FileUtil::readFile( string{ filePath }, listData ) == false || listData.empty() )
			return false;

		string_view xmlStr( reinterpret_cast<const utf8*>( listData.data() ), listData.size() );
		return loadFromXmlString( pGameObject, xmlStr );
	}

	void ObjectStateSerializer::openSaveFileDialog( const GameObject* pGameObject, FileDialogDelegate onSaveDone )
	{
		FileDialogParams params;
		params._type				= FileDialogParams::Type::Save;
		params._description			= "GameObject State XML File (*.xml)";
		params._filterExtensionList = { "xml" };
		params._bEnableMultiselect	= false;

		FileDialogDelegate del = SW_DELEGATE_LAMBDA( FileDialogDelegate, [pGameObject, onSaveDone]( const vector<string>& fileNames )
		{
			if ( fileNames.empty() == false && pGameObject != nullptr )
				saveToXmlFile( pGameObject, fileNames.front() );
			if ( onSaveDone.isBound() )
				onSaveDone( fileNames );
		} );
		FileUtil::openFileDialog( params, del );
	}

	void ObjectStateSerializer::openLoadFileDialog( GameObject* pGameObject, FileDialogDelegate onLoadDone )
	{
		FileDialogParams params;
		params._type				= FileDialogParams::Type::Open;
		params._description			= "GameObject State XML File (*.xml)";
		params._filterExtensionList = { "xml" };
		params._bEnableMultiselect	= false;

		FileDialogDelegate del = SW_DELEGATE_LAMBDA( FileDialogDelegate, [pGameObject, onLoadDone]( const vector<string>& fileNames )
		{
			if ( fileNames.empty() == false && pGameObject != nullptr )
				loadFromXmlFile( pGameObject, fileNames.front() );
			if ( onLoadDone.isBound() )
				onLoadDone( fileNames );
		} );
		FileUtil::openFileDialog( params, del );
	}
} // namespace sw
