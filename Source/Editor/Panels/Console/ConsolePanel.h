/**
 * @file ConsolePanel.h
 * @brief Logger 를 구독해 항목을 걸러 보여 주는 Output Log 창입니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/String/fixed_string.h"

#include "DevTools/Console/DevConsole.h"

#include "Editor/Common/Commands/Dev/EditorLogCommands.h"
#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Console/Model/ConsoleLogRows.h"

namespace sw::editor
{
    /** @brief Logger 출력을 그대로 보여 주는 콘솔 창입니다. */
    class ConsolePanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "console";

        // ------------------------------------------------------------------------------
        // 1) 수명 주기. 생성할 때 Logger 를 구독하고 shutdown 이나 소멸할 때 해제한다
        // ------------------------------------------------------------------------------
        /** @brief Output Log 창을 만들고 Logger 를 구독합니다. */
        ConsolePanel();
        /** @brief Logger 구독을 해제하고 창을 파괴합니다. */
        virtual ~ConsolePanel() override;

        // ------------------------------------------------------------------------------
        // 2) IEditorPanel: 제목과 그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8*    getPanelTitle() const override { return "Output Log"; }
        EditorDockSlot getDefaultDockSlot() const override { return EditorDockSlot::Bottom; }
        /** @brief 필터, 레벨 토글, 로그 목록을 그립니다. */
        void drawContent() override;
        /** @brief 지우기, 필터, 레벨 토글 툴바를 그립니다. */
        void drawConsoleToolbar( bool& bNewLogs );
        /** @brief 필터를 통과한 로그 목록을 그립니다. */
        void drawLogList( bool bNewLogs );
        /** @brief 툴바에 보이는 레벨별 줄 수입니다. */
        struct LevelCounts
        {
            size_t _error{ 0 };
            size_t _warning{ 0 };
            size_t _info{ 0 };
            size_t _trace{ 0 };
        };
        /** @brief 툴바의 첫 줄(지우기 · 자동 스크롤 · 접기 · 재생 때 지우기 · 오류 때 멈춤)입니다. 지우면 @p bNewLogs 를 끕니다. */
        void drawToolbarToggles( bool& bNewLogs );
        /** @brief 레벨 보기 토글 칩(오류 · 경고 · 정보 · 추적)입니다. */
        void drawLevelChips( const LevelCounts& counts );
        /** @brief 툴바의 복사 · 로그 파일 열기 · 태그 거르기 · 검색창입니다. */
        void drawToolbarCommands();
        /** @brief 로그 한 줄(선택 칠하기 · 글 · 툴팁 · 클릭 선택 · 우클릭 메뉴)을 그립니다. */
        void drawLogRow( int32 rowIndex, float32 rowWidth, float32 lineHeight, bool bListHovered );
        /** @brief 로그 줄의 우클릭 메뉴(IDE 열기 · 복사)입니다. */
        void drawLogRowContextMenu( const LogEntry& entry );
        /** @brief Logger 구독을 해제합니다. */
        void shutdown( IRHIDevice* pRHIDevice ) override;

        /** @brief 태그(카테고리) 필터입니다. */
        EditorLogTagFilter& getTagFilter() { return _tagFilter; }
        /** @brief 입력란이 쓰는 개발 콘솔입니다. 명령 기록을 보관합니다. 응답은 로그로 남아 이 패널에 보입니다. */
        DevConsole& getDevConsole() { return _devConsole; }
        /** @brief 마지막으로 그린 로그(필터링 전)에 이 메시지가 있으면 true 입니다. 에디터 자체 테스트가 씁니다. */
        bool isMessageInSnapshot( string_view message ) const;
        /** @brief 마지막으로 그린 필터링된 목록에 이 메시지가 있으면 true 입니다. 에디터 자체 테스트가 씁니다. */
        bool isMessageVisible( string_view message ) const;

        // ------------------------------------------------------------------------------
        // 3) Logger 구독. 콜백은 그리기 스레드가 아닌 곳에서 호출될 수 있다
        // ------------------------------------------------------------------------------
        /** @brief Logger 콜백입니다. 어느 스레드에서 호출해도 뮤텍스로 보호해 항목을 추가합니다. */
        void onLogWritten( const LogEntry& entry );
        /** @brief 필터를 통과한 로그 포인터 목록을 다시 만듭니다. */
        void updateFilteredEntries( const string& filterStr );
        /** @brief 태그 팝업의 태그 목록(그리기용 복사본에 나온 카테고리, 이름 순)을 복사본이 바뀌었을 때만 다시 짓습니다. */
        const vector<string>& getSnapshotTags() SW_REQUIRES_LOCK( _uiThread );
        /** @brief 로그 행의 카테고리를 반환합니다. 로그를 쓴 위치(`SW_LOG_CALLER`)이고, 없으면 모듈 태그입니다. 태그 필터와 팝업이 이 이름을 씁니다. */
        static const string& getEntryCategory( const LogEntry& entry );
        /** @brief 태그(로거 카테고리)를 켜고 끄는 팝업을 그립니다. */
        void drawTagFilterPopup();
        /** @brief 로그 행이 가리키는 소스 위치를 IDE 로 엽니다. 메시지 안의 위치를 먼저 씁니다. */
        static void openEntryInIde( const LogEntry& entry );
        /** @brief 아래쪽 명령 입력란(개발 콘솔)을 그립니다. 명령과 전역 변수의 get, set, Tab 자동 완성, ↑↓ 기록을 지원합니다. */
        void drawCommandLine();
        /** @brief 지난 그리기의 행 수입니다. 접기와 필터링을 적용한 뒤의 수이며, 프로브 `Editor.OutputLogVisibleRows` 가 읽습니다. */
        uint32 getRowCount() const
        {
            _uiThread.assumeHeld();
            return static_cast<uint32>( _listRow.size() );
        }
        /** @brief 보관 중인 로그를 모두 지웁니다. Clear 버튼과 Clear on Play 가 부릅니다. */
        void clearEntries();
        /** @brief 한 행의 전체 문자열(시각, 태그, 수준, 메시지)을 만듭니다. 복사가 씁니다. */
        static string formatEntryLine( const LogEntry& entry );
        /** @brief 선택한 행을 클립보드에 복사합니다. 접힌 행은 한 번만 복사합니다. */
        void copySelectedRows() const;
        /** @brief 로그 파일 폴더에서 가장 최근 파일을 파일 탐색기로 엽니다. */
        static void showLogFileInExplorer();
        /** @brief Play 시작(Clear on Play)과 새 오류(Error Pause)를 보고 반응합니다. */
        void updatePlayReactions();

    private:
        deque<LogEntry> _listEntry            SW_GUARDED_BY( _entriesMutex );
        ThreadRole                            _uiThread;                  ///< UI 스레드 역할 — 그리기용 사본 · 행 · 선택은 패널을 그리는 스레드만 만진다(잠금이 아니라 분석용 능력)
        deque<LogEntry> _listDrawSnapshot     SW_GUARDED_BY( _uiThread ); ///< 꽉 찬 뒤에도 새 줄마다 앞줄을 O(1) 로 뺀다(vector 앞 지우기는 전체를 민다)
        vector<string> _listTagCache          SW_GUARDED_BY( _uiThread ); ///< 태그 팝업이 보이는 태그 목록. `_snapshotGeneration` 이 바뀐 뒤 팝업을 열 때만 다시 짓는다
        vector<const LogEntry*> _listVisible  SW_GUARDED_BY( _uiThread );
        vector<ConsoleLogRow> _listRow        SW_GUARDED_BY( _uiThread );
        ConsoleLogSelection _selection        SW_GUARDED_BY( _uiThread );
        string                                _cachedFilter;
        EditorLogTagFilter                    _tagFilter;
        uint32                                _cachedTagRevision;
        mutex                                 _entriesMutex;
        Subscription                          _logListenerSubscription; ///< 전역 로그 리스너. 소멸자 · `shutdown` 이 항목보다 먼저 푼다(콜백이 항목을 만진다)
        uint64                                _errorSerial;             ///< 받은 오류 행의 누계. `_entriesMutex` 가 보호한다
        uint64                                _entrySerial;             ///< 받은 행의 누계. `_entriesMutex` 가 보호한다
        uint64                                _snapshotSerial;          ///< 그리기용 복사본이 따라잡은 누계. UI 스레드만 쓴다
        uint64                                _seenErrorSerial;         ///< Error Pause 가 마지막으로 확인한 누계
        uint64                                _snapshotGeneration;      ///< 그리기용 복사본이 바뀔 때(새 줄 · 지우기)마다 오른다. UI 스레드만 쓴다
        uint64                                _tagCacheGeneration;      ///< `_listTagCache` 를 지은 때의 `_snapshotGeneration`
        fixed_string<constant::kMaxBuffer128> _filterBuffer;
        DevConsole                            _devConsole;
        fixed_string<constant::kMaxBuffer512> _commandBuffer;
        bool                                  _arrLevelEnabled[4];
        bool                                  _arrCachedLevelEnabled[4];
        bool                                  _bAutoScroll;
        uint8                                 _bHasNewLogs   : 1 SW_GUARDED_BY( _entriesMutex );
        uint8                                 _bCollapse     : 1; ///< 같은 행을 한 행으로 접는다(유니티의 Collapse)
        uint8                                 _bClearOnPlay  : 1; ///< Play 를 시작하면 지운다
        uint8                                 _bErrorPause   : 1; ///< Play 중 오류 행이 들어오면 멈춘다
        uint8                                 _bScrollToEnd  : 1; ///< 다음 그리기에서 맨 아래로 내린다. 명령을 입력한 뒤에 켠다
        uint8                                 _bWasStopped   : 1; ///< 지난 그리기에 Play 가 정지 상태였다
        uint8                                 _bRowsChanged  : 1; ///< 행을 다시 만들었다. 선택 범위를 자른다
        [[maybe_unused]] uint8                _reservedFlags : 1;
    };
} // namespace sw::editor
