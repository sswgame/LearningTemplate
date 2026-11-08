#include "pch.h"

#include "Editor/Panels/Console/ConsolePanel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Automation/Runner/AutomationLogPolicy.h"
#include "DevTools/Client/Automation/Runner/AutomationSession.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorFontSetup.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Panels/Console/Model/ConsoleViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ConsolePanelInternal
        {
            // 테마 토큰에서 고른다 — 고정 밝은 회색은 라이트 테마에서 바탕에 묻혔다. 신호색은 오류 · 경고에만.
            static const Color& colorForLevel( LogLevel level )
            {
                switch ( level )
                {
                    case LogLevel::Error:
                        return SW_THEME_COLOR( "Signal", "Error" );
                    case LogLevel::Warning:
                        return SW_THEME_COLOR( "Signal", "Warning" );
                    case LogLevel::Trace:
                        return SW_THEME_COLOR( "Base", "TextMuted" );
                    case LogLevel::Info:
                    case LogLevel::Count:
                        return SW_THEME_COLOR( "Base", "Text" );
                }
            }

            /** @brief 로그 목록에서 레벨별 줄 수를 셉니다. */
            static ConsolePanel::LevelCounts countLevels( const deque<ConsoleLogEntry>& listEntry )
            {
                ConsolePanel::LevelCounts counts;
                for ( const ConsoleLogEntry& entry : listEntry )
                {
                    if ( entry._bExpected )
                        continue; // 테스트가 선언한 줄은 오류 · 경고 수가 아니다(행은 흐리게 그린다)
                    switch ( entry._level )
                    {
                        case LogLevel::Error:
                        {
                            ++counts._error;
                            break;
                        }
                        case LogLevel::Warning:
                        {
                            ++counts._warning;
                            break;
                        }
                        case LogLevel::Info:
                        {
                            ++counts._info;
                            break;
                        }
                        case LogLevel::Trace:
                        {
                            ++counts._trace;
                            break;
                        }
                        case LogLevel::Count:
                        {
                            break;
                        }
                    }
                }
                return counts;
            }

            /**
             * @brief 레벨 보기 토글 칩 하나("이름 (줄 수)")를 그립니다. @p pMark 가 있으면 시나리오용 레이블을 남깁니다.
             * @param pSignalColor 글자를 칠할 신호색(오류 · 경고가 있을 때만). nullptr 이면 본문 색이다 — 0 건 · 정보 줄은 신호가 아니다.
             */
            static void drawLevelChip( bool& inoutEnabled, const utf8* pName, size_t count, const Color* pSignalColor, const utf8* pTooltip, const utf8* pMark )
            {
                fixed_string<constant::buffer::kMax32> label;
                formatstring( label.data(), label.capacity(), "%# (%#)", pName, static_cast<uint64>( count ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( label.c_str() ) );
                if ( pSignalColor != nullptr )
                    EditorThemeUtil::pushTextColor( *pSignalColor );
                const bool bClicked = EditorWidgets::drawToggleButton( label.c_str(), inoutEnabled );
                if ( pSignalColor != nullptr )
                    EditorThemeUtil::popTextColor();
                if ( bClicked )
                    inoutEnabled = ( inoutEnabled == false );
                if ( pMark != nullptr )
                    EditorSelfTestMarks::record( pMark );
                EditorWidgets::drawTooltip( pTooltip );
            }

            /** @brief 입력 줄의 Tab(자동완성) · ↑↓(기록)을 개발 콘솔로 처리합니다. */
            static int32 onCommandLineEdit( ImGuiInputTextCallbackData* pData )
            {
                DevConsole* pConsole = static_cast<DevConsole*>( pData->UserData );
                if ( pConsole == nullptr )
                    return 0;
                if ( pData->EventFlag == ImGuiInputTextFlags_CallbackCompletion )
                {
                    string         line( pData->Buf, static_cast<size_t>( pData->BufTextLen ) );
                    vector<string> listCandidate;
                    if ( pConsole->complete( line, listCandidate ) )
                    {
                        pData->DeleteChars( 0, pData->BufTextLen );
                        pData->InsertChars( 0, line.c_str() );
                    }
                    if ( listCandidate.size() > 1 )
                    {
                        string candidates;
                        for ( const string& candidate : listCandidate )
                        {
                            candidates += candidate;
                            candidates += "  ";
                        }
                        SW_LOG_INFO_ID( "ConsolePanel.Completions", "%#", candidates.c_str() );
                    }
                    return 0;
                }
                if ( pData->EventFlag == ImGuiInputTextFlags_CallbackHistory )
                {
                    string     line;
                    const bool bMoved = pData->EventKey == ImGuiKey_UpArrow ? pConsole->moveHistoryBack( line ) : pConsole->moveHistoryForward( line );
                    if ( bMoved )
                    {
                        pData->DeleteChars( 0, pData->BufTextLen );
                        pData->InsertChars( 0, line.c_str() );
                    }
                }
                return 0;
            }

            /** @brief 열린 Output Log 패널입니다. 없으면 nullptr 입니다. */
            static ConsolePanel* findPanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                return pPanels != nullptr ? pPanels->findPanel<ConsolePanel>() : nullptr;
            }

            /** @brief Output Log 가 지난 그리기에 보인 줄 수입니다(접기 · 거르기 뒤). */
            [[nodiscard]] static bool readVisibleRows( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ConsolePanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getRowCount() );
                return true;
            }

            /** @brief `log.repeat <count> <warning|info> <text...>` — 같은 줄을 여러 번 남깁니다(Output Log 접기 테스트). */
            static bool runLogRepeat( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 3 )
                    return false;
                int32      count{ 0 };
                const bool bInvalidLogCount = StringUtil::parseInt( listArgument[0], count ) == false || count <= 0 || count > 1000;
                if ( bInvalidLogCount )
                    return false;
                const bool bWarning = listArgument[1] == "warning";
                if ( bWarning == false && listArgument[1] != "info" )
                    return false;
                string text = listArgument[2];
                for ( size_t argumentIndex = 3; argumentIndex < listArgument.size(); ++argumentIndex )
                {
                    text += " ";
                    text += listArgument[argumentIndex];
                }
                for ( int32 lineIndex = 0; lineIndex < count; ++lineIndex )
                {
                    if ( bWarning )
                        SW_LOG_WARNING_ID( "Console.LogRepeat", "%#", text.c_str() );
                    else
                        SW_LOG_INFO_ID( "Console.LogRepeat", "%#", text.c_str() );
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "logged %# lines", count );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( ConsolePanel, ConsolePanel::kPanelID, EditorPanelCategory::Core, 400 );
    SW_AUTOMATION_WATCH( editorOutputLogVisibleRows, "Editor.OutputLogVisibleRows", "Rows the Output Log drew in the last frame (after its filters and Collapse)",
                         &ConsolePanelInternal::readVisibleRows );
    SW_DEV_COMMAND( LogRepeat, "log.repeat", "log.repeat <count> <warning|info> <text...>", "Write the same log line several times (Output Log Collapse checks)",
                    &ConsolePanelInternal::runLogRepeat );

    ConsolePanel::ConsolePanel()
        : _listEntry{}
        , _listDrawSnapshot{}
        , _listTagCache{}
        , _listVisible{}
        , _listRow{}
        , _selection{}
        , _cachedFilter{}
        , _tagFilter{}
        , _cachedTagRevision{ 0 }
        , _entriesMutex{}
        , _pScenarioLogPolicy{}
        , _logListenerSubscription{}
        , _errorSerial{ 0 }
        , _entrySerial{ 0 }
        , _snapshotSerial{ 0 }
        , _seenErrorSerial{ 0 }
        , _snapshotGeneration{ 1 }
        , _tagCacheGeneration{ 0 }
        , _filterBuffer{}
        , _devConsole{}
        , _commandBuffer{}
        , _arrLevelEnabled{ true, true, true, true }
        , _arrCachedLevelEnabled{ true, true, true, true }
        , _bAutoScroll{ true }
        , _bHasNewLogs{ SW_TRUE }
        , _bCollapse{ SW_FALSE }
        , _bClearOnPlay{ SW_FALSE }
        , _bErrorPause{ SW_FALSE }
        , _bScrollToEnd{ SW_TRUE }
        , _bWasStopped{ SW_TRUE }
        , _bRowsChanged{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
        // 세션이 없는 실행(자동화 실행기를 짓지 않은 구성 · 에디터 서비스 없이 세운 테스트)은 시나리오 선언이 없다 — 단위 테스트 선언만 본다.
        if ( const AutomationSession* const pSession = findSubsystem<AutomationSession>() )
            _pScenarioLogPolicy = pSession->getLogPolicy();
        _logListenerSubscription = Logger::registerGlobalListener(
            SW_DELEGATE_METHOD( LogWrittenDelegate, &ConsolePanel::onLogWritten, this ) );
    }

    ConsolePanel::~ConsolePanel()
    {
        _logListenerSubscription.reset(); // 멤버가 사라지기 전에 — 다른 스레드의 방송이 끝나기를 기다린다
    }

    void ConsolePanel::updateFilteredEntries( const string& filterStr )
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        const EditorListFilter filter{ filterStr };

        _listVisible.clear();
        _listVisible.reserve( _listDrawSnapshot.size() );

        for ( const ConsoleLogEntry& entry : _listDrawSnapshot )
        {
            const uint8 levelIndex = static_cast<uint8>( entry._level );
            if ( levelIndex >= 4 || _arrLevelEnabled[levelIndex] == false )
                continue;

            if ( _tagFilter.isTagVisible( getEntryCategory( entry ) ) == false )
                continue;

            if ( filter.matchesAny( { entry._message, entry._tag, entry.getCategory(), entry._file } ) == false )
                continue;

            _listVisible.push_back( &entry );
        }

        ConsoleLogRows::populate( _listVisible, _bCollapse == SW_TRUE, _listRow );
        _bRowsChanged = SW_TRUE;

        _cachedFilter      = filterStr;
        _cachedTagRevision = _tagFilter.getRevision();
        for ( int32 levelIndex = 0; levelIndex < 4; ++levelIndex )
        {
            _arrCachedLevelEnabled[levelIndex] = _arrLevelEnabled[levelIndex];
        }
    }

    void ConsolePanel::drawContent()
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        updatePlayReactions();

        bool bNewLogs{ false };
        {
            ScopedLock<mutex> lock{ _entriesMutex };
            if ( _bHasNewLogs == SW_TRUE )
            {
                // 새 줄만 덧붙이고 넘친 앞줄을 뺀다. 통째로 다시 담으면 새 로그가 올 때마다 최대 2048 줄(글자열마다 할당)을 복사한다.
                const uint64 newCount = _entrySerial - _snapshotSerial;
                if ( newCount >= _listEntry.size() )
                    _listDrawSnapshot.assign( _listEntry.begin(), _listEntry.end() );
                else
                {
                    _listDrawSnapshot.insert( _listDrawSnapshot.end(), _listEntry.end() - static_cast<ptrdiff_t>( newCount ), _listEntry.end() );
                    while ( _listDrawSnapshot.size() > _listEntry.size() )
                    {
                        _listDrawSnapshot.pop_front();
                    }
                }
                _snapshotSerial = _entrySerial;
                ++_snapshotGeneration;
                bNewLogs     = true;
                _bHasNewLogs = SW_FALSE;
            }
        }

        const bool bCollapseBefore = _bCollapse == SW_TRUE;
        drawConsoleToolbar( bNewLogs );
        const bool bCollapseChanged = bCollapseBefore != ( _bCollapse == SW_TRUE );

        ImGui::Separator();

        const string filterStr{ StringUtil::trim( _filterBuffer.c_str() ) };

        // 필터나 레벨 설정이 바뀌었거나 새 로그가 들어왔을 때만 다시 계산한다
        bool bLevelChanged{ false };
        for ( int32 levelIndex = 0; levelIndex < 4; ++levelIndex )
        {
            if ( _arrCachedLevelEnabled[levelIndex] != _arrLevelEnabled[levelIndex] )
            {
                bLevelChanged = true;
                break;
            }
        }

        const bool bFilterChanged = ( filterStr != _cachedFilter ) || _cachedTagRevision != _tagFilter.getRevision();

        const bool bRefreshFilteredEntries = bNewLogs || bFilterChanged || bLevelChanged || bCollapseChanged;
        if ( bRefreshFilteredEntries )
            updateFilteredEntries( filterStr );

        drawLogList( bNewLogs );
    }

    void ConsolePanel::drawConsoleToolbar( bool& bNewLogs )
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        const LevelCounts counts = ConsolePanelInternal::countLevels( _listDrawSnapshot );

        if ( EditorChrome::beginToolbar( "##ConsoleToolbar" ) )
        {
            drawToolbarToggles( bNewLogs );
            drawLevelChips( counts );
            drawToolbarCommands();
        }

        EditorChrome::endToolbar();
    }

    void ConsolePanel::drawToolbarToggles( bool& bNewLogs )
    {
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Clear" ) ) )
        {
            clearEntries();
            bNewLogs = false;
        }
        EditorSelfTestMarks::record( "console.clear" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "Clear" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Collapse" ) );
        if ( EditorWidgets::drawToggleButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Collapse" ), "Collapse" ), _bCollapse == SW_TRUE ) )
            _bCollapse = _bCollapse == SW_TRUE ? SW_FALSE : SW_TRUE;
        EditorSelfTestMarks::record( "console.collapse" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "Collapse" ) );
    }

    void ConsolePanel::drawLevelChips( const LevelCounts& counts )
    {
        // 신호색은 오류 · 경고가 실제로 있을 때 글자에만 — 0 건이면 본문 색이다(빨간 "Error (0)" 은 문제가 있다고 읽힌다).
        const Color* pErrorSignal   = counts._error > 0 ? &SW_THEME_COLOR( "Signal", "Error" ) : nullptr;
        const Color* pWarningSignal = counts._warning > 0 ? &SW_THEME_COLOR( "Signal", "Warning" ) : nullptr;
        ConsolePanelInternal::drawLevelChip( _arrLevelEnabled[0], "Error", counts._error, pErrorSignal, SW_LOCKEY( "EditorConsole", "ShowErrors" ), nullptr );
        ConsolePanelInternal::drawLevelChip( _arrLevelEnabled[1], "Warning", counts._warning, pWarningSignal, SW_LOCKEY( "EditorConsole", "ShowWarnings" ), nullptr );
        ConsolePanelInternal::drawLevelChip( _arrLevelEnabled[2], "Info", counts._info, nullptr, SW_LOCKEY( "EditorConsole", "ShowInfo" ), "console.level.info" );
        ConsolePanelInternal::drawLevelChip( _arrLevelEnabled[3], "Trace", counts._trace, nullptr, SW_LOCKEY( "EditorConsole", "ShowTrace" ), nullptr );
    }

    void ConsolePanel::drawToolbarCommands()
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        // 한 줄 툴바: 자주 쓰는 것(지우기 · 접기 · 레벨 · 검색 · 태그)만 밖에 두고 가끔 켜는 설정과 내보내기는 옵션 메뉴로 — 언리얼 Output Log 의 Filters · Settings 와 같은 나눔.
        // 예전에는 단추 열하나가 두 줄로 접혀 검색 칸이 셋째 줄로 밀렸다.
        fixed_string<constant::buffer::kMax32> tagLabel;
        if ( _tagFilter.getHiddenCount() > 0 )
            formatstring( tagLabel.data(), tagLabel.capacity(), "%# (-%#)###Tags", SW_LOCKEY( "EdWidget", "TagFilter" ), _tagFilter.getHiddenCount() );
        else
            formatstring( tagLabel.data(), tagLabel.capacity(), "%####Tags", SW_LOCKEY( "EdWidget", "TagFilter" ) );
        const ImGuiStyle& style         = ImGui::GetStyle();
        const float32     trailing      = EditorChrome::measureSmallButton( tagLabel.c_str() ) + ImGui::GetFrameHeight() + style.ItemSpacing.x * 2.0f;
        const float32     minSearch     = ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldMediumEm" );
        const float32     sameLineWidth = EditorChrome::getRemainingLineWidth() - trailing;
        const bool        bSameLine     = sameLineWidth >= minSearch;
        if ( bSameLine )
            ImGui::SameLine();
        const float32 searchWidth = bSameLine ? sameLineWidth : ImGui::GetContentRegionAvail().x - trailing;
        EditorWidgets::drawSearchField( "##log_filter", _filterBuffer, SW_LOCKEY( "EditorConsole", "FilterHint" ), MathUtil::max( searchWidth, minSearch ), false, "console.filter" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "FilterTooltip" ) );

        ImGui::SameLine();
        if ( ImGui::Button( tagLabel.c_str() ) )
            EditorInputGUI::openPopup( "##LogTagFilter" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "TagFilter" ) );
        drawTagFilterPopup();

        ImGui::SameLine();
        if ( ImGui::Button( SW_EDITOR_ICON( "Settings" ), ImVec2{ ImGui::GetFrameHeight(), ImGui::GetFrameHeight() } ) )
            EditorInputGUI::openPopup( "##ConsoleOptions" );
        EditorSelfTestMarks::record( "console.options" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "Options" ) );
        drawOptionsPopup();
    }

    void ConsolePanel::drawOptionsPopup()
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        if ( ImGui::BeginPopup( "##ConsoleOptions" ) == false )
            return;
        ImGui::MenuItem( SW_LOCKEY( "EdLabel", "AutoScroll" ), nullptr, &_bAutoScroll );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "AutoScroll" ) );
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "ClearOnPlay" ), nullptr, _bClearOnPlay == SW_TRUE ) )
            _bClearOnPlay = _bClearOnPlay == SW_TRUE ? SW_FALSE : SW_TRUE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "ClearOnPlay" ) );
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "PauseOnError" ), nullptr, _bErrorPause == SW_TRUE ) )
            _bErrorPause = _bErrorPause == SW_TRUE ? SW_FALSE : SW_TRUE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "ErrorPause" ) );
        ImGui::Separator();
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "CopyVisibleLines" ), nullptr, false, _listVisible.empty() == false ) )
        {
            string allLogs;
            for ( const LogEntry* pEntry : _listVisible )
            {
                if ( pEntry != nullptr )
                    allLogs += ConsoleLogRows::formatEntryLine( *pEntry ) + "\n";
            }
            ImGui::SetClipboardText( allLogs.c_str() );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "CopyAll" ) );
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "OpenLogFile" ) ) )
            showLogFileInExplorer();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "OpenLogFile" ) );
        ImGui::EndPopup();
    }

    void ConsolePanel::drawLogList( bool bNewLogs )
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        editor::EditorSectionDesc logDesc{};
        logDesc._pID   = "##log_scroll";
        logDesc._kind  = editor::EditorSectionKind::Child;
        logDesc._flags = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::FillRemaining |
                         editor::EditorSectionFlags::HorizontalScrollbar;
        EditorChrome::beginSection( logDesc );

        // 0건의 이유를 나눠서 알려 준다. 로그가 아직 없는 것, 검색어가 걸러 낸 것, 레벨을 모두
        // 끈 것은 서로 다른 상황이고 고치는 방법도 다르다.
        if ( _listVisible.empty() )
        {
            const EditorListFilter filter{ _cachedFilter };
            if ( _listDrawSnapshot.empty() )
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorConsole", "NoLogOutput" ) );
            else if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorConsole", "AllLevelsHidden" ) );
        }

        // 로그 줄은 고정폭이다 — 시각 · 태그 열이 줄마다 맞아야 읽힌다(본문 UI 는 그로테스크). 줄 높이도 이 글꼴로 잰다.
        EditorFontSetup::pushMonoFont();
        // 맨 아래에 붙어 있을 때만 새 로그를 따라간다 — 값은 지난 프레임 배치 기준이다(스크롤은 다음 Begin 에 적용된다).
        const float32 lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const bool    bAtBottom  = ConsoleLogRows::isScrolledToBottom( ImGui::GetScrollY(), ImGui::GetScrollMaxY(), lineHeight * 0.5f );

        if ( _bRowsChanged == SW_TRUE )
        {
            _selection.clampTo( static_cast<uint32>( _listRow.size() ) );
            _bRowsChanged = SW_FALSE;
        }
        if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) == false )
            _selection.release();

        const bool    bListHovered = ImGui::IsWindowHovered();
        const float32 rowWidth     = ImGui::GetContentRegionAvail().x;

        ImGuiListClipper clipper;
        clipper.Begin( static_cast<int32>( _listRow.size() ) );
        while ( clipper.Step() )
        {
            for ( int32 rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex )
            {
                drawLogRow( rowIndex, rowWidth, lineHeight, bListHovered );
            }
        }

        // Ctrl+C 는 `Editor.Panel.console` 레이어의 액션이다(이 창이 포커스일 때만 켜진다).
        const bool bCopyShortcutPressed = _selection.hasSelection() && EditorInputGUI::wasActionTriggered( "console.copySelection" );
        if ( bCopyShortcutPressed )
            copySelectedRows();

        const bool bScrollRequested = _bScrollToEnd == SW_TRUE;
        if ( _bAutoScroll && ( bScrollRequested || ( bNewLogs && bAtBottom ) ) )
            ImGui::SetScrollHereY( 1.0f );
        _bScrollToEnd = SW_FALSE;
        EditorFontSetup::popMonoFont();

        EditorChrome::endSection();

        EditorWidgets::drawCountLabel( static_cast<uint32>( _listRow.size() ), static_cast<uint32>( _listDrawSnapshot.size() ), SW_LOCKEY( "EdWidget", "Lines" ) );
        ImGui::SameLine();
        drawCommandLine();
    }

    void ConsolePanel::drawLogRow( int32 rowIndex, float32 rowWidth, float32 lineHeight, bool bListHovered )
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        const ConsoleLogRow&   row   = _listRow[static_cast<size_t>( rowIndex )];
        const ConsoleLogEntry& entry = *row._pEntry;
        // 테스트가 선언한 줄(`ConsoleLogEntry::_bExpected`)은 신호색 대신 흐린 색으로 — 진짜 오류와 눈으로 갈린다.
        const Color& levelColor = entry._bExpected ? SW_THEME_COLOR( "Base", "TextMuted" ) : ConsolePanelInternal::colorForLevel( entry._level );
        ImGui::PushID( rowIndex );

        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const ImVec2 rowMax{ rowMin.x + rowWidth, rowMin.y + lineHeight };
        if ( _selection.isSelected( static_cast<uint32>( rowIndex ) ) )
            ImGui::GetWindowDrawList()->AddRectFilled( rowMin, rowMax, ImGui::GetColorU32( ImGuiCol_Header ) );

        if ( row._repeatCount > 1 )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( levelColor ), "(%u)", row._repeatCount );
            ImGui::SameLine( 0.0f, ImGui::GetStyle().ItemInnerSpacing.x );
        }
        // 줄마다 되풀이되는 머리는 줄인다: 날짜는 빼고 시각만, 수준은 Info 가 아닐 때만 적는다 — 언리얼 Output Log 의 "Category: Warning: …" 처럼
        // 눈이 메시지에 먼저 닿는다. 날짜 · 수준을 포함한 전체 줄은 복사(Copy)와 로그 파일에 그대로 남는다.
        const string_view timeStamp{ entry._timeStamp };
        const size_t      dateEnd = timeStamp.rfind( ' ' );
        const string      timeOfDay( dateEnd == string_view::npos ? timeStamp : timeStamp.substr( dateEnd + 1 ) );
        ImGui::TextDisabled( "%s", timeOfDay.c_str() );
        ImGui::SameLine( 0.0f, 0.0f );
        EditorThemeUtil::pushTextColor( SW_THEME_COLOR( "Base", "TextMuted" ) ); // 태그는 줄마다 되풀이되는 열이라 강조색을 쓰지 않는다
        ImGui::Text( " %s:", entry._tag.c_str() );
        EditorThemeUtil::popTextColor();
        if ( entry._level != LogLevel::Info )
        {
            ImGui::SameLine( 0.0f, 0.0f );
            if ( entry._bExpected )
                ImGui::TextColored( EditorColorUtil::toImVec4( levelColor ), " %s (%s):", ConsoleLogRows::levelName( entry._level ), SW_LOCKEY( "EdText", "ExpectedByTest" ) );
            else
                ImGui::TextColored( EditorColorUtil::toImVec4( levelColor ), " %s:", ConsoleLogRows::levelName( entry._level ) );
        }
        ImGui::SameLine( 0.0f, 0.0f );
        ImGui::TextUnformatted( " " );
        ImGui::SameLine( 0.0f, 0.0f );
        ImGui::TextUnformatted( entry._message.c_str() );

        if ( entry._file.empty() == false && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
        {
            const string tooltipText = SW_LOCKEYFORMAT( "EditorConsole", "EntrySource", TextArgumentList().addText( "file", entry._file.c_str() ).addInteger( "line", entry._line ) );
            EditorWidgets::drawTooltip( tooltipText.c_str() );
        }
        if ( EditorInputGUI::wasItemDoubleClicked() )
            openEntryInIde( entry );

        // 줄을 누르면 고르고, 누른 채 끌거나 Shift 로 누르면 넓힌다(언리얼 Output Log 의 여러 줄 선택). 선택 밖 줄을 오른쪽으로 누르면 그 줄만 고른다(R2).
        if ( bListHovered && ImGui::IsMouseHoveringRect( rowMin, rowMax, false ) )
        {
            const EditorPointerState pointer = EditorInputGUI::readPointer();
            if ( pointer.wasPressed( EditorMouseButton::Left ) )
                _selection.press( static_cast<uint32>( rowIndex ), pointer.hasModifier( ModifierKey::Shift ) );
            else if ( pointer.wasPressed( EditorMouseButton::Right ) && _selection.isSelected( static_cast<uint32>( rowIndex ) ) == false )
            {
                _selection.press( static_cast<uint32>( rowIndex ), false );
                _selection.release();
            }
            else if ( _selection.isDragging() )
                _selection.dragTo( static_cast<uint32>( rowIndex ) );
        }

        drawLogRowContextMenu( entry );
        ImGui::PopID();
    }

    void ConsolePanel::drawLogRowContextMenu( const LogEntry& entry )
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        if ( EditorInputGUI::beginItemContextMenu( "LogEntryCtx" ) == false )
            return;
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "OpenInIDE" ) ) )
            openEntryInIde( entry );
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "CopyMessage" ) ) )
            ImGui::SetClipboardText( entry._message.c_str() );
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "CopyFullLogLine" ) ) )
        {
            const string full = ConsoleLogRows::formatEntryLineWithSource( entry );
            ImGui::SetClipboardText( full.c_str() );
        }
        fixed_string<constant::buffer::kMax64> copySelectedLabel;
        formatstring( copySelectedLabel.data(), copySelectedLabel.capacity(), "Copy Selected (%u lines)", _selection.getCount() );
        if ( ImGui::MenuItem( copySelectedLabel.c_str(), "Ctrl+C", false, _selection.hasSelection() ) )
            copySelectedRows();
        ImGui::EndPopup();
    }

    void ConsolePanel::drawCommandLine()
    {
        ImGui::SetNextItemWidth( -1.0f );
        constexpr ImGuiInputTextFlags kFlags     = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion;
        const bool                    bSubmitted = ImGui::InputTextWithHint( "##ConsoleCommand", SW_LOCKEY( "EditorConsole", "CommandHint" ), _commandBuffer.data(),
                                                                             _commandBuffer.capacity(), kFlags, &ConsolePanelInternal::onCommandLineEdit, &_devConsole );
        if ( bSubmitted )
        {
            (void)_devConsole.submit( _commandBuffer.c_str() ); // 답 · 실패는 로그로 남아 이 패널에 보인다
            _commandBuffer.clear();
            _bAutoScroll  = true;
            _bScrollToEnd = SW_TRUE;           // 친 명령의 답을 보도록 맨 아래로 내린다
            ImGui::SetKeyboardFocusHere( -1 ); // 이어서 칠 수 있게 입력 줄에 머문다
        }
    }

    string_view ConsolePanel::getEntryCategory( const LogEntry& entry )
    {
        const string_view category = entry.getCategory();
        return category.empty() ? string_view{ entry._tag } : category;
    }

    bool ConsolePanel::isMessageInSnapshot( string_view message ) const
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        return VectorUtil::containsBy<&LogEntry::_message>( _listDrawSnapshot, message );
    }

    bool ConsolePanel::isMessageVisible( string_view message ) const
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        for ( const LogEntry* pEntry : _listVisible )
        {
            if ( pEntry != nullptr && pEntry->_message == message )
                return true;
        }
        return false;
    }

    void ConsolePanel::clearEntries()
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        ScopedLock<mutex> lock{ _entriesMutex };
        _listEntry.clear();
        _listDrawSnapshot.clear();
        _listVisible.clear();
        _listRow.clear();
        _selection.clear();
        _snapshotSerial = _entrySerial;
        ++_snapshotGeneration;
        _bHasNewLogs = SW_FALSE;
    }

    void ConsolePanel::copySelectedRows() const
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        if ( _selection.hasSelection() == false )
            return;
        string text;
        for ( uint32 rowIndex = _selection.getFirst(); rowIndex <= _selection.getLast() && rowIndex < _listRow.size(); ++rowIndex )
        {
            text += ConsoleLogRows::formatEntryLine( *_listRow[rowIndex]._pEntry );
            text += "\n";
        }
        ImGui::SetClipboardText( text.c_str() );
    }

    void ConsolePanel::showLogFileInExplorer()
    {
        ILogSink* pSink = Logger::getGlobalSink();
        if ( pSink == nullptr || pSink->getLogFolderPath().empty() )
        {
            SW_LOG_WARNING_ID( "ConsolePanel.NoLogFileFolder", "No log file folder (the log sink writes no file)" );
            return;
        }
        // 이번 실행의 파일은 싱크만 안다 — 폴더의 "가장 새 파일" 은 같은 폴더를 쓰는 다른 프로세스의 것일 수 있다.
        const string logFilePath = pSink->getLogFilePath();
        (void)ConsoleViewModel::showInFileExplorer( logFilePath.empty() ? string( pSink->getLogFolderPath() ) : logFilePath );
    }

    void ConsolePanel::updatePlayReactions()
    {
        const bool bStopped          = ConsoleViewModel::isPlayStopped();
        const bool bClearOnPlayStart = _bClearOnPlay == SW_TRUE && _bWasStopped == SW_TRUE && bStopped == false;
        if ( bClearOnPlayStart )
            clearEntries();
        _bWasStopped = bStopped ? SW_TRUE : SW_FALSE;

        uint64 errorSerial{ 0 };
        {
            ScopedLock<mutex> lock{ _entriesMutex };
            errorSerial = _errorSerial;
        }
        // 오류는 로그를 쓴 다음 그리기에서 본다 — 그 프레임이 끝나기 전에 멈추는 유니티 Error Pause 보다 한 프레임 늦다.
        const bool bPauseOnNewError = _bErrorPause == SW_TRUE && errorSerial != _seenErrorSerial && ConsoleViewModel::isPlayRunning();
        if ( bPauseOnNewError )
            ConsoleViewModel::pausePlay();
        _seenErrorSerial = errorSerial;
    }

    void ConsolePanel::drawTagFilterPopup()
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        if ( ImGui::BeginPopup( "##LogTagFilter" ) == false )
            return;
        if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "ShowAll" ) ) )
            _tagFilter.showAll();
        ImGui::Separator();
        for ( const string& tag : getSnapshotTags() )
        {
            bool bVisible = _tagFilter.isTagVisible( tag );
            if ( ImGui::Checkbox( tag.empty() ? "(none)" : tag.c_str(), &bVisible ) )
                _tagFilter.setTagVisible( tag, bVisible );
        }
        ImGui::EndPopup();
    }

    const vector<string>& ConsolePanel::getSnapshotTags()
    {
        if ( _tagCacheGeneration == _snapshotGeneration )
            return _listTagCache;
        // 지금 보관하고 있는 로그에 나온 태그만 보인다.
        _listTagCache.clear();
        _listTagCache.reserve( _listDrawSnapshot.size() );
        for ( const ConsoleLogEntry& entry : _listDrawSnapshot )
        {
            _listTagCache.push_back( string{ getEntryCategory( entry ) } );
        }
        VectorUtil::sortUnique( _listTagCache );
        _tagCacheGeneration = _snapshotGeneration;
        return _listTagCache;
    }

    void ConsolePanel::openEntryInIde( const LogEntry& entry )
    {
        EditorSourceLocation location;
        if ( EditorSourceLocation::findLogEntryLocation( entry._message, entry._file, entry._line, location ) )
            (void)EditorSourceLocation::openInIde( location ); // 실패는 openInIde 가 경고로 남긴다
    }

    void ConsolePanel::shutdown()
    {
        _logListenerSubscription.reset();
    }

    void ConsolePanel::onLogWritten( const LogEntry& entry )
    {
        // 어느 스레드 · 어느 스코프에서 쓴 로그든 콘솔이 보관하고 있는 사본은 에디터 담당이다.
        SW_MEMORY_SCOPE( Editor );
        ConsoleLogEntry consoleEntry;
        static_cast<LogEntry&>( consoleEntry ) = entry;
        // 받는 순간 묻는다 — 선언은 케이스 · 시나리오 단계가 도는 동안만 살고, 로그는 부른 스레드에서 바로 온다. 시나리오는 로그 게이트와 같은 판정(`classify`)을 읽는다
        // (`<AllowLog>` 는 나와도 되는 줄일 뿐 기대한 줄이 아니라 경고색 그대로다). 언리얼 Automation 의 `AddExpectedError` 도 기대 오류를 로그 출력에서 따로 표시한다.
        const bool bScenarioDeclared = _pScenarioLogPolicy != nullptr &&
                                       _pScenarioLogPolicy->classify( entry._level, entry._pID != nullptr ? string_view{ entry._pID } : string_view{}, entry._message ) == AutomationLogResult::Declared;
        consoleEntry._bExpected = bScenarioDeclared || ( AutomationLogPolicy::isGatedLevel( entry._level ) && test::isExpectedLog( entry._pID ) );
        ScopedLock<mutex> lock{ _entriesMutex };
        if ( entry._level == LogLevel::Error && consoleEntry._bExpected == false )
            ++_errorSerial; // 오류 때 멈춤은 진짜 오류에만
        _listEntry.push_back( std::move( consoleEntry ) );
        ++_entrySerial;
        while ( _listEntry.size() > constant::buffer::kMax2048 )
        {
            _listEntry.pop_front();
        }
        _bHasNewLogs = SW_TRUE;
    }

} // namespace sw::editor
