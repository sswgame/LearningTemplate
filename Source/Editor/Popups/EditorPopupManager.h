/**
 * @file EditorPopupManager.h
 * @brief 에디터 팝업 · 모달 대화 상자를 등록하고 관리합니다(EditorContext 소유).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/IEditorPopup.h"
#include "Editor/Common/Workspace/EditorRegistry.h"

namespace sw::editor
{

    struct EditorPopupRegistration;

    /** @brief 등록된 팝업 항목 메타데이터 */
    struct EditorPopupEntry
    {
        string                         _id;
        unique_ptr<IEditorPopup>       _pInstance;
        const EditorPopupRegistration* _pRegistration{ nullptr }; ///< 이 인스턴스를 만든 등록 줄(직접 registerPopup 한 것은 nullptr)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPopupRegistration
     * @brief 팝업 한 종류의 등록 줄입니다. 팝업의 .cpp 가 `SW_EDITOR_POPUP` 으로 둡니다. id 는 팝업의 `kPopupID` 입니다.
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
        /** @brief `SW_EDITOR_POPUP` 으로 등록된 팝업을 순서대로 만들어 둡니다. 두 번째 부름부터는 등록 목록과 맞추기만 합니다. */
        void registerDefaultPopups();
        /** @brief 등록 목록과 맞춥니다. 새 줄은 만들고 사라진 줄의 팝업은 지웁니다. 세대가 같으면 바로 돌아갑니다. */
        void syncWithRegistry();
        /** @brief 등록 줄이 [@p pBegin, @p pEnd)(모듈 이미지) 안인 팝업을 지웁니다(열려 있으면 닫힌 채로 사라진다). 지운 수를 돌려줍니다. */
        uint32 releasePopupsWithin( const void* pBegin, const void* pEnd );
        void   clear();

    private:
        vector<EditorPopupEntry> _listPopup;
        uint32                   _syncedGeneration{ invalid_index::kUint32 }; ///< 마지막으로 맞춘 등록 세대(맞춘 적이 없으면 kUint32)
    };
} // namespace sw::editor

/**
 * @brief 팝업 종류를 그 팝업의 .cpp 에서 등록합니다. 예: `SW_EDITOR_POPUP( QuickLauncherPopup, 100 );`
 * @param TPopup 기본 생성자와 `static constexpr const utf8* kPopupID` 가 있는 `IEditorPopup` 구현
 * @param order  그리기 순서(작을수록 먼저 — 뒤의 것이 위에 그려집니다)
 */
#define SW_EDITOR_POPUP( TPopup, order ) \
    SW_EDITOR_REGISTER( ::sw::editor::EditorPopupRegistration, Popup_##TPopup, { TPopup::kPopupID, order }, &::sw::editor::createEditorPopup<TPopup> )
