#include "pch.h"

#include "Editor/Popups/EditorPopupManager.h"

#include "Editor/Popups/BoneHierarchyPopup.h"
#include "Editor/Popups/CommandPalettePopup.h"

namespace sw::editor
{
	void EditorPopupManager::registerPopup( unique_ptr<IEditorPopup> pPopup )
	{
		if ( pPopup == nullptr )
			return;

		const string popupId = string{ pPopup->getPopupId() };

		// 이미 동일 ID가 등록되어 있으면 교체
		for ( EditorPopupEntry& entry : _listPopups )
		{
			if ( entry._id == popupId )
			{
				entry._pInstance = std::move( pPopup );
				return;
			}
		}

		EditorPopupEntry entry;
		entry._id		 = popupId;
		entry._pInstance = std::move( pPopup );
		_listPopups.push_back( std::move( entry ) );
	}

	IEditorPopup* EditorPopupManager::findPopup( string_view id )
	{
		if ( _bDefaultsRegistered == false )
			registerDefaultPopups();

		for ( EditorPopupEntry& entry : _listPopups )
		{
			if ( entry._id == id && entry._pInstance != nullptr )
				return entry._pInstance.get();
		}
		return nullptr;
	}

	void EditorPopupManager::openPopup( string_view id )
	{
		IEditorPopup* pPopup = findPopup( id );
		if ( pPopup != nullptr )
			pPopup->open();
	}

	void EditorPopupManager::closePopup( string_view id )
	{
		IEditorPopup* pPopup = findPopup( id );
		if ( pPopup != nullptr )
			pPopup->close();
	}

	void EditorPopupManager::togglePopup( string_view id )
	{
		IEditorPopup* pPopup = findPopup( id );
		if ( pPopup != nullptr )
			pPopup->toggle();
	}

	bool EditorPopupManager::isPopupOpen( string_view id ) const
	{
		for ( const EditorPopupEntry& entry : _listPopups )
		{
			if ( entry._id == id && entry._pInstance != nullptr )
				return entry._pInstance->isOpen();
		}
		return false;
	}

	void EditorPopupManager::drawOpenPopups()
	{
		if ( _bDefaultsRegistered == false )
			registerDefaultPopups();

		for ( EditorPopupEntry& entry : _listPopups )
		{
			if ( entry._pInstance != nullptr && entry._pInstance->isOpen() )
			{
				entry._pInstance->draw();
			}
		}
	}

	void EditorPopupManager::registerDefaultPopups()
	{
		if ( _bDefaultsRegistered )
			return;
		_bDefaultsRegistered = true;

		registerPopup( make_unique<CommandPalettePopup>() );
		registerPopup( make_unique<BoneHierarchyPopup>() );
	}

	void EditorPopupManager::clear()
	{
		_listPopups.clear();
		_bDefaultsRegistered = false;
	}
} // namespace sw::editor
