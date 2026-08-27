#include "pch.h"

#include "Editor/Common/Workspace/SelectionManager.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"

#include <algorithm>

namespace sw::editor
{
	void SelectionManager::selectObject( GameObjectPtr pObj, SelectionMode mode )
	{
		pruneInvalid();

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
				if ( pObj.isValid() && hasObject( pObj ) == false )
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

	void SelectionManager::selectObjects( const vector<GameObjectPtr>& listObjs, SelectionMode mode )
	{
		pruneInvalid();

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
			else if ( hasObject( pObj ) == false )
			{
				_listSelectedObjects.push_back( pObj );
			}
		}

		notifyChanged();
	}

	bool SelectionManager::hasObject( const GameObjectPtr& pObj ) const
	{
		return std::find( _listSelectedObjects.begin(), _listSelectedObjects.end(), pObj ) !=
			   _listSelectedObjects.end();
	}

	GameObjectPtr SelectionManager::getPrimaryObject() const
	{
		if ( _listSelectedObjects.empty() )
			return GameObjectPtr{};
		return _listSelectedObjects.front();
	}

	uint64 SelectionManager::getPrimaryObjectId() const
	{
		if ( _listSelectedObjects.empty() )
			return 0;
		GameObject* pRaw = _listSelectedObjects.front().get();
		return ( pRaw != nullptr ) ? pRaw->getObjectId() : 0;
	}

	void SelectionManager::selectAsset( string_view assetPath, SelectionMode mode )
	{
		switch ( mode )
		{
			case SelectionMode::Replace:
			{
				_listSelectedAssets.clear();
				if ( assetPath.empty() == false )
					_listSelectedAssets.emplace_back( assetPath );
				break;
			}
			case SelectionMode::Add:
			{
				if ( assetPath.empty() == false && hasAsset( assetPath ) == false )
					_listSelectedAssets.emplace_back( assetPath );
				break;
			}
			case SelectionMode::Remove:
			{
				auto it = std::find( _listSelectedAssets.begin(), _listSelectedAssets.end(), assetPath );
				if ( it != _listSelectedAssets.end() )
					_listSelectedAssets.erase( it );
				break;
			}
			case SelectionMode::Toggle:
			{
				auto it = std::find( _listSelectedAssets.begin(), _listSelectedAssets.end(), assetPath );
				if ( it != _listSelectedAssets.end() )
					_listSelectedAssets.erase( it );
				else if ( assetPath.empty() == false )
					_listSelectedAssets.emplace_back( assetPath );
				break;
			}
		}

		notifyChanged();
	}

	void SelectionManager::selectAssets( const vector<string>& listAssetPaths, SelectionMode mode )
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
			else if ( hasAsset( path ) == false )
			{
				_listSelectedAssets.push_back( path );
			}
		}

		notifyChanged();
	}

	bool SelectionManager::hasAsset( string_view assetPath ) const
	{
		return std::find( _listSelectedAssets.begin(), _listSelectedAssets.end(), assetPath ) !=
			   _listSelectedAssets.end();
	}

	string_view SelectionManager::getPrimaryAsset() const
	{
		if ( _listSelectedAssets.empty() )
			return {};
		return _listSelectedAssets.front();
	}

	void SelectionManager::clearObjectSelection()
	{
		if ( _listSelectedObjects.empty() == false )
		{
			_listSelectedObjects.clear();
			notifyChanged();
		}
	}

	void SelectionManager::clearAssetSelection()
	{
		if ( _listSelectedAssets.empty() == false )
		{
			_listSelectedAssets.clear();
			notifyChanged();
		}
	}

	void SelectionManager::clearAll()
	{
		const bool bHadObjects = _listSelectedObjects.empty() == false;
		const bool bHadAssets  = _listSelectedAssets.empty() == false;

		_listSelectedObjects.clear();
		_listSelectedAssets.clear();

		if ( bHadObjects || bHadAssets )
			notifyChanged();
	}

	void SelectionManager::pruneInvalid()
	{
		const size_t countBefore = _listSelectedObjects.size();
		_listSelectedObjects.erase(
			std::remove_if( _listSelectedObjects.begin(), _listSelectedObjects.end(),
							[]( const GameObjectPtr& pObj )
		{ return pObj.isValid() == false; } ),
			_listSelectedObjects.end() );

		if ( _listSelectedObjects.size() != countBefore )
			notifyChanged();
	}

	void SelectionManager::notifyChanged()
	{
		if ( _onSelectionChanged.isBound() )
			_onSelectionChanged();
	}
} // namespace sw::editor
