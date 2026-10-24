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
        uint64          _targetObjectID{ 0 };
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
        void executeItem( const QuickLauncherItem& item );
        void pollFileIndex();
        /** @brief 걸러 낸 결과 줄을 그립니다. 줄을 눌렀으면 그 줄을 고르고 true 를 반환합니다. 목록이 비면 색인 중인지에 따라 안내를 그립니다. */
        bool drawResults( const vector<const QuickLauncherItem*>& listFiltered, bool bIndexOutstanding );
        /** @brief 목록이 오기 전에 누른 Enter 를 기억했다가 목록이 오면 실행으로 바꿉니다. @p bExecute 에 이번 프레임의 실행 요청을 받아 최종 값을 반환합니다. */
        bool resolvePendingExecute( int32 filteredCount, bool bIndexOutstanding, bool bExecute );

    private:
        vector<QuickLauncherItem>             _listAllItem;
        EditorResourceIndexJob                _fileIndexJob;
        fixed_string<constant::kMaxBuffer128> _searchBuffer;
        int32                                 _selectedIndex;
        bool                                  _bJustOpened;
        bool                                  _bExecuteWhenIndexed; ///< 파일 목록이 오기 전에 Enter 를 눌렀다 — 목록이 오면 첫 줄을 연다
    };
} // namespace sw::editor
