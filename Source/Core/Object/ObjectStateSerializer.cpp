/**
 * @file ObjectStateSerializer.cpp
 * @brief ObjectStateSerializer 구현
 */
#include "pch.h"
#include "ObjectStateSerializer.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/Component.h"
#include "Core/Object/SceneComponent.h"
#include "Core/Object/TagSystem.h"
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

		/** @brief SceneComponent 로컬 TRS + 동일 GO 내 부모 flat-index. parentIdx=-1 이면 루트. */
		std::string formatSceneTransform( const SceneComponent* sceneComp, int32 parentFlatIndex )
		{
			std::string out = formatFloat3( sceneComp->getLocalPosition() );
			out += ';';
			out += formatFloat3( sceneComp->getLocalRotation() );
			out += ';';
			out += formatFloat3( sceneComp->getLocalScale() );
			out += ';';
			out += std::to_string( parentFlatIndex );
			return out;
		}

		bool parseSceneTransform( std::string_view text, float3& outPos, float3& outRot, float3& outScl, int32& outParentIdx )
		{
			outPos		 = float3{};
			outRot		 = float3{};
			outScl		 = float3( 1.0f, 1.0f, 1.0f );
			outParentIdx = -1;

			std::string_view parts[4];
			size_t			 start	  = 0;
			size_t			 partCount = 0;
			while ( partCount < 4 && start <= text.size() )
			{
				const size_t sep = text.find( ';', start );
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

			if ( partCount >= 4 && parts[3].empty() == false )
			{
				try
				{
					outParentIdx = static_cast<int32>( std::stoi( std::string( parts[3] ) ) );
				}
				catch ( ... )
				{
					outParentIdx = -1;
				}
			}
			return true;
		}

		int32 findFlatComponentIndex( const GameObject* gameObject, const Component* target )
		{
			if ( gameObject == nullptr || target == nullptr )
				return -1;

			const std::vector<Component*>& comps = gameObject->getAllComponents();
			for ( size_t i = 0; i < comps.size(); ++i )
			{
				if ( comps[i] == target )
					return static_cast<int32>( i );
			}
			return -1;
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

		// SceneComponent local TRS (+ same-GO parent flat index). Keyed by flat component index.
		xmlBackend.beginMap( "SceneTransforms" );
		for ( size_t i = 0; i < comps.size(); ++i )
		{
			Component* comp = comps[i];
			if ( comp == nullptr )
				continue;
			SceneComponent* sceneComp = comp->asSceneComponent();
			if ( sceneComp == nullptr )
				continue;

			int32 parentIdx = -1;
			if ( SceneComponent* parent = sceneComp->getParent() )
			{
				if ( parent->getOwner() == gameObject )
					parentIdx = findFlatComponentIndex( gameObject, parent );
			}

			xmlBackend.beginMapEntry();
			xmlBackend.writeMapKey( std::to_string( i ).c_str() );
			xmlBackend.writeMapValue( formatSceneTransform( sceneComp, parentIdx ).c_str() );
			xmlBackend.endMapEntry();
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

		// Apply SceneComponent transforms after components exist; then re-attach same-GO parents.
		struct PendingAttach
		{
			int32 childIdx  = -1;
			int32 parentIdx = -1;
		};
		std::vector<PendingAttach> pendingAttaches;

		XmlMapItemDelegate xformCb = SW_DELEGATE_LAMBDA( XmlMapItemDelegate,
														 [gameObject, &pendingAttaches]( std::string_view keyStr, std::string_view valStr )
		{
			if ( keyStr.empty() || valStr.empty() )
				return;

			int32 flatIdx = -1;
			try
			{
				flatIdx = static_cast<int32>( std::stoi( std::string( keyStr ) ) );
			}
			catch ( ... )
			{
				return;
			}

			const std::vector<Component*>& comps = gameObject->getAllComponents();
			if ( flatIdx < 0 || static_cast<size_t>( flatIdx ) >= comps.size() )
				return;

			Component* comp = comps[static_cast<size_t>( flatIdx )];
			if ( comp == nullptr )
				return;
			SceneComponent* sceneComp = comp->asSceneComponent();
			if ( sceneComp == nullptr )
				return;

			float3 pos{};
			float3 rot{};
			float3 scl{ 1.0f, 1.0f, 1.0f };
			int32  parentIdx = -1;
			if ( parseSceneTransform( valStr, pos, rot, scl, parentIdx ) == false )
			{
				SW_LOG_WARNING( "[ObjectStateSerializer] Failed to parse SceneTransform for index %#", flatIdx );
				return;
			}

			sceneComp->setLocalPosition( pos );
			sceneComp->setLocalRotation( rot );
			sceneComp->setLocalScale( scl );

			if ( parentIdx >= 0 )
				pendingAttaches.push_back( PendingAttach{ flatIdx, parentIdx } );
		} );
		xmlBackend.iterateMap( "SceneTransforms", xformCb );

		{
			const std::vector<Component*>& comps = gameObject->getAllComponents();
			for ( const PendingAttach& link : pendingAttaches )
			{
				if ( link.childIdx < 0 || link.parentIdx < 0 )
					continue;
				if ( static_cast<size_t>( link.childIdx ) >= comps.size() ||
					 static_cast<size_t>( link.parentIdx ) >= comps.size() )
					continue;

				SceneComponent* child  = comps[static_cast<size_t>( link.childIdx )] != nullptr
											 ? comps[static_cast<size_t>( link.childIdx )]->asSceneComponent()
											 : nullptr;
				SceneComponent* parent = comps[static_cast<size_t>( link.parentIdx )] != nullptr
											 ? comps[static_cast<size_t>( link.parentIdx )]->asSceneComponent()
											 : nullptr;
				if ( child == nullptr || parent == nullptr )
					continue;

				child->attachToComponent( parent );
			}
		}

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
