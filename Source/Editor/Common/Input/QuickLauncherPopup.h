/**
 * @file QuickLauncherPopup.h
 * @brief 애셋 · 오브젝트(그리고 명령 · 창)를 빠르게 찾아 여는 런처 팝업입니다(Ctrl+P).
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
     * @class QuickLauncherPopup
     * @brief Ctrl+P 로 여는 퍼지 검색 런처입니다 — 씬 오브젝트 · 에셋을 먼저, 명령 · 창을 뒤에 둔다.
     * @details 항목은 Command Palette 와 같은 검색 제공자(`EditorSearchSources`)가 모은다 — 두 팝업은 진입점과 제공자 순서만 다르다.
     *          에셋 색인이 오기 전에는 명령 · 창을 섞지 않는다(파일 이름을 치고 바로 누른 Enter 가 색인을 기다린다).
     */
    class QuickLauncherPopup : public IEditorPopup
    {
    public:
        /** @brief 팝업 매니저에서 이 팝업을 찾는 id 입니다. */
        static constexpr const utf8* kPopupID = "QuickLauncher";

        QuickLauncherPopup();
        virtual ~QuickLauncherPopup() override = default;

        virtual const utf8* getPopupID() const override { return kPopupID; }
        virtual const utf8* getPopupTitle() const override { return "Quick Open"; }

        static void open();
        static void close();
        static void toggle();
        static bool isOpen();
        /** @brief 팝업이 열려 있고 파일 목록(일 스레드)이 아직 안 왔으면 true 입니다. 자동화 시계 잡기 조건이 읽습니다(`QuickLauncherPopup.cpp`). */
        static bool isIndexing();

    protected:
        virtual void drawContent() override;
        virtual void onOpen() override;

    private:
        void rebuildIndex();
        void pollFileIndex();
        /** @brief 걸러 낸 결과 줄을 그립니다. 줄을 눌렀으면 그 줄을 고르고 true 를 반환합니다. 목록이 비면 색인 중인지에 따라 안내를 그립니다. */
        bool drawResults( const vector<const EditorSearchItem*>& listFiltered, bool bIndexOutstanding );
        /** @brief 목록이 오기 전에 누른 Enter 를 기억했다가 목록이 오면 실행으로 바꿉니다. @p bExecute 에 이번 프레임의 실행 요청을 받아 최종 값을 반환합니다. */
        [[nodiscard]] bool resolvePendingExecute( int32 filteredCount, bool bIndexOutstanding, bool bExecute );
        /** @brief 후보 전체의 가장 넓은 종류 표시 폭입니다. 목록이나 글꼴 크기가 바뀔 때만 다시 잰다. */
        float32 getWidestBadgeWidth();

    private:
        vector<EditorSearchItem>                _listItem;     ///< 씬 오브젝트(열 때) · 에셋(색인이 오면 붙는다)
        vector<EditorSearchItem>                _listTailItem; ///< 명령 · 창(색인이 온 뒤에만 거른다)
        EditorAssetIndexJob                     _fileIndexJob;
        fixed_string<constant::buffer::kMax128> _searchBuffer;
        int32                                   _selectedIndex;
        float32                                 _widestBadgeWidth;        ///< 후보 전체(`_listItem` · `_listTailItem`)의 가장 넓은 배지 폭
        float32                                 _badgeFontSize;           ///< `_widestBadgeWidth` 를 잰 글꼴 크기 — 0 이면 다시 잰다
        bool                                    _bJustOpened;             ///< NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        uint8                                   _bExecuteWhenIndexed : 1; ///< 파일 목록이 오기 전에 Enter 를 눌렀다 — 목록이 오면 첫 줄을 연다
    };
} // namespace sw::editor
