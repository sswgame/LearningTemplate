#include "pch.h"

#include "Editor/Common/Workspace/SelectionManager.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"

#include <algorithm>

namespace sw::editor
{
	namespace
	{
		SelectionManager* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getSelectionManager();
			return nullptr;
		}

		const vector<GameObjectPtr> s_emptyObjects{};
		const vector<string>		s_emptyAssets{};
		Delegate<void()>			s_emptyDelegate{};
	} // namespace

	// ------------------------------------------------------------------------------
	// Static Methods
	// ------------------------------------------------------------------------------
	void SelectionManager::selectObject( GameObjectPtr pObj, SelectionMode mode )
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			pManager->selectObjectImpl( pObj, mode );
	}

	void SelectionManager::selectObjects( const vector<GameObjectPtr>& listObjs, SelectionMode mode )
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			pManager->selectObjectsImpl( listObjs, mode );
	}

	bool SelectionManager::hasObject( const GameObjectPtr& pObj )
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			return pManager->hasObjectImpl( pObj );
		return false;
	}

	GameObjectPtr SelectionManager::getPrimaryObject()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			return pManager->getPrimaryObjectImpl();
		return GameObjectPtr{};
	}

	uint64 SelectionManager::getPrimaryObjectId()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			return pManager->getPrimaryObjectIdImpl();
		return 0;
	}

	const vector<GameObjectPtr>& SelectionManager::getSelectedObjects()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			return pManager->getSelectedObjectsImpl();
		return s_emptyObjects;
	}

	size_t SelectionManager::getSelectedObjectCount()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			return pManager->getSelectedObjectCountImpl();
		return 0;
	}

	void SelectionManager::selectAsset( string_view assetPath, SelectionMode mode )
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			pManager->selectAssetImpl( assetPath, mode );
	}

	void SelectionManager::selectAssets( const vector<string>& listAssetPaths, SelectionMode mode )
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			pManager->selectAssetsImpl( listAssetPaths, mode );
	}

	bool SelectionManager::hasAsset( string_view assetPath )
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			return pManager->hasAssetImpl( assetPath );
		return false;
	}

	string_view SelectionManager::getPrimaryAsset()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			return pManager->getPrimaryAssetImpl();
		return {};
	}

	const vector<string>& SelectionManager::getSelectedAssets()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			return pManager->getSelectedAssetsImpl();
		return s_emptyAssets;
	}

	void SelectionManager::clearObjectSelection()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			pManager->clearObjectSelectionImpl();
	}

	void SelectionManager::clearAssetSelection()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			pManager->clearAssetSelectionImpl();
	}

	void SelectionManager::clearAll()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			pManager->clearAllImpl();
	}

	void SelectionManager::pruneInvalid()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			pManager->pruneInvalidImpl();
	}

	Delegate<void()>& SelectionManager::onSelectionChanged()
	{
		SelectionManager* pManager = getImpl();
		if ( pManager != nullptr )
			return pManager->onSelectionChangedImpl();
		return s_emptyDelegate;
	}

	// ------------------------------------------------------------------------------
	// Instance Implementations
	// ------------------------------------------------------------------------------
	void SelectionManager::selectObjectImpl( GameObjectPtr pObj, SelectionMode mode )
	{
		pruneInvalidImpl();

		switch ( mode )
		{
			case SelectionMode::Replace:
			{
				_listSelectedObjects.clear();
				if ( pObj.isValid() )
					_listSelectedObjects.push_back( pObj );
				break;
			}
			case SelectionMode::Add:
			{
				if ( pObj.isValid() && hasObjectImpl( pObj ) == false )
					_listSelectedObjects.push_back( pObj );
				break;
			}
			case SelectionMode::Remove:
			{
				auto it = std::find( _listSelectedObjects.begin(), _listSelectedObjects.end(), pObj );
				if ( it != _listSelectedObjects.end() )
					_listSelectedObjects.erase( it );
				break;
			}
			case SelectionMode::Toggle:
			{
				auto it = std::find( _listSelectedObjects.begin(), _listSelectedObjects.end(), pObj );
				if ( it != _listSelectedObjects.end() )
					_listSelectedObjects.erase( it );
				else if ( pObj.isValid() )
					_listSelectedObjects.push_back( pObj );
				break;
			}
		}

		notifyChanged();
	}

	void SelectionManager::selectObjectsImpl( const vector<GameObjectPtr>& listObjs, SelectionMode mode )
	{
		pruneInvalidImpl();

		if ( mode == SelectionMode::Replace )
			_listSelectedObjects.clear();

		for ( const GameObjectPtr& pObj : listObjs )
		{
			if ( pObj.isValid() == false )
				continue;

			if ( mode == SelectionMode::Remove )
			{
				auto it = std::find( _listSelectedObjects.begin(), _listSelectedObjects.end(), pObj );
				if ( it != _listSelectedObjects.end() )
					_listSelectedObjects.erase( it );
			}
			else if ( hasObjectImpl( pObj ) == false )
			{
				_listSelectedObjects.push_back( pObj );
			}
		}

		notifyChanged();
	}

	bool SelectionManager::hasObjectImpl( const GameObjectPtr& pObj ) const
	{
		return std::find( _listSelectedObjects.begin(), _listSelectedObjects.end(), pObj ) !=
			   _listSelectedObjects.end();
	}

	GameObjectPtr SelectionManager::getPrimaryObjectImpl() const
	{
		if ( _listSelectedObjects.empty() )
			return GameObjectPtr{};
		return _listSelectedObjects.front();
	}

	uint64 SelectionManager::getPrimaryObjectIdImpl() const
	{
		if ( _listSelectedObjects.empty() )
			return 0;
		GameObject* pRaw = _listSelectedObjects.front().get();
		return ( pRaw != nullptr ) ? pRaw->getObjectId() : 0;
	}

	void SelectionManager::selectAssetImpl( string_view assetPath, SelectionMode mode )
	{
		const string pathStr{ assetPath };

		switch ( mode )
		{
			case SelectionMode::Replace:
			{
				_listSelectedAssets.clear();
				if ( pathStr.empty() == false )
					_listSelectedAssets.push_back( pathStr );
				break;
			}
			case SelectionMode::Add:
			{
				if ( pathStr.empty() == false && hasAssetImpl( assetPath ) == false )
					_listSelectedAssets.push_back( pathStr );
				break;
			}
			case SelectionMode::Remove:
			{
				auto it = std::find( _listSelectedAssets.begin(), _listSelectedAssets.end(), pathStr );
				if ( it != _listSelectedAssets.end() )
					_listSelectedAssets.erase( it );
				break;
			}
			case SelectionMode::Toggle:
			{
				auto it = std::find( _listSelectedAssets.begin(), _listSelectedAssets.end(), pathStr );
				if ( it != _listSelectedAssets.end() )
					_listSelectedAssets.erase( it );
				else if ( pathStr.empty() == false )
					_listSelectedAssets.push_back( pathStr );
				break;
			}
		}

		notifyChanged();
	}

	void SelectionManager::selectAssetsImpl( const vector<string>& listAssetPaths, SelectionMode mode )
	{
		if ( mode == SelectionMode::Replace )
			_listSelectedAssets.clear();

		for ( const string& path : listAssetPaths )
		{
			if ( path.empty() )
				continue;

			if ( mode == SelectionMode::Remove )
			{
				auto it = std::find( _listSelectedAssets.begin(), _listSelectedAssets.end(), path );
				if ( it != _listSelectedAssets.end() )
					_listSelectedAssets.erase( it );
			}
			else if ( hasAssetImpl( path ) == false )
			{
				_listSelectedAssets.push_back( path );
			}
		}

		notifyChanged();
	}

	bool SelectionManager::hasAssetImpl( string_view assetPath ) const
	{
		return std::find( _listSelectedAssets.begin(), _listSelectedAssets.end(), string{ assetPath } ) !=
			   _listSelectedAssets.end();
	}

	string_view SelectionManager::getPrimaryAssetImpl() const
	{
		if ( _listSelectedAssets.empty() )
			return {};
		return _listSelectedAssets.front();
	}

	void SelectionManager::clearObjectSelectionImpl()
	{
		if ( _listSelectedObjects.empty() == false )
		{
			_listSelectedObjects.clear();
			notifyChanged();
		}
	}

	void SelectionManager::clearAssetSelectionImpl()
	{
		if ( _listSelectedAssets.empty() == false )
		{
			_listSelectedAssets.clear();
			notifyChanged();
		}
	}

	void SelectionManager::clearAllImpl()
	{
		const bool bHadAny = ( _listSelectedObjects.empty() == false ) || ( _listSelectedAssets.empty() == false );
		_listSelectedObjects.clear();
		_listSelectedAssets.clear();
		if ( bHadAny )
			notifyChanged();
	}

	void SelectionManager::pruneInvalidImpl()
	{
		_listSelectedObjects.erase(
			std::remove_if( _listSelectedObjects.begin(), _listSelectedObjects.end(),
							[]( const GameObjectPtr& pObj )
		{ return pObj.isValid() == false; } ),
			_listSelectedObjects.end() );
	}

	void SelectionManager::notifyChanged()
	{
		if ( _onSelectionChanged.isBound() )
			_onSelectionChanged();
	}
} // namespace sw::editor
