#include "pch.h"

#include "Editor/Common/Workspace/SelectionManager.h"

#include "Core/Common/StdHeaders.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"

namespace sw::editor
{
    void SelectionManager::selectObject( GameObject* pObj, SelectionMode mode )
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
                auto it = std::find( _listSelectedObject.begin(), _listSelectedObject.end(), handle );
                if ( it != _listSelectedObject.end() )
                    _listSelectedObject.erase( it );
                break;
            }
            case SelectionMode::Toggle:
            {
                auto it = std::find( _listSelectedObject.begin(), _listSelectedObject.end(), handle );
                if ( it != _listSelectedObject.end() )
                    _listSelectedObject.erase( it );
                else if ( handle.isValid() )
                    _listSelectedObject.push_back( handle );
                break;
            }
            default:
                break;
        }

        notifyChanged();
    }

    void SelectionManager::selectObjects( const vector<GameObject*>& listObj, SelectionMode mode )
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
                auto it = std::find( _listSelectedObject.begin(), _listSelectedObject.end(), handle );
                if ( it != _listSelectedObject.end() )
                    _listSelectedObject.erase( it );
            }
            else if ( mode == SelectionMode::Toggle )
            {
                auto it = std::find( _listSelectedObject.begin(), _listSelectedObject.end(), handle );
                if ( it != _listSelectedObject.end() )
                    _listSelectedObject.erase( it );
                else
                    _listSelectedObject.push_back( handle );
            }
            else if ( hasObject( pObj ) == false )
            {
                _listSelectedObject.push_back( handle );
            }
        }

        notifyChanged();
    }

    bool SelectionManager::hasObject( const GameObject* pObj ) const
    {
        if ( pObj == nullptr )
            return false;
        return std::find( _listSelectedObject.begin(), _listSelectedObject.end(), pObj->getHandle() ) != _listSelectedObject.end();
    }

    GameObject* SelectionManager::getPrimaryObject() const
    {
        if ( _listSelectedObject.empty() )
            return nullptr;
        return editor::findGameObject( _listSelectedObject.front() );
    }

    uint64 SelectionManager::getPrimaryObjectId() const
    {
        const GameObject* pPrimary = getPrimaryObject();
        return pPrimary != nullptr ? pPrimary->getObjectId() : 0;
    }

    void SelectionManager::getSelectedObjects( vector<GameObject*>& outListObject ) const
    {
        outListObject.clear();
        outListObject.reserve( _listSelectedObject.size() );
        for ( const GameObjectHandle handle : _listSelectedObject )
        {
            GameObject* pObj = editor::findGameObject( handle );
            if ( pObj != nullptr )
                outListObject.push_back( pObj );
        }
    }

    void SelectionManager::selectAsset( string_view assetPath, SelectionMode mode )
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
                auto it = std::find( _listSelectedAsset.begin(), _listSelectedAsset.end(), assetPath );
                if ( it != _listSelectedAsset.end() )
                    _listSelectedAsset.erase( it );
                break;
            }
            case SelectionMode::Toggle:
            {
                auto it = std::find( _listSelectedAsset.begin(), _listSelectedAsset.end(), assetPath );
                if ( it != _listSelectedAsset.end() )
                    _listSelectedAsset.erase( it );
                else if ( assetPath.empty() == false )
                    _listSelectedAsset.emplace_back( assetPath );
                break;
            }
            default:
                break;
        }

        notifyChanged();
    }

    void SelectionManager::selectAssets( const vector<string>& listAssetPath, SelectionMode mode )
    {
        if ( mode == SelectionMode::Replace )
            _listSelectedAsset.clear();

        for ( const string& path : listAssetPath )
        {
            if ( path.empty() )
                continue;

            if ( mode == SelectionMode::Remove )
            {
                auto it = std::find( _listSelectedAsset.begin(), _listSelectedAsset.end(), path );
                if ( it != _listSelectedAsset.end() )
                    _listSelectedAsset.erase( it );
            }
            else if ( mode == SelectionMode::Toggle )
            {
                auto it = std::find( _listSelectedAsset.begin(), _listSelectedAsset.end(), path );
                if ( it != _listSelectedAsset.end() )
                    _listSelectedAsset.erase( it );
                else
                    _listSelectedAsset.push_back( path );
            }
            else if ( hasAsset( path ) == false )
            {
                _listSelectedAsset.push_back( path );
            }
        }

        notifyChanged();
    }

    bool SelectionManager::hasAsset( string_view assetPath ) const
    {
        return std::find( _listSelectedAsset.begin(), _listSelectedAsset.end(), assetPath ) !=
               _listSelectedAsset.end();
    }

    string_view SelectionManager::getPrimaryAsset() const
    {
        if ( _listSelectedAsset.empty() )
            return {};
        return _listSelectedAsset.front();
    }

    void SelectionManager::clearObjectSelection()
    {
        if ( _listSelectedObject.empty() == false )
        {
            _listSelectedObject.clear();
            notifyChanged();
        }
    }

    void SelectionManager::clearAssetSelection()
    {
        if ( _listSelectedAsset.empty() == false )
        {
            _listSelectedAsset.clear();
            notifyChanged();
        }
    }

    void SelectionManager::clearAll()
    {
        const bool bHadObjects = _listSelectedObject.empty() == false;
        const bool bHadAssets  = _listSelectedAsset.empty() == false;

        _listSelectedObject.clear();
        _listSelectedAsset.clear();

        if ( bHadObjects || bHadAssets )
            notifyChanged();
    }

    void SelectionManager::pruneInvalid()
    {
        const size_t countBefore = _listSelectedObject.size();
        _listSelectedObject.erase(
            std::remove_if( _listSelectedObject.begin(), _listSelectedObject.end(),
                            []( GameObjectHandle handle )
        { return editor::findGameObject( handle ) == nullptr; } ),
            _listSelectedObject.end() );

        if ( _listSelectedObject.size() != countBefore )
            notifyChanged();
    }

    void SelectionManager::notifyChanged()
    {
        if ( _onSelectionChanged.isBound() )
            _onSelectionChanged();
    }
} // namespace sw::editor
