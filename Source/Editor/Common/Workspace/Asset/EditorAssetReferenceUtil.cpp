#include "pch.h"

#include "Editor/Common/Workspace/Asset/EditorAssetReferenceUtil.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Editor/Common/EditorSceneIterationAccess.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentAssetPathUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    uint32 EditorAssetReferenceUtil::remapComponent( Component& component, const unordered_map<string, string>& mapOldToNew, vector<hashed_string>& outListChanged )
    {
        if ( mapOldToNew.empty() )
            return 0;
        uint32 changedCount{ 0 };
        // 순회는 핫 리로드 · 프리로드와 같은 도우미 하나다 — 상속 칸 · 시퀀스 원소 · 중첩 구조체 안의 경로까지 내려간다.
        ComponentAssetPathUtil::forEachAssetPath( component, SW_DELEGATE_LAMBDA( AssetPathVisitor, [&mapOldToNew, &outListChanged, &changedCount]( const AssetPathValue& value )
        {
            const string_view path = value.getPath();
            const auto        it   = path.empty() ? mapOldToNew.end() : mapOldToNew.find( string{ path } );
            if ( it == mapOldToNew.end() || value.assign( it->second ) == false )
                return;
            ++changedCount;
            if ( VectorUtil::contains( outListChanged, value._pTopProperty->_name ) == false )
                outListChanged.push_back( value._pTopProperty->_name ); // 알림은 맨 위 칸 하나에 한 번
        } ) );
        return changedCount;
    }

    uint32 EditorAssetReferenceUtil::remapScene( Scene& scene, const unordered_map<string, string>& mapOldToNew )
    {
        GameObjectManager* const pObjects = scene.getObjectManager();
        if ( pObjects == nullptr || mapOldToNew.empty() )
            return 0;
        PrefabInstanceRegistry& prefabs      = pObjects->getSubsystem<PrefabInstanceRegistry>();
        uint32                  changedCount = scene.remapAssetPaths( mapOldToNew ); // 조립 기록(부모 씬 · 인스턴스 씬 · 엔티티 프리팹) — 다음 저장이 새 id 를 쓴다
        pObjects->forEachGameObject( editor::EditorSceneIterationAccess::getKey(), [&mapOldToNew, &prefabs, &changedCount]( GameObject* pObject )
        {
            const uint64                  objectID = pObject->getObjectID();
            const PrefabNestedPart* const pPart    = prefabs.findNestedPart( objectID );
            const auto                    itPart   = pPart != nullptr ? mapOldToNew.find( pPart->_prefabPath ) : mapOldToNew.end();
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
