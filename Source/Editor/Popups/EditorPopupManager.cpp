#include "pch.h"

#include "Editor/Popups/EditorPopupManager.h"

#include "Core/Module/ModuleUnloadListener.h"

#include "Editor/Common/GUI/IEditorPopup.h"

namespace sw::editor
{
    void EditorPopupManager::registerPopup( unique_ptr<IEditorPopup> pPopup )
    {
        if ( pPopup == nullptr )
            return;

        const string popupID = string{ pPopup->getPopupID() };

        // 같은 ID 가 이미 등록돼 있으면 교체한다
        for ( EditorPopupEntry& entry : _listPopup )
        {
            if ( entry._id == popupID )
            {
                entry._pInstance = std::move( pPopup );
                return;
            }
        }

        EditorPopupEntry entry;
        entry._id        = popupID;
        entry._pInstance = std::move( pPopup );
        _listPopup.push_back( std::move( entry ) );
    }

    IEditorPopup* EditorPopupManager::findPopup( string_view id )
    {
        syncWithRegistry();

        for ( EditorPopupEntry& entry : _listPopup )
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
        for ( const EditorPopupEntry& entry : _listPopup )
        {
            if ( entry._id == id && entry._pInstance != nullptr )
                return entry._pInstance->isOpen();
        }
        return false;
    }

    void EditorPopupManager::drawOpenPopups()
    {
        syncWithRegistry();

        for ( EditorPopupEntry& entry : _listPopup )
        {
            if ( entry._pInstance != nullptr && entry._pInstance->isOpen() )
                entry._pInstance->draw();
        }
    }

    void EditorPopupManager::registerDefaultPopups()
    {
        syncWithRegistry();
    }

    void EditorPopupManager::syncWithRegistry()
    {
        using PopupRegistry                = EditorRegistry<EditorPopupRegistration>;
        const EditorRegistrationList& list = PopupRegistry::getList();
        if ( list.getGeneration() == _syncedGeneration )
            return;
        _syncedGeneration = list.getGeneration();

        for ( size_t index = _listPopup.size(); index-- > 0; )
        {
            const EditorPopupEntry& entry = _listPopup[index];
            if ( entry._pRegistration == nullptr || PopupRegistry::find( entry._id ) == entry._pRegistration )
                continue;
            _listPopup.erase( _listPopup.begin() + static_cast<ptrdiff_t>( index ) );
        }
        for ( uint32 index = 0; index < PopupRegistry::getCount(); ++index )
        {
            const EditorPopupRegistration& registration = PopupRegistry::getAt( index );
            bool                           bExisting    = false;
            for ( const EditorPopupEntry& entry : _listPopup )
            {
                bExisting = bExisting || entry._pRegistration == &registration;
            }
            if ( bExisting )
                continue;
            registerPopup( registration._pCreate() );
            for ( EditorPopupEntry& entry : _listPopup )
            {
                if ( entry._id == registration._pID )
                    entry._pRegistration = &registration;
            }
        }
    }

    uint32 EditorPopupManager::releasePopupsWithin( const void* pBegin, const void* pEnd )
    {
        uint32 releasedCount{ 0 };
        for ( size_t index = _listPopup.size(); index-- > 0; )
        {
            if ( IModuleUnloadListener::isAddressWithin( _listPopup[index]._pRegistration, pBegin, pEnd ) == false )
                continue;
            _listPopup.erase( _listPopup.begin() + static_cast<ptrdiff_t>( index ) );
            ++releasedCount;
        }
        return releasedCount;
    }

    void EditorPopupManager::clear()
    {
        _listPopup.clear();
        _syncedGeneration = invalid_index::kUint32;
    }
} // namespace sw::editor
