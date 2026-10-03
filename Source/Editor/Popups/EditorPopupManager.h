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
#include "Editor/Common/Workspace/EditorRegistry.h"

namespace sw::editor
{

    /** @brief 등록된 팝업 항목 메타데이터 */
    struct EditorPopupEntry
    {
        string                   _id;
        unique_ptr<IEditorPopup> _pInstance;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPopupRegistration
     * @brief 팝업 한 종류의 등록 줄입니다. 팝업의 .cpp 가 `SW_EDITOR_POPUP` 으로 둡니다. id 는 팝업의 `kPopupId` 입니다.
     */
    struct EditorPopupRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "popup";

        unique_ptr<IEditorPopup> ( *_pCreate )();
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 등록 줄이 가리키는 팝업 생성 함수입니다. */
    template <typename TPopup>
    unique_ptr<IEditorPopup> createEditorPopup()
    {
        return make_unique<TPopup>();
    }

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
        /** @brief `SW_EDITOR_POPUP` 으로 등록된 팝업을 순서대로 만들어 둡니다. 두 번째 부름부터는 아무것도 하지 않습니다. */
        void registerDefaultPopups();
        void clear();

    private:
        vector<EditorPopupEntry> _listPopup;
        bool                     _bDefaultsRegistered{ false };
    };
} // namespace sw::editor

/**
 * @brief 팝업 종류를 그 팝업의 .cpp 에서 등록합니다. 예: `SW_EDITOR_POPUP( QuickLauncherPopup, 100 );`
 * @param TPopup 기본 생성자와 `static constexpr const utf8* kPopupId` 가 있는 `IEditorPopup` 구현
 * @param order  그리기 순서(작을수록 먼저 — 뒤의 것이 위에 그려집니다)
 */
#define SW_EDITOR_POPUP( TPopup, order ) \
    SW_EDITOR_REGISTER( ::sw::editor::EditorPopupRegistration, Popup_##TPopup, { TPopup::kPopupId, order }, &::sw::editor::createEditorPopup<TPopup> )
