/**
 * @file QuickLauncherPopup.h
 * @brief 애셋 · 오브젝트를 빠르게 찾아 여는 런처 팝업입니다(Ctrl+P).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIo.h"
#include "Editor/Common/Gui/IEditorPopup.h"

namespace sw::editor
{
    /** @brief 퀵 런처 검색 항목 */
    struct QuickLauncherItem
    {
        string _category; /**< "Scene", "Prefab", "Texture", "Shader", "Data", "GameObject" */
        string _title;
        string _detail;
        string _path;
        uint64 _targetObjectId{ 0 };
    };

    /**
     * @class QuickLauncherPopup
     * @brief Ctrl+P 로 여는 애셋 · 게임 오브젝트 퍼지 검색 런처입니다.
     */
    class QuickLauncherPopup : public IEditorPopup
    {
    public:
        QuickLauncherPopup();
        virtual ~QuickLauncherPopup() override = default;

        virtual const utf8* getPopupId() const override { return "QuickLauncher"; }
        virtual const utf8* getPopupTitle() const override { return "Quick Open"; }

        static void open();
        static void close();
        static void toggle();
        static bool isOpen();

    protected:
        virtual void drawContent() override;
        virtual void onOpen() override;

    private:
        void rebuildIndex();
        void executeItem( const QuickLauncherItem& item );
        void pollFileIndex();

    private:
        vector<QuickLauncherItem>             _listAllItem;
        EditorResourceIndexJob                _fileIndexJob;
        fixed_string<constant::kMaxBuffer128> _searchBuffer;
        int32                                 _selectedIndex;
        bool                                  _bJustOpened;
    };
} // namespace sw::editor
