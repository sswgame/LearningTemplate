#include "pch.h"

#include "Editor/Common/Workspace/SelectionManager.h"

#include "Core/Common/StdHeaders.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"

namespace sw::editor
{
	void SelectionManager::selectObject( GameObjectPtr pObj, SelectionMode mode )
	{
		pruneInvalid();

		switch ( mode )
		{
			case SelectionMode::Replace:
			{
				_listSelectedObject.clear();
				if ( pObj.isValid() )
					_listSelectedObject.push_back( pObj );
				break;
			}
			case SelectionMode::Add:
			{
				if ( pObj.isValid() && hasObject( pObj ) == false )
					_listSelectedObject.push_back( pObj );
				break;
			}
			case SelectionMode::Remove:
			{
				auto it = std::find( _listSelectedObject.begin(), _listSelectedObject.end(), pObj );
				if ( it != _listSelectedObject.end() )
					_listSelectedObject.erase( it );
				break;
			}
			case SelectionMode::Toggle:
			{
				auto it = std::find( _listSelectedObject.begin(), _listSelectedObject.end(), pObj );
				if ( it != _listSelectedObject.end() )
					_listSelectedObject.erase( it );
				else if ( pObj.isValid() )
					_listSelectedObject.push_back( pObj );
				break;
			}
			default:
				break;
		}

		notifyChanged();
	}

	void SelectionManager::selectObjects( const vector<GameObjectPtr>& listObjs, SelectionMode mode )
	{
		pruneInvalid();

		if ( mode == SelectionMode::Replace )
			_listSelectedObject.clear();

		for ( const GameObjectPtr& pObj : listObjs )
		{
			if ( pObj.isValid() == false )
				continue;

			if ( mode == SelectionMode::Remove )
			{
				auto it = std::find( _listSelectedObject.begin(), _listSelectedObject.end(), pObj );
				if ( it != _listSelectedObject.end() )
					_listSelectedObject.erase( it );
			}
			else if ( hasObject( pObj ) == false )
			{
				_listSelectedObject.push_back( pObj );
			}
		}

		notifyChanged();
	}

	bool SelectionManager::hasObject( const GameObjectPtr& pObj ) const
	{
		return std::find( _listSelectedObject.begin(), _listSelectedObject.end(), pObj ) !=
			   _listSelectedObject.end();
	}

	GameObjectPtr SelectionManager::getPrimaryObject() const
	{
		if ( _listSelectedObject.empty() )
			return GameObjectPtr{};
		return _listSelectedObject.front();
	}

	uint64 SelectionManager::getPrimaryObjectId() const
	{
		if ( _listSelectedObject.empty() )
			return 0;
		GameObject* pRaw = _listSelectedObject.front().get();
		return ( pRaw != nullptr ) ? pRaw->getObjectId() : 0;
	}

	void SelectionManager::selectAsset( string_view assetPath, SelectionMode mode )
	{
		switch ( mode )
		{
			case SelectionMode::Replace:
			{
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

	void SelectionManager::selectAssets( const vector<string>& listAssetPaths, SelectionMode mode )
	{
		if ( mode == SelectionMode::Replace )
			_listSelectedAsset.clear();

		for ( const string& path : listAssetPaths )
		{
			if ( path.empty() )
				continue;

			if ( mode == SelectionMode::Remove )
			{
				auto it = std::find( _listSelectedAsset.begin(), _listSelectedAsset.end(), path );
				if ( it != _listSelectedAsset.end() )
					_listSelectedAsset.erase( it );
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
							[]( const GameObjectPtr& pObj )
		{ return pObj.isValid() == false; } ),
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
