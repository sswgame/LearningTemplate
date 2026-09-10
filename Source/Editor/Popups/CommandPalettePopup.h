/**
 * @file CommandPalettePopup.h
 * @brief 글로벌 커맨드 팔레트 팝업 (IEditorPopup 구현체)
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/IEditorPopup.h"

namespace sw::editor
{
    /** @brief 커맨드 팔레트 항목 */
    struct CommandPaletteEntry
    {
        string           _category;
        string           _label;
        string           _detail;
        Delegate<void()> _action;
    };

    /**
     * @class CommandPalettePopup
     * @brief Ctrl+Shift+P / Ctrl+Space 로 열리는 글로벌 액션 & 오브젝트 & 윈도우 퍼지 검색기
     * @details 커맨드를 **가지고 있지 않습니다** — 매번 열릴 때 `EditorCommandRegistry` 와 패널
     *          목록과 활성 씬을 읽어 목록을 만듭니다. 팔레트에 커맨드를 더하려면
     *          `EditorCommandGui::registerDefaults` 의 표에 한 줄을 넣으십시오. 그러면 메뉴와
     *          단축키에도 같이 나타납니다 (예전에는 여기에 따로 적어야 해서 서로 어긋났습니다).
     */
    class CommandPalettePopup : public IEditorPopup
    {
    public:
        CommandPalettePopup();
        virtual ~CommandPalettePopup() override = default;

        // ------------------------------------------------------------------------------
        // IEditorPopup 구현
        // ------------------------------------------------------------------------------
        virtual const utf8* getPopupId() const override { return "CommandPalette"; }
        virtual const utf8* getPopupTitle() const override { return "Command Palette"; }

        // ------------------------------------------------------------------------------
        // 정적(Static) 편의 API
        // ------------------------------------------------------------------------------
        static void open();
        static void close();
        static void toggle();
        static bool isOpen();

    protected:
        virtual void drawContent() override;
        virtual void onOpen() override;

    private:
        void rebuildDynamicEntries();

    private:
        vector<CommandPaletteEntry>           _listAllCommand;
        fixed_string<constant::kMaxBuffer128> _searchBuffer;
        int32                                 _selectedIndex{ 0 };
        bool                                  _bJustOpened{ false };
    };
} // namespace sw::editor
