/**
 * @file ConsolePanel.h
 * @brief Logger를 구독하고 항목을 필터링하는 Output Log 윈도우
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
    /** @brief Logger 출력을 미러링하는 라이브 콘솔 */
    class ConsolePanel : public IEditorPanel
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 생명주기 — 생성 시 Logger 구독, shutdown/소멸 시 해제
        // ------------------------------------------------------------------------------
        /** @brief Output Log 윈도우를 생성하고 Logger를 구독합니다. */
        ConsolePanel();
        /** @brief Logger 구독을 해제하고 윈도우를 파괴합니다. */
        virtual ~ConsolePanel() override;

        // ------------------------------------------------------------------------------
        // 2) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 윈도우 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Output Log"; }
        /** @brief 필터, 레벨 토글, 로그 목록을 그립니다. */
        void drawContent() override;
        /** @brief Logger 구독을 해제합니다. */
        void shutdown( IRHIDevice* pRhiDevice ) override;

        // ------------------------------------------------------------------------------
        // 3) Logger 구독 — 콜백은 드로우 스레드 밖에서 올 수 있음
        // ------------------------------------------------------------------------------
        /** @brief Logger 콜백: 드로우 스레드에서 안전하게 항목을 추가합니다. */
        void onLogWritten( const LogEntry& entry );
        /** @brief Logger 구독을 제거합니다. */
        void unsubscribe();
        /** @brief 필터링된 로그 포인터 목록을 재구성합니다. */
        void updateFilteredEntries( const string& filterStr );

    private:
        deque<LogEntry>                       _listEntry;
        vector<LogEntry>                      _listDrawSnapshot;
        vector<const LogEntry*>               _listVisible;
        string                                _cachedFilter;
        mutex                                 _entriesMutex;
        DelegateHandle                        _logListenerHandle;
        fixed_string<constant::kMaxBuffer128> _filterBuffer;
        bool                                  _arrLevelEnabled[4];
        bool                                  _arrCachedLevelEnabled[4];
        bool                                  _bAutoScroll;
        uint8                                 _bHasNewLogs   : 1;
        [[maybe_unused]] uint8                _reservedFlags : 7;
    };
} // namespace sw::editor
