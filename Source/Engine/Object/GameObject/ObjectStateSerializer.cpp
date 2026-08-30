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

namespace sw
{
	namespace
	{
		struct ObjectStateSerializerInternal
		{
			/** @brief Tags 배열 복원으로 이미 생긴 TagComponent는 재사용합니다. */
			static Component* addOrReuseComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning )
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

			static string formatTagId( const TagID& tag )
			{
				if ( tag._pString != nullptr )
					return string( "str:" ) + tag._pString;
				return to_string( tag._id );
			}

			static bool parseTagId( string_view text, TagID& outTag )
			{
				outTag = TagID{};
				if ( text.empty() )
					return false;

				if ( text.size() >= 4 && text.substr( 0, 4 ) == "str:" )
				{
					outTag = TagID::request( text.substr( 4 ) );
					return outTag.isValid();
				}

				const fixed_string<constant::kMaxBuffer64> tokenNt{ text };
				outTag._id = StringUtil::strtoull( tokenNt.c_str() );
				return outTag.isValid();
			}

			static void* createOwnedComponent( void* pOuter, hashed_string typeName )
			{
				GameObject* pGameObject = static_cast<GameObject*>( pOuter );
				if ( pGameObject == nullptr || pGameObject->getManager() == nullptr )
					return nullptr;
				return pGameObject->getManager()->addComponentByName( pGameObject, typeName, false );
			}

			static const TypeInfo* getComponentRuntimeTypeInfo( const void* pInstance )
			{
				const Component* pComp = static_cast<const Component*>( pInstance );
				if ( pComp == nullptr || pComp->isPendingKill() )
					return nullptr;
				return pComp->getTypeInfo();
			}

			static SerializeContext makeGameObjectXmlContext( GameObject* pGameObject )
			{
				SerializeContext ctx = SerializeContext::getDefault();
				ctx.setOuterInstance( pGameObject );
				ctx.setOwnedPointerFactory( &createOwnedComponent );
				ctx.setRuntimeTypeInfoFn( &getComponentRuntimeTypeInfo );
				return ctx;
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	string ObjectStateSerializer::saveToXmlString( const GameObject* pGameObject )
	{
		if ( pGameObject == nullptr )
			return {};

		pGameObject->prepareSerialize();

		const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
		if ( pTypeInfo == nullptr )
			return {};

		SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( const_cast<GameObject*>( pGameObject ) );
		return XmlSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, ctx );
	}

	string ObjectStateSerializer::saveToJsonString( const GameObject* pGameObject )
	{
		if ( pGameObject == nullptr )
			return {};

		pGameObject->prepareSerialize();

		const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
		if ( pTypeInfo == nullptr )
			return {};

		SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( const_cast<GameObject*>( pGameObject ) );
		return JsonSerializer::serializeVersioned( kObjectReflectedSchemaVersion, pGameObject, *pTypeInfo, ctx );
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
			writer.writeString( ObjectStateSerializerInternal::formatTagId( tag ) );
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
			uint32 _childIdx;
			uint32 _parentIdx;
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
			writer.write( att._childIdx );
			writer.write( att._parentIdx );
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
			if ( ObjectStateSerializerInternal::parseTagId( tagStr, tag ) )
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

			Component* pComp = ObjectStateSerializerInternal::addOrReuseComponentByName( pGameObject, hashed_string( typeName.c_str() ), false );
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

		SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( pGameObject );
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

		const TypeInfo* pTypeInfo = pGameObject->getTypeInfo();
		if ( pTypeInfo == nullptr )
			return false;

		const hashed_string oldName = pGameObject->getName();
		pGameObject->clearComponents();

		SerializeContext ctx = ObjectStateSerializerInternal::makeGameObjectXmlContext( pGameObject );
		uint32			 ver{ 0 };
		if ( JsonSerializer::deserializeVersioned( ver, pGameObject, *pTypeInfo, jsonString, kObjectReflectedSchemaVersion,
												   nullptr, nullptr, ctx ) == false )
			return false;

		if ( pGameObject->getName() != oldName && pGameObject->getManager() != nullptr )
			pGameObject->getManager()->notifyNameChanged( pGameObject, oldName, pGameObject->getName() );

		pGameObject->setActive( pGameObject->isActive() );
		pGameObject->applyLoadedHierarchy();
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
		(void)jsonString;
		if ( pGameObject == nullptr )
			return false;

		pGameObject->applyLoadedHierarchy();
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
		params._listFilterExtension = { "xml" };
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
		params._listFilterExtension = { "xml" };
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
