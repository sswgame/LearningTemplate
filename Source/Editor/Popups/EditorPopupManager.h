/**
 * @file EditorPopupManager.h
 * @brief 에디터 팝업 · 모달 대화 상자를 등록하고 관리합니다(EditorContext 소유).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Gui/IEditorPopup.h"

namespace sw::editor
{

    /** @brief 등록된 팝업 항목 메타데이터 */
    struct EditorPopupEntry
    {
        string                   _id;
        unique_ptr<IEditorPopup> _pInstance;
    };

    /**
     * @class EditorPopupManager
     * @brief 에디터 팝업 인스턴스를 한곳에서 등록하고 관리합니다(EditorContext 소유).
     */
    class EditorPopupManager
    {
    public:
        EditorPopupManager()  = default;
        ~EditorPopupManager() = default;

        void registerPopup( unique_ptr<IEditorPopup> pPopup );

        template <typename TPopup, typename... TArgs>
        TPopup* registerPopup( TArgs&&... args )
        {
            auto    pPopup = make_unique<TPopup>( std::forward<TArgs>( args )... );
            TPopup* pRaw   = pPopup.get();
            registerPopup( std::move( pPopup ) );
            return pRaw;
        }

        IEditorPopup* findPopup( string_view id );

        template <typename TPopup>
        TPopup* findPopup( string_view id )
        {
            return static_cast<TPopup*>( findPopup( id ) );
        }

        void openPopup( string_view id );
        void closePopup( string_view id );
        void togglePopup( string_view id );
        bool isPopupOpen( string_view id ) const;

        void drawOpenPopups();
        void registerDefaultPopups();
        void clear();

    private:
        vector<EditorPopupEntry> _listPopup;
        bool                     _bDefaultsRegistered{ false };
    };
} // namespace sw::editor
