#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorPrefabIsolation.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorPrefabIsolationInternal
        {
            static bool matchesPrefabPath( const EditorWorkspace& workspace, const GameObject* pObject, string_view prefabPath )
            {
                if ( pObject == nullptr || prefabPath.empty() )
                    return false;
                const string_view mapped = workspace.findPrefabPath( pObject );
                return mapped.empty() == false && FileUtil::pathsEqualNormalized( mapped, prefabPath );
            }

            /** @brief @p pScopeRoot 아래에서 @p pRoot 의 줄기(자손 · 조상)가 아닌 오브젝트를 숨깁니다. */
            static void hideObjectsOutsideIsolation( GameObjectManager& manager, const GameObject* pScopeRoot, const GameObject* pRoot,
                                                     vector<PrefabIsolationHiddenObject>& outHidden )
            {
                outHidden.clear();
                if ( pScopeRoot == nullptr )
                    return;

                const vector<GameObject*> listObject = manager.getAllGameObjects();
                for ( GameObject* pObject : listObject )
                {
                    if ( pObject == nullptr || pObject->isPendingDestroy() )
                        continue;
                    if ( pObject->isDescendantOf( pScopeRoot ) == false )
                        continue; // 문서 루트 밖(미리보기 환경)은 건드리지 않는다
                    if ( pObject->isDescendantOf( pRoot ) || pRoot->isDescendantOf( pObject ) )
                        continue;

                    PrefabIsolationHiddenObject entry{};
                    entry._objectID   = pObject->getObjectID();
                    entry._bWasActive = pObject->isActive() ? SW_TRUE : SW_FALSE;
                    outHidden.push_back( entry );
                    pObject->setActive( false );
                }
            }

            static void restoreIsolationHidden( GameObjectManager& manager, const vector<PrefabIsolationHiddenObject>& listHidden )
            {
                for ( const PrefabIsolationHiddenObject& entry : listHidden )
                {
                    GameObject* pObject = manager.findGameObjectByID( entry._objectID );
                    if ( pObject == nullptr )
                        continue;
                    pObject->setActive( entry._bWasActive == SW_TRUE );
                }
            }
        };
    } // namespace

    GameObject* EditorPrefabIsolation::findInstance( GameObjectManager& manager, const EditorWorkspace& workspace, string_view prefabPath, GameObject* pPreferred,
                                                     GameObject* pUnderRoot )
    {
        if ( pPreferred != nullptr && EditorPrefabIsolationInternal::matchesPrefabPath( workspace, pPreferred, prefabPath ) )
        {
            if ( pUnderRoot == nullptr || pPreferred->isDescendantOf( pUnderRoot ) )
                return pPreferred;
        }

        GameObject*               pFirst     = nullptr;
        const vector<GameObject*> listObject = manager.getAllGameObjects();
        for ( GameObject* pObject : listObject )
        {
            if ( pObject == nullptr || pObject->isPendingDestroy() )
                continue;
            if ( EditorPrefabIsolationInternal::matchesPrefabPath( workspace, pObject, prefabPath ) == false )
                continue;
            if ( pUnderRoot != nullptr && pObject->isDescendantOf( pUnderRoot ) == false )
                continue;
            if ( pUnderRoot != nullptr )
                return pObject;
            if ( pFirst == nullptr )
                pFirst = pObject;
        }
        return pFirst;
    }

    void EditorPrefabIsolation::push( GameObjectManager& manager, string_view prefabPath, GameObject& root, bool bSpawnedRoot )
    {
        PrefabIsolationFrame frame{};
        frame._prefabPath               = string{ prefabPath };
        frame._rootObjectID             = root.getObjectID();
        frame._bSpawnedRoot             = bSpawnedRoot ? SW_TRUE : SW_FALSE;
        const GameObject* pDocumentRoot = isActive() ? manager.findGameObjectByID( getDocumentRootObjectID() ) : nullptr;
        EditorPrefabIsolationInternal::hideObjectsOutsideIsolation( manager, pDocumentRoot, &root, frame._listHidden );
        _listFrame.push_back( std::move( frame ) );
    }

    bool EditorPrefabIsolation::pop( GameObjectManager* pManager, PrefabIsolationFrame& outFrame )
    {
        if ( _listFrame.empty() )
            return false;
        outFrame = std::move( _listFrame.back() );
        _listFrame.pop_back();
        if ( pManager != nullptr )
            EditorPrefabIsolationInternal::restoreIsolationHidden( *pManager, outFrame._listHidden );
        return true;
    }
} // namespace sw::editor
