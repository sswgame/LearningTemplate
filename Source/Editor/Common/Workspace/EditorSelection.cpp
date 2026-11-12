#include "pch.h"

#include "Editor/Common/Workspace/EditorSelection.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelectionSync.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/ObjectSnapshot.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    void EditorSelection::selectObject( GameObject* pObj, SelectionMode mode )
    {
        if ( mode == SelectionMode::Replace )
        {
            pruneInvalid();
            if ( pObj != nullptr && _listSelectedObject.size() == 1 && _listSelectedObject.front() == pObj->getHandle() )
                return;
        }
        selectObjects( vector<GameObject*>{ pObj }, mode );
    }

    void EditorSelection::selectObjects( const vector<GameObject*>& listObj, SelectionMode mode )
    {
        // 빼기만 하는 고르기는 사라진 핸들과 상관없으므로 선택 전체를 풀지 않는다 — 되돌리기 · 삭제가 오브젝트마다 빼기를 부른다.
        if ( mode != SelectionMode::Remove )
            pruneInvalid();

        if ( mode == SelectionMode::Replace )
        {
            _listSelectedObject.clear();
            _uniqueSelectedObject.clear();
            _listSelectedObject.reserve( listObj.size() );
            _uniqueSelectedObject.reserve( listObj.size() );
        }

        // 뺀 핸들은 색인에서 바로 지우고 목록은 끝에 한 번만 접는다(빼기마다 목록을 훑으면 제곱이다). 같은 호출에서 다시 넣으면 목록의 원래 칸을 살린다.
        unordered_set<GameObjectHandle> uniqueRemoved;
        for ( const GameObject* pObj : listObj )
        {
            if ( pObj == nullptr )
                continue;
            const GameObjectHandle handle = pObj->getHandle();
            if ( handle.isValid() == false )
                continue;

            const bool bSelected = _uniqueSelectedObject.contains( handle );
            const bool bRemove   = mode == SelectionMode::Remove || ( mode == SelectionMode::Toggle && bSelected );
            if ( bRemove )
            {
                if ( bSelected )
                {
                    _uniqueSelectedObject.erase( handle );
                    uniqueRemoved.insert( handle );
                }
            }
            else if ( bSelected == false )
            {
                _uniqueSelectedObject.insert( handle );
                if ( uniqueRemoved.erase( handle ) == 0 )
                    _listSelectedObject.push_back( handle );
            }
        }
        if ( uniqueRemoved.empty() == false )
        {
            VectorUtil::removeIf( _listSelectedObject, [&uniqueRemoved]( GameObjectHandle handle )
            { return uniqueRemoved.contains( handle ); } );
        }

        notifyChanged();
    }

    bool EditorSelection::hasObject( const GameObject* pObj ) const
    {
        if ( pObj == nullptr )
            return false;
        return _uniqueSelectedObject.contains( pObj->getHandle() );
    }

    GameObject* EditorSelection::getPrimaryObject() const
    {
        if ( _listSelectedObject.empty() )
            return nullptr;
        return findObject( _listSelectedObject.front() );
    }

    uint64 EditorSelection::getPrimaryObjectID() const
    {
        const GameObject* pPrimary = getPrimaryObject();
        return pPrimary != nullptr ? pPrimary->getObjectID() : 0;
    }

    void EditorSelection::getSelectedObjects( vector<GameObject*>& outListObject ) const
    {
        outListObject.clear();
        outListObject.reserve( _listSelectedObject.size() );
        for ( const GameObjectHandle handle : _listSelectedObject )
        {
            GameObject* pObj = findObject( handle );
            if ( pObj != nullptr )
                outListObject.push_back( pObj );
        }
    }

    void EditorSelection::getTopLevelObjects( vector<GameObject*>& outListObject ) const
    {
        vector<GameObject*> listSelected;
        getSelectedObjects( listSelected );
        outListObject = filterTopLevel( listSelected );
    }

    vector<GameObject*> EditorSelection::filterTopLevel( const vector<GameObject*>& listObject )
    {
        // 조상 확인을 해시 집합으로 한다. 오브젝트 × 깊이이며, 목록 전체를 조상마다 훑지 않는다(8000 개 선택이면 그쪽이 제곱이다).
        unordered_set<const GameObject*> uniquePicked;
        uniquePicked.reserve( listObject.size() );
        for ( const GameObject* pObj : listObject )
        {
            if ( pObj != nullptr )
                uniquePicked.insert( pObj );
        }
        vector<GameObject*> listRoot;
        listRoot.reserve( listObject.size() );
        for ( GameObject* pObj : listObject )
        {
            if ( pObj == nullptr )
                continue;
            bool bAncestorPicked = false;
            for ( const GameObject* pParent = pObj->getParent(); pParent != nullptr && bAncestorPicked == false; pParent = pParent->getParent() )
            {
                bAncestorPicked = uniquePicked.contains( pParent );
            }
            if ( bAncestorPicked == false )
                listRoot.push_back( pObj );
        }
        return listRoot;
    }

    void EditorSelection::selectAsset( string_view assetPath, SelectionMode mode )
    {
        if ( mode == SelectionMode::Replace && _listSelectedAsset.size() == 1 && _listSelectedAsset.front() == assetPath )
            return;
        selectAssets( vector<string>{ string{ assetPath } }, mode );
    }

    void EditorSelection::selectAssets( const vector<string>& listAssetPath, SelectionMode mode )
    {
        if ( mode == SelectionMode::Replace )
        {
            _listSelectedAsset.clear();
            _uniqueSelectedAsset.clear();
            _listSelectedAsset.reserve( listAssetPath.size() );
            _uniqueSelectedAsset.reserve( listAssetPath.size() );
        }

        // `selectObjects` 와 같은 접기 — 뺀 경로는 색인에서 바로 지우고 목록은 끝에 한 번만 접는다. 같은 호출에서 다시 넣으면 원래 칸을 살린다.
        unordered_set<string> uniqueRemoved;
        for ( const string& path : listAssetPath )
        {
            if ( path.empty() )
                continue;

            const bool bSelected = _uniqueSelectedAsset.contains( path );
            const bool bRemove   = mode == SelectionMode::Remove || ( mode == SelectionMode::Toggle && bSelected );
            if ( bRemove )
            {
                if ( bSelected )
                {
                    _uniqueSelectedAsset.erase( path );
                    uniqueRemoved.insert( path );
                }
            }
            else if ( bSelected == false )
            {
                _uniqueSelectedAsset.insert( path );
                if ( uniqueRemoved.erase( path ) == 0 )
                    _listSelectedAsset.push_back( path );
            }
        }
        if ( uniqueRemoved.empty() == false )
        {
            VectorUtil::removeIf( _listSelectedAsset, [&uniqueRemoved]( const string& path )
            { return uniqueRemoved.contains( path ); } );
        }

        notifyChanged();
    }

    void EditorSelection::remapAssets( const unordered_map<string, string>& mapOldToNew )
    {
        if ( mapOldToNew.empty() || _listSelectedAsset.empty() )
            return;

        // 목록을 새 id 로 다시 쓰고 색인을 새로 만든다. 칸마다 색인을 고치면 A→B · B→C 처럼 이어 옮길 때 아직 안 바꾼 옛 B 와 겹쳐 보인다.
        bool                  bChanged = false;
        vector<string>        listRemapped;
        unordered_set<string> uniqueRemapped;
        listRemapped.reserve( _listSelectedAsset.size() );
        uniqueRemapped.reserve( _listSelectedAsset.size() );
        for ( string& selectedAsset : _listSelectedAsset )
        {
            const auto it = mapOldToNew.find( selectedAsset );
            if ( it != mapOldToNew.end() )
            {
                selectedAsset = it->second;
                bChanged      = true;
            }
            if ( uniqueRemapped.insert( selectedAsset ).second )
                listRemapped.push_back( std::move( selectedAsset ) );
        }
        // 위 반복이 칸을 옮겨 냈으므로 바뀐 것이 없어도 다시 담는다(그때는 원래와 같은 목록이다).
        _listSelectedAsset   = std::move( listRemapped );
        _uniqueSelectedAsset = std::move( uniqueRemapped );
        if ( bChanged )
            notifyChanged();
    }

    bool EditorSelection::hasAsset( string_view assetPath ) const
    {
        return _uniqueSelectedAsset.contains( assetPath );
    }

    string_view EditorSelection::getPrimaryAsset() const
    {
        if ( _listSelectedAsset.empty() )
            return {};
        return _listSelectedAsset.front();
    }

    void EditorSelection::clearObjectSelection()
    {
        if ( _listSelectedObject.empty() == false )
        {
            _listSelectedObject.clear();
            _uniqueSelectedObject.clear();
            notifyChanged();
        }
    }

    void EditorSelection::clearAssetSelection()
    {
        if ( _listSelectedAsset.empty() == false )
        {
            _listSelectedAsset.clear();
            _uniqueSelectedAsset.clear();
            notifyChanged();
        }
    }

    void EditorSelection::clearAll()
    {
        const bool bHadObjects = _listSelectedObject.empty() == false;
        const bool bHadAssets  = _listSelectedAsset.empty() == false;

        _listSelectedObject.clear();
        _uniqueSelectedObject.clear();
        _listSelectedAsset.clear();
        _uniqueSelectedAsset.clear();

        if ( bHadObjects || bHadAssets )
            notifyChanged();
    }

    void EditorSelection::pruneInvalid()
    {
        const size_t removeCount = VectorUtil::removeIf( _listSelectedObject, [this]( GameObjectHandle handle )
        {
            if ( findObject( handle ) != nullptr )
                return false;
            _uniqueSelectedObject.erase( handle );
            return true;
        } );
        if ( removeCount > 0 )
            notifyChanged();
    }

    EditorSelection::~EditorSelection()
    {
        if ( isPreviewScope() )
            unbindPreviewScope();
    }

    void EditorSelection::bindPreviewScope( Scene& scene, CommandStack& stack )
    {
        _listSelectedObject.clear();
        _uniqueSelectedObject.clear();
        _pPreviewScene = &scene;
        _pPreviewStack = &stack;
        editor::registerPreviewSelection( *this );
    }

    void EditorSelection::unbindPreviewScope()
    {
        if ( editor::getFocusedSelection() == this )
            editor::setFocusedSelection( nullptr );
        editor::unregisterPreviewSelection( *this );
        _listSelectedObject.clear();
        _uniqueSelectedObject.clear();
        _pPreviewScene = nullptr;
        _pPreviewStack = nullptr;
    }

    Scene* EditorSelection::getScene() const
    {
        return _pPreviewScene != nullptr ? _pPreviewScene : editor::getActiveScene();
    }

    GameObject* EditorSelection::findObject( GameObjectHandle handle ) const
    {
        if ( _pPreviewScene == nullptr )
            return editor::findGameObject( handle );
        const GameObjectManager* pManager = _pPreviewScene->getObjectManager();
        return ( pManager != nullptr && handle.isValid() ) ? pManager->resolveGameObject( handle ) : nullptr;
    }

    void EditorSelection::recordModify( GameObject& obj, const ObjectSnapshot& before, const ObjectSnapshot& after, string_view label ) const
    {
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        if ( _pPreviewScene == nullptr )
        {
            transaction.recordModify( &obj, before, after, label );
            return;
        }
        // 창의 스택에 남긴다 — 명령은 기록할 때의 씬 키를 기억해 활성 씬이 바뀌어도 그 미리보기 씬에서 되돌린다(`ObjectSnapshot`).
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
        {
            SW_LOG_ERROR_ID( "EditorSelection.NoSceneManagerEditPreview", "No SceneManager - the edit of '%#' in the preview scene is not recorded", obj.getName().c_str() );
            return;
        }
        _pPreviewStack->push( ObjectSnapshot::makeModify( *pSceneManager, obj, before, after, label ) );
    }

    void EditorSelection::notifyChanged()
    {
        // 바뀐 선택이 겨누는 선택이 된다 — 미리보기 창에서 고르면 그 창의 것, 메인에서 고르면 메인으로 돌아온다.
        editor::setFocusedSelection( isPreviewScope() ? this : nullptr );
        // 동기화 채널에 자기 문맥으로 알린다 — 선택은 옮기지 않고, 받는 쪽이 따를 문맥을 고른다(`EditorSelectionSync`).
        EditorSelectionSync* pSync = editor::findSubsystem<EditorSelectionSync>();
        if ( pSync != nullptr )
            pSync->onSelectionChanged( *this );
        if ( _onSelectionChanged.isBound() )
            _onSelectionChanged();
    }
} // namespace sw::editor
