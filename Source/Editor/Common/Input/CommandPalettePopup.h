/**
 * @file CommandPalettePopup.h
 * @brief 전역 커맨드 팔레트 팝업입니다(IEditorPopup 구현).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/GUI/IEditorPopup.h"
#include "Editor/Common/Input/EditorSearchSources.h"

namespace sw::editor
{
    /**
     * @class CommandPalettePopup
     * @brief Ctrl+Shift+P / Ctrl+Space 로 여는 퍼지 검색기입니다 — 명령 · 창 · 씬 오브젝트 · 에셋을 이 순서로 찾는다.
     * @details 항목은 Quick Open 과 같은 검색 제공자(`EditorSearchSources`)가 모은다 — 두 팝업은 진입점과 제공자 순서만 다르다.
     *          커맨드를 **직접 보관하고 있지 않습니다.** 열릴 때마다 `EditorCommandRegistry` 와 패널 목록과 활성 씬을 읽고, 에셋은 워커가 색인한다.
     *          팔레트에 커맨드를 더하려면 `EditorCommandGUI::registerDefaults` 의 테이블에 한 줄을 넣으십시오. 그러면 메뉴와 단축키에도 같이 나타납니다.
     */
    class CommandPalettePopup : public IEditorPopup
    {
    public:
        /** @brief 팝업 매니저에서 이 팝업을 찾는 id 입니다. */
        static constexpr const utf8* kPopupID = "CommandPalette";

        CommandPalettePopup();
        virtual ~CommandPalettePopup() override = default;

        // ------------------------------------------------------------------------------
        // IEditorPopup 구현
        // ------------------------------------------------------------------------------
        virtual const utf8* getPopupID() const override { return kPopupID; }
        virtual const utf8* getPopupTitle() const override { return "Command Palette"; }

        // ------------------------------------------------------------------------------
        // 정적 편의 함수
        // ------------------------------------------------------------------------------
        static void open();
        static void close();
        static void toggle();
        static bool isOpen();

    protected:
        virtual void drawContent() override;
        virtual void onOpen() override;

    private:
        vector<EditorSearchItem>                _listItem;      ///< 명령 · 창 · 씬 오브젝트(열 때), 에셋(색인이 오면 끝에 붙는다)
        EditorAssetIndexJob                     _assetIndexJob; ///< 에셋 색인(Quick Open 과 같은 워커 작업)
        fixed_string<constant::buffer::kMax128> _searchBuffer;
        int32                                   _selectedIndex;
        bool                                    _bJustOpened; ///< NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
    };
} // namespace sw::editor
