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

#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/GUI/IEditorPopup.h"

namespace sw::editor
{
    /** @brief 퀵 런처 검색 항목 */
    struct QuickLauncherItem
    {
        EditorAssetType _kind{};   ///< 파일 항목의 애셋 종류. 게임 오브젝트 항목은 `Unknown`
        string          _category; ///< 배지 문구 — 애셋 종류의 단수 이름(`EditorAssetTypeInfo::_pDisplayName`) 또는 "GameObject"
        string          _title;
        string          _detail;
        string          _path;
        uint64          _targetObjectId{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class QuickLauncherPopup
     * @brief Ctrl+P 로 여는 애셋 · 게임 오브젝트 퍼지 검색 런처입니다.
     */
    class QuickLauncherPopup : public IEditorPopup
    {
    public:
        /** @brief 팝업 매니저에서 이 팝업을 찾는 id 입니다. */
        static constexpr const utf8* kPopupId = "QuickLauncher";

        QuickLauncherPopup();
        virtual ~QuickLauncherPopup() override = default;

        virtual const utf8* getPopupId() const override { return kPopupId; }
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
