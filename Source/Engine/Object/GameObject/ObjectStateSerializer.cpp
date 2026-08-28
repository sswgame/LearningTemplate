#include "pch.h"

#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Engine/Reflection/ReflectionCast.h"
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

		/** @brief Tags 배열 복원으로 이미 생긴 TagComponent는 재사용합니다. */
		Component* addOrReuseComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning )
		{
			if ( pGameObject == nullptr )
				return nullptr;
			if ( typeName == hashed_string( "TagComponent" ) )
			{
				TagComponent* pExisting = pGameObject->getComponent<TagComponent>();
				if ( pExisting != nullptr )
					return pExisting;
			}
			GameObjectManager* pManager = pGameObject->getManager();
			if ( pManager == nullptr )
				return nullptr;
			return pManager->addComponentByName( pGameObject, typeName, bLogWarning );
		}

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

			/** @brief Type name, else FQN, else component name, else "Component". */
			string componentTypeBaseName( const Component* pComp )
			{
				if ( pComp == nullptr )
					return "Component";

				if ( pComp->getComponentName().empty() == false )
					return pComp->getComponentName().c_str();

				const TypeInfo* pTypeInfo = pComp->getTypeInfo();
				if ( pTypeInfo != nullptr )
				{
					if ( pTypeInfo->_name.empty() == false )
						return pTypeInfo->_name.c_str();
					if ( pTypeInfo->_fullyQualifiedName.empty() == false )
						return pTypeInfo->_fullyQualifiedName.c_str();
				}

				return "Component";
			}

			/** @brief Attach 참조용 안정 키. 타입(또는 컴포넌트 이름) + 같은 GO 안 출현 순번. */
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
				SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
				if ( pSceneComp != nullptr )
					pSceneComp->markTransformDirty();
			}

			string findStableComponentKey( const Component* pComp )
			{
				if ( pComp == nullptr || pComp->getOwner() == nullptr )
					return {};

				unordered_map<string, int32> occurrence;
				for ( Component* pOther : pComp->getOwner()->getAllComponents() )
				{
					if ( pOther == nullptr )
						continue;
					string base;
					if ( pOther->getComponentName().empty() == false )
						base = pOther->getComponentName().c_str();
					else
						base = componentTypeBaseName( pOther );
					const int32 occ = occurrence[base]++;
					if ( pOther == pComp )
						return makeStableComponentKey( pComp, occ );
				}
				return {};
			}

			void* createOwnedComponent( void* pOuter, hashed_string typeName )
			{
				GameObject* pGameObject = static_cast<GameObject*>( pOuter );
				if ( pGameObject == nullptr || pGameObject->getManager() == nullptr )
					return nullptr;
				return pGameObject->getManager()->addComponentByName( pGameObject, typeName, false );
			}

			const TypeInfo* getComponentRuntimeTypeInfo( const void* pInstance )
			{
				const Component* pComp = static_cast<const Component*>( pInstance );
				if ( pComp == nullptr || pComp->isPendingKill() )
					return nullptr;
				return pComp->getTypeInfo();
			}

			SerializeContext makeGameObjectXmlContext( GameObject* pGameObject )
			{
				SerializeContext ctx = SerializeContext::getDefault();
				ctx.setOuterInstance( pGameObject );
				ctx.setOwnedPointerFactory( &createOwnedComponent );
				ctx.setRuntimeTypeInfoFn( &getComponentRuntimeTypeInfo );
				return ctx;
			}

			void writeAttachJson( JsonValue dest, const SceneComponent* pSceneComp )
			{
				if ( dest.isObject() == false || pSceneComp == nullptr )
					return;
				SceneComponent* pParent = pSceneComp->getParent();
				if ( pParent == nullptr )
					return;
				GameObject* pParentOwner = pParent->getOwner();
				if ( pParentOwner == nullptr )
					return;
				const string parentKey = findStableComponentKey( pParent );
				if ( parentKey.empty() )
					return;
				dest.set( "AttachOwner" ).setString( pParentOwner->getName().c_str() );
				dest.set( "AttachComponent" ).setString( parentKey.c_str() );
			}

			bool readAttachJson( const JsonValue src, string& outOwner, string& outKey )
			{
				outOwner.clear();
				outKey.clear();
				if ( src.isObject() == false )
					return false;
				outKey = src.get( "AttachComponent" ).asString();
				if ( outKey.empty() )
					return false;
				outOwner = src.get( "AttachOwner" ).asString();
				return true;
			}

			struct PendingAttach
			{
				SceneComponent* _pChild{ nullptr };
				string			_parentOwnerName;
				string			_parentStableKey;
			};

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

					const TypeInfo* pTypeInfo = pComp->getTypeInfo();
					if ( pTypeInfo != nullptr )
					{
						if ( pTypeInfo->_name.empty() == false )
							outMap.emplace( string( pTypeInfo->_name.c_str() ) + '#' + to_string( occ ), pComp );
						if ( pTypeInfo->_fullyQualifiedName.empty() == false )
							outMap.emplace( string( pTypeInfo->_fullyQualifiedName.c_str() ) + '#' + to_string( occ ), pComp );
					}
				}
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
				return castTo<SceneComponent>( it->second );
			}

			void applyPendingAttaches( GameObject* pGameObject, const vector<PendingAttach>& pendingAttaches )
			{
				if ( pGameObject == nullptr || pendingAttaches.empty() )
					return;

				const string_view ownerName = pGameObject->getName().c_str();
				for ( const PendingAttach& link : pendingAttaches )
				{
					if ( link._pChild == nullptr || link._parentStableKey.empty() )
						continue;

					// Cross-GO parents are rebound via ParentGO so primary SceneComponents stay aligned
					// with GameObject::attachToParent. Attach* attributes only restore in-GO SC trees.
					const bool bCrossGo = link._parentOwnerName.empty() == false &&
										  string_view( link._parentOwnerName ) != ownerName;
					if ( bCrossGo )
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

		} // namespace
	} // namespace

	string ObjectStateSerializer::saveToXmlString( const GameObject* pGameObject )
	{
		if ( pGameObject == nullptr )
			return {};

		pGameObject->prepareSerialize();

		const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
		if ( pTypeInfo == nullptr )
			return {};

		SerializeContext ctx = makeGameObjectXmlContext( const_cast<GameObject*>( pGameObject ) );
		return XmlSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, ctx );
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
			JsonValue	 dest = compsJson.set( key, false );
			if ( reflectedDoc.parse( reflected ) && reflectedDoc.root().isObject() )
				dest.assignFrom( reflectedDoc.root() );
			else
				dest.setObject();

			SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
			if ( pSceneComp != nullptr )
				writeAttachJson( dest, pSceneComp );
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
				BinarySerializer::serializeVersioned( kObjectReflectedSchemaVersion, pComp, *pTypeInfo, compDataList );
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
			SceneComponent* pSc = castTo<SceneComponent>( listComponents[componentIndex] );
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

			Component* pComp = addOrReuseComponentByName( pGameObject, hashed_string( typeName.c_str() ), false );
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
				SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
				if ( pSceneComp != nullptr )
					pSceneComp->markTransformDirty();
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
						SceneComponent* pChildSc  = castTo<SceneComponent>( listLoadedComponents[childIdx] );
						SceneComponent* pParentSc = castTo<SceneComponent>( listLoadedComponents[parentIdx] );
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

		const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
		if ( pTypeInfo == nullptr )
			return false;

		const hashed_string oldName = pGameObject->getName();
		pGameObject->clearComponents();

		SerializeContext ctx = makeGameObjectXmlContext( pGameObject );
		uint32			 ver{ 0 };
		if ( XmlSerializer::deserializeVersioned( ver, pGameObject, *pTypeInfo, xmlString, kObjectReflectedSchemaVersion,
												  nullptr, nullptr, ctx ) == false )
			return false;

		if ( pGameObject->getName() != oldName && pGameObject->getManager() != nullptr )
			pGameObject->getManager()->notifyNameChanged( pGameObject, oldName, pGameObject->getName() );

		pGameObject->setActive( pGameObject->isActive() );
		pGameObject->applyLoadedHierarchy();
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

		const JsonValue			  compsJson = root.get( "Components" );
		vector<PendingAttach>	  listPendingAttaches;
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
				Component*	  pComp = addOrReuseComponentByName( pGameObject, typeName, false );
				if ( pComp == nullptr )
				{
					SW_LOG_ERROR( "loadFromJsonString: Failed to add component %#", compName.c_str() );
					continue;
				}
				pComp->setComponentName( typeName );
				const JsonValue compJson = compsJson.get( compName, false );
				deserializeComponentJson( pComp, compJson.dump() );

				SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
				if ( pSceneComp != nullptr )
				{
					string parentOwner;
					string parentKey;
					if ( readAttachJson( compJson, parentOwner, parentKey ) )
					{
						PendingAttach link;
						link._pChild		  = pSceneComp;
						link._parentOwnerName = std::move( parentOwner );
						link._parentStableKey = std::move( parentKey );
						listPendingAttaches.push_back( std::move( link ) );
					}
				}
			}
		}

		applyPendingAttaches( pGameObject, listPendingAttaches );

		return true;
	}

	bool ObjectStateSerializer::rebindSceneHierarchy( GameObject* pGameObject, string_view xmlString )
	{
		(void)xmlString;
		if ( pGameObject == nullptr )
			return false;

		pGameObject->applyLoadedHierarchy();
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

		const JsonValue			 compsJson = root.get( "Components" );
		vector<PendingAttach>	 listPendingAttaches;
		if ( compsJson.isObject() )
		{
			unordered_map<string, Component*> keyMap;
			buildStableComponentKeyMap( pGameObject, keyMap );
			for ( const string& compName : compsJson.memberNames() )
			{
				const auto mapIt = keyMap.find( compName );
				Component* pComp = mapIt != keyMap.end() ? mapIt->second : nullptr;
				SceneComponent* pSceneComp = castTo<SceneComponent>( pComp );
				if ( pSceneComp == nullptr )
					continue;

				string parentOwner;
				string parentKey;
				if ( readAttachJson( compsJson.get( compName, false ), parentOwner, parentKey ) == false )
					continue;

				PendingAttach link;
				link._pChild		  = pSceneComp;
				link._parentOwnerName = std::move( parentOwner );
				link._parentStableKey = std::move( parentKey );
				listPendingAttaches.push_back( std::move( link ) );
			}
		}
		applyPendingAttaches( pGameObject, listPendingAttaches );

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
