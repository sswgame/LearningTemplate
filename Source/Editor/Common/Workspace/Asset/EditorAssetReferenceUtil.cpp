#include "pch.h"

#include "Editor/Common/Workspace/Asset/EditorAssetReferenceUtil.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/EditorSceneIterationAccess.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentAssetPathUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Serializer/AssetRefBase.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAssetReferenceUtilInternal
        {
            /** @brief 에셋 경로 칸 @p prop 의 값을 @p newPath 로 씁니다. 읽기(`ComponentAssetPathUtil::readAssetPath`)가 받는 형식(`AssetRef` · `string` · `hashed_string`)만 쓴다. */
            static bool writeAssetPath( const PropertyInfo& prop, Component& component, string_view newPath )
            {
                static const hashed_string s_assetRefType( AssetRefBase::kTypeName );
                if ( prop._typeName == s_assetRefType )
                {
                    AssetRefBase* const pValue = prop.getValuePtr<AssetRefBase>( &component );
                    return pValue != nullptr && pValue->assign( newPath ); // 종류가 맞지 않으면 assign 이 이유를 남긴다 — 옮김은 접미사를 지켜 오지 않는다
                }
                const ReflectBuiltinIndex builtinIndex = ReflectedValueUtil::findBuiltinIndex( prop._typeName );
                if ( builtinIndex == ReflectBuiltinOf<string>::kIndex )
                {
                    string* const pValue = prop.getValuePtr<string>( &component );
                    if ( pValue == nullptr )
                        return false;
                    *pValue = string{ newPath };
                    return true;
                }
                if ( builtinIndex == ReflectBuiltinOf<hashed_string>::kIndex )
                {
                    hashed_string* const pValue = prop.getValuePtr<hashed_string>( &component );
                    if ( pValue == nullptr )
                        return false;
                    *pValue = hashed_string( newPath );
                    return true;
                }
                return false;
            }

            /** @brief 원소 하나(@p pElement, 형식 @p elementTypeName)가 @p mapOldToNew 의 옛 id 면 새 id 로 씁니다. `AssetRef` · `string` · `hashed_string` 원소만. */
            static bool remapElement( void* pElement, const hashed_string& elementTypeName, const unordered_map<string, string>& mapOldToNew )
            {
                static const hashed_string s_assetRefType( AssetRefBase::kTypeName );
                if ( elementTypeName == s_assetRefType )
                {
                    AssetRefBase* const pValue = static_cast<AssetRefBase*>( pElement );
                    const auto          it     = pValue->isBuiltin() ? mapOldToNew.end() : mapOldToNew.find( pValue->path() );
                    return it != mapOldToNew.end() && pValue->assign( it->second );
                }
                const ReflectBuiltinIndex builtinIndex = ReflectedValueUtil::findBuiltinIndex( elementTypeName );
                if ( builtinIndex == ReflectBuiltinOf<string>::kIndex )
                {
                    string* const pValue = static_cast<string*>( pElement );
                    const auto    it     = mapOldToNew.find( *pValue );
                    if ( it == mapOldToNew.end() )
                        return false;
                    *pValue = it->second;
                    return true;
                }
                if ( builtinIndex == ReflectBuiltinOf<hashed_string>::kIndex )
                {
                    hashed_string* const pValue = static_cast<hashed_string*>( pElement );
                    const auto           it     = mapOldToNew.find( string{ pValue->c_str() } );
                    if ( it == mapOldToNew.end() )
                        return false;
                    *pValue = hashed_string( it->second );
                    return true;
                }
                return false;
            }

            /** @brief 에셋 경로 원소의 시퀀스 칸(`vector<AssetRef<T>>` · `PROPERTY( AssetPath ) vector<string>`)의 원소를 고칩니다. 바꾼 원소 수입니다. */
            static uint32 remapSequence( const PropertyInfo& prop, Component& component, const unordered_map<string, string>& mapOldToNew )
            {
                const ISequenceContainerWrapper* const pSequence  = prop.hasContainerWrapper() ? prop._containerWrapper->asSequence() : nullptr;
                void* const                            pContainer = prop.getValuePtr<void>( &component );
                if ( pSequence == nullptr || pContainer == nullptr || pSequence->allowsInPlaceElementWrite() == false )
                    return 0;
                uint32 changedCount{ 0 };
                for ( size_t index = 0; index < pSequence->getSize( pContainer ); ++index )
                {
                    if ( remapElement( pSequence->getElement( pContainer, index ), prop._elementTypeName, mapOldToNew ) )
                        ++changedCount;
                }
                return changedCount;
            }
        };
    } // namespace

    uint32 EditorAssetReferenceUtil::remapComponent( Component& component, const unordered_map<string, string>& mapOldToNew, vector<hashed_string>& outListChanged )
    {
        const TypeInfo* const pType = component.getTypeInfo();
        if ( pType == nullptr || mapOldToNew.empty() )
            return 0;
        uint32 changedCount{ 0 };
        pType->forEachProperty( [&component, &mapOldToNew, &outListChanged, &changedCount]( const PropertyInfo& prop )
        {
            if ( prop._bIsContainer == SW_TRUE )
            {
                if ( prop._metadata->_bAssetPath == SW_FALSE && prop._metadata->_assetType.empty() )
                    return;
                const uint32 elementCount = EditorAssetReferenceUtilInternal::remapSequence( prop, component, mapOldToNew );
                if ( elementCount > 0 )
                {
                    outListChanged.push_back( prop._name );
                    changedCount += elementCount;
                }
                return;
            }
            const string_view path = ComponentAssetPathUtil::readAssetPath( prop, &component );
            if ( path.empty() )
                return;
            const auto it = mapOldToNew.find( string{ path } );
            if ( it == mapOldToNew.end() || EditorAssetReferenceUtilInternal::writeAssetPath( prop, component, it->second ) == false )
                return;
            outListChanged.push_back( prop._name );
            ++changedCount;
        }, true ); // 상속 칸도 — 기반 컴포넌트의 경로 칸이 빠지지 않게
        return changedCount;
    }

    uint32 EditorAssetReferenceUtil::remapScene( Scene& scene, const unordered_map<string, string>& mapOldToNew )
    {
        GameObjectManager* const pObjects = scene.getObjectManager();
        if ( pObjects == nullptr || mapOldToNew.empty() )
            return 0;
        PrefabInstanceRegistry& prefabs = pObjects->getSubsystem<PrefabInstanceRegistry>();
        uint32                  changedCount{ 0 };
        pObjects->forEachGameObject( editor::EditorSceneIterationAccess::getKey(), [&scene, &mapOldToNew, &prefabs, &changedCount]( GameObject* pObject )
        {
            const uint64 objectID = pObject->getObjectID();
            const auto   itPrefab = mapOldToNew.find( string{ scene.getEntityPrefabPath( objectID ) } );
            if ( itPrefab != mapOldToNew.end() )
            {
                scene.setEntityPrefabPath( objectID, itPrefab->second ); // 다음 저장이 엔티티의 prefab 에 새 id 를 쓴다
                ++changedCount;
            }
            const PrefabNestedPart* const pPart  = prefabs.findNestedPart( objectID );
            const auto                    itPart = pPart != nullptr ? mapOldToNew.find( pPart->_prefabPath ) : mapOldToNew.end();
            if ( itPart != mapOldToNew.end() )
            {
                PrefabNestedPart part = *pPart;
                part._prefabPath      = itPart->second;
                prefabs.restoreNestedPart( objectID, part ); // 기록 순서를 지킨다(새로 등록하면 부품 순서가 바뀐다)
                ++changedCount;
            }
            for ( Component* pComponent : pObject->getComponents() )
            {
                if ( pComponent == nullptr || pComponent->isPendingDestroy() )
                    continue;
                vector<hashed_string> listChanged;
                changedCount += remapComponent( *pComponent, mapOldToNew, listChanged );
                for ( const hashed_string& propertyName : listChanged )
                    pComponent->onPropertyChanged( propertyName ); // 자원을 새 id 로 다시 잡는다(인스펙터 편집과 같은 알림)
            }
        } );
        return changedCount;
    }
} // namespace sw::editor
