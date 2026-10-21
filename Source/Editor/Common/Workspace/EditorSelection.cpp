#include "pch.h"

#include "Editor/Common/Workspace/EditorSelection.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/ObjectUndoUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorSelection" );

    void EditorSelection::selectObject( GameObject* pObj, SelectionMode mode )
    {
        pruneInvalid();

        const GameObjectHandle handle = ( pObj != nullptr ) ? pObj->getHandle() : GameObjectHandle{};
        switch ( mode )
        {
            case SelectionMode::Replace:
            {
                if ( _listSelectedObject.size() == 1 && _listSelectedObject.front() == handle )
                    return;
                _listSelectedObject.clear();
                if ( handle.isValid() )
                    _listSelectedObject.push_back( handle );
                break;
            }
            case SelectionMode::Add:
            {
                if ( handle.isValid() && hasObject( pObj ) == false )
                    _listSelectedObject.push_back( handle );
                break;
            }
            case SelectionMode::Remove:
            {
                VectorUtil::removeFirst( _listSelectedObject, handle );
                break;
            }
            case SelectionMode::Toggle:
            {
                VectorUtil::removeFirst( _listSelectedObject, handle );
                else if ( handle.isValid() )
                    _listSelectedObject.push_back( handle );
                break;
            }
        }

        notifyChanged();
    }

    void EditorSelection::selectObjects( const vector<GameObject*>& listObj, SelectionMode mode )
    {
        pruneInvalid();

        if ( mode == SelectionMode::Replace )
            _listSelectedObject.clear();

        for ( GameObject* pObj : listObj )
        {
            if ( pObj == nullptr )
                continue;

            const GameObjectHandle handle = pObj->getHandle();
            if ( mode == SelectionMode::Remove )
            {
                VectorUtil::removeFirst( _listSelectedObject, handle );
            }
            else if ( mode == SelectionMode::Toggle )
            {
                VectorUtil::removeFirst( _listSelectedObject, handle );
                else _listSelectedObject.push_back( handle );
            }
            else if ( hasObject( pObj ) == false )
            {
                _listSelectedObject.push_back( handle );
            }
        }

        notifyChanged();
    }

    bool EditorSelection::hasObject( const GameObject* pObj ) const
    {
        if ( pObj == nullptr )
            return false;
        return VectorUtil::contains( _listSelectedObject, pObj->getHandle() );
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

    void EditorSelection::selectAsset( string_view assetPath, SelectionMode mode )
    {
        switch ( mode )
        {
            case SelectionMode::Replace:
            {
                if ( _listSelectedAsset.size() == 1 && _listSelectedAsset.front() == assetPath )
                    return;
                _listSelectedAsset.clear();
                if ( assetPath.empty() == false )
                    _listSelectedAsset.emplace_back( assetPath );
                break;
            }
            case SelectionMode::Add:
            {
                if ( assetPath.empty() == false && hasAsset( assetPath ) == false )
                    _listSelectedAsset.emplace_back( assetPath );
                break;
            }
            case SelectionMode::Remove:
            {
                VectorUtil::removeFirst( _listSelectedAsset, assetPath );
                break;
            }
            case SelectionMode::Toggle:
            {
                VectorUtil::removeFirst( _listSelectedAsset, assetPath );
                else if ( assetPath.empty() == false )
                    _listSelectedAsset.emplace_back( assetPath );
                break;
            }
        }

        notifyChanged();
    }

    void EditorSelection::selectAssets( const vector<string>& listAssetPath, SelectionMode mode )
    {
        if ( mode == SelectionMode::Replace )
            _listSelectedAsset.clear();

        for ( const string& path : listAssetPath )
        {
            if ( path.empty() )
                continue;

            if ( mode == SelectionMode::Remove )
            {
                VectorUtil::removeFirst( _listSelectedAsset, path );
            }
            else if ( mode == SelectionMode::Toggle )
            {
                VectorUtil::removeFirst( _listSelectedAsset, path );
                else _listSelectedAsset.push_back( path );
            }
            else if ( hasAsset( path ) == false )
            {
                _listSelectedAsset.push_back( path );
            }
        }

        notifyChanged();
    }

    bool EditorSelection::hasAsset( string_view assetPath ) const
    {
        return VectorUtil::contains( _listSelectedAsset, assetPath );
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
            notifyChanged();
        }
    }

    void EditorSelection::clearAssetSelection()
    {
        if ( _listSelectedAsset.empty() == false )
        {
            _listSelectedAsset.clear();
            notifyChanged();
        }
    }

    void EditorSelection::clearAll()
    {
        const bool bHadObjects = _listSelectedObject.empty() == false;
        const bool bHadAssets  = _listSelectedAsset.empty() == false;

        _listSelectedObject.clear();
        _listSelectedAsset.clear();

        if ( bHadObjects || bHadAssets )
            notifyChanged();
    }

    void EditorSelection::pruneInvalid()
    {
        const size_t removeCount = VectorUtil::removeIf( _listSelectedObject, [this]( GameObjectHandle handle )
        { return findObject( handle ) == nullptr; } );
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
        if ( _pPreviewScene == nullptr )
        {
            EditorTransaction::recordModify( &obj, before, after, label );
            return;
        }
        // 창의 스택에 남긴다 — 명령은 기록할 때의 씬 키를 기억해 활성 씬이 바뀌어도 그 미리보기 씬에서 되돌린다(`ObjectUndoUtil`).
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
        {
            SW_LOG_ERROR( "No SceneManager - the edit of '%#' in the preview scene is not recorded", obj.getName().c_str() );
            return;
        }
        _pPreviewStack->push( ObjectUndoUtil::makeModify( *_pPreviewStack, *pSceneManager, obj, before, after, label ) );
    }

    void EditorSelection::notifyChanged()
    {
        // 바뀐 선택이 겨누는 선택이 된다 — 미리보기 창에서 고르면 그 창의 것, 메인에서 고르면 메인으로 돌아온다.
        editor::setFocusedSelection( isPreviewScope() ? this : nullptr );
        if ( _onSelectionChanged.isBound() )
            _onSelectionChanged();
    }
} // namespace sw::editor
