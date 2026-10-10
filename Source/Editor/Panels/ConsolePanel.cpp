#include "pch.h"

#include "Editor/Panels/ConsolePanel.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/EditorPanelManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ConsolePanelInternal
        {
            static ImVec4 colorForLevel( LogLevel level )
            {
                switch ( level )
                {
                    case LogLevel::Error:
                        return ImVec4( 1.0f, 0.4f, 0.4f, 1.0f );
                    case LogLevel::Warning:
                        return ImVec4( 1.0f, 0.8f, 0.4f, 1.0f );
                    case LogLevel::Info:
                        return ImVec4( 0.8f, 0.8f, 0.8f, 1.0f );
                    case LogLevel::Trace:
                        return ImVec4( 0.6f, 0.6f, 0.6f, 1.0f );
                    case LogLevel::Count:
                        return ImVec4( 1.0f, 1.0f, 1.0f, 1.0f );
                }
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
                        SW_LOG_INFO( "%#", candidates.c_str() );
                    }
                    return 0;
                }
                if ( pData->EventFlag == ImGuiInputTextFlags_CallbackHistory )
                {
                    const string* pLine = pData->EventKey == ImGuiKey_UpArrow ? pConsole->moveHistoryBack() : pConsole->moveHistoryForward();
                    if ( pLine != nullptr )
                    {
                        pData->DeleteChars( 0, pData->BufTextLen );
                        pData->InsertChars( 0, pLine->c_str() );
                    }
                }
                return 0;
            }

            static const utf8* levelName( LogLevel level )
            {
                static constexpr const utf8* kArrNames[] = { "Error", "Warning", "Info", "Trace" };
                const uint8                  index       = static_cast<uint8>( level );
                if ( index >= 4 )
                    return "Info";
                return kArrNames[index];
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( ConsolePanel, "console", EditorPanelCategory::Core, 400 );

    ConsolePanel::ConsolePanel()
        : _listEntry{}
        , _listDrawSnapshot{}
        , _listVisible{}
        , _cachedFilter{}
        , _tagFilter{}
        , _cachedTagRevision{ 0 }
        , _entriesMutex{}
        , _logListenerHandle{}
        , _filterBuffer{}
        , _devConsole{}
        , _commandBuffer{}
        , _arrLevelEnabled{ true, true, true, true }
        , _arrCachedLevelEnabled{ true, true, true, true }
        , _bAutoScroll{ true }
        , _bHasNewLogs{ SW_TRUE }
        , _reservedFlags{ 0 }
    {
        _logListenerHandle = Logger::addGlobalListener(
            SW_DELEGATE_METHOD( LogWrittenDelegate, &ConsolePanel::onLogWritten, this ) );
    }

    ConsolePanel::~ConsolePanel()
    {
        unsubscribe();
    }

    void ConsolePanel::updateFilteredEntries( const string& filterStr )
    {
        const EditorListFilter filter{ filterStr };

        _listVisible.clear();
        _listVisible.reserve( _listDrawSnapshot.size() );

        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            const uint8 levelIndex = static_cast<uint8>( entry._level );
            if ( levelIndex >= 4 || _arrLevelEnabled[levelIndex] == false )
                continue;

            if ( _tagFilter.isTagVisible( getEntryCategory( entry ) ) == false )
                continue;

            if ( filter.matchesAny( { entry._message, entry._tag, entry._caller, entry._file } ) == false )
                continue;

            _listVisible.push_back( &entry );
        }

        _cachedFilter      = filterStr;
        _cachedTagRevision = _tagFilter.getRevision();
        for ( int32 levelIndex = 0; levelIndex < 4; ++levelIndex )
        {
            _arrCachedLevelEnabled[levelIndex] = _arrLevelEnabled[levelIndex];
        }
    }

    void ConsolePanel::drawContent()
    {
        bool bNewLogs{ false };
        {
            std::scoped_lock<mutex> lock{ _entriesMutex };
            if ( _bHasNewLogs == SW_TRUE )
            {
                _listDrawSnapshot.assign( _listEntry.begin(), _listEntry.end() );
                bNewLogs     = true;
                _bHasNewLogs = SW_FALSE;
            }
        }

        drawConsoleToolbar( bNewLogs );

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

        if ( bNewLogs || bFilterChanged || bLevelChanged )
            updateFilteredEntries( filterStr );

        drawLogList( bNewLogs );
    }

    void ConsolePanel::drawConsoleToolbar( bool& bNewLogs )
    {
        size_t errorCount{ 0 };
        size_t warnCount{ 0 };
        size_t infoCount{ 0 };
        size_t traceCount{ 0 };
        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            switch ( entry._level )
            {
                case LogLevel::Error:
                {
                    ++errorCount;
                    break;
                }
                case LogLevel::Warning:
                {
                    ++warnCount;
                    break;
                }
                case LogLevel::Info:
                {
                    ++infoCount;
                    break;
                }
                case LogLevel::Trace:
                {
                    ++traceCount;
                    break;
                }
                case LogLevel::Count:
                {
                    break;
                }
            }
        }

        if ( EditorChrome::beginToolbar( "##ConsoleToolbar" ) )
        {
            if ( ImGui::Button( "Clear" ) )
            {
                std::scoped_lock<mutex> lock{ _entriesMutex };
                _listEntry.clear();
                _listDrawSnapshot.clear();
                _listVisible.clear();
                _bHasNewLogs = SW_FALSE;
                bNewLogs     = false;
            }
            EditorWidgets::drawTooltip( "콘솔 로그 출력을 모두 지웁니다" );

            ImGui::SameLine();
            ImGui::Checkbox( "Auto-scroll", &_bAutoScroll );
            EditorWidgets::drawTooltip( "새로운 로그가 추가될 때 자동으로 맨 아래로 스크롤합니다" );

            fixed_string<constant::kMaxBuffer32> arrErrLabel;
            formatstring( arrErrLabel.data(), arrErrLabel.capacity(), "Error (%zu)", errorCount );
            fixed_string<constant::kMaxBuffer32> arrWarnLabel;
            formatstring( arrWarnLabel.data(), arrWarnLabel.capacity(), "Warning (%zu)", warnCount );
            fixed_string<constant::kMaxBuffer32> arrInfoLabel;
            formatstring( arrInfoLabel.data(), arrInfoLabel.capacity(), "Info (%zu)", infoCount );
            fixed_string<constant::kMaxBuffer32> arrTraceLabel;
            formatstring( arrTraceLabel.data(), arrTraceLabel.capacity(), "Trace (%zu)", traceCount );

            constexpr editor::Color4 kTraceChip{ 0.40f, 0.40f, 0.45f, 1.0f };

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( arrErrLabel.c_str(), _arrLevelEnabled[0], editor::style::kError ) )
                _arrLevelEnabled[0] = ( _arrLevelEnabled[0] == false );
            EditorWidgets::drawTooltip( "오류(Error) 수준 로그 표시 여부를 토글합니다" );

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( arrWarnLabel.c_str(), _arrLevelEnabled[1], editor::style::kWarn ) )
                _arrLevelEnabled[1] = ( _arrLevelEnabled[1] == false );
            EditorWidgets::drawTooltip( "경고(Warning) 수준 로그 표시 여부를 토글합니다" );

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( arrInfoLabel.c_str(), _arrLevelEnabled[2], editor::style::kOk ) )
                _arrLevelEnabled[2] = ( _arrLevelEnabled[2] == false );
            EditorWidgets::drawTooltip( "정보(Info) 수준 로그 표시 여부를 토글합니다" );

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( arrTraceLabel.c_str(), _arrLevelEnabled[3], kTraceChip ) )
                _arrLevelEnabled[3] = ( _arrLevelEnabled[3] == false );
            EditorWidgets::drawTooltip( "상세(Trace) 수준 로그 표시 여부를 토글합니다" );

            ImGui::SameLine();
            if ( ImGui::Button( "Copy All" ) && _listVisible.empty() == false )
            {
                string allLogs;
                for ( const LogEntry* pEntry : _listVisible )
                {
                    if ( pEntry != nullptr )
                    {
                        allLogs += "[" + pEntry->_timeStamp + "] [" + pEntry->_tag + "] [" + ConsolePanelInternal::levelName( pEntry->_level ) +
                                   "] - " + pEntry->_message + "\n";
                    }
                }
                ImGui::SetClipboardText( allLogs.c_str() );
            }
            EditorWidgets::drawTooltip( "필터링된 모든 콘솔 로그를 클립보드에 복사합니다" );

            ImGui::SameLine();
            fixed_string<constant::kMaxBuffer32> tagLabel;
            if ( _tagFilter.getHiddenCount() > 0 )
                formatstring( tagLabel.data(), tagLabel.capacity(), "Tags (-%u)", _tagFilter.getHiddenCount() );
            else
                formatstring( tagLabel.data(), tagLabel.capacity(), "Tags" );
            if ( ImGui::Button( tagLabel.c_str() ) )
                ImGui::OpenPopup( "##LogTagFilter" );
            EditorWidgets::drawTooltip( "로그 태그(카테고리)마다 보이기를 켜고 끕니다" );
            drawTagFilterPopup();

            EditorWidgets::drawSearchField( "##log_filter", _filterBuffer, "Filter (tag / message / file)", -1.0f, false );
            EditorWidgets::drawTooltip( "로그 메시지, 모듈 태그, 파일명으로 필터링하여 검색합니다" );
        }

        EditorChrome::endToolbar();
    }

    void ConsolePanel::drawLogList( bool bNewLogs )
    {
        editor::EditorSectionDesc logDesc{};
        logDesc._pId   = "##log_scroll";
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
                EditorWidgets::drawEmptyHint( "No log output yet." );
            else if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( "All log levels are hidden. Re-enable one in the toolbar." );
        }

        ImGuiListClipper clipper;
        clipper.Begin( static_cast<int32>( _listVisible.size() ) );
        while ( clipper.Step() )
        {
            for ( int32 logIndex = clipper.DisplayStart; logIndex < clipper.DisplayEnd; ++logIndex )
            {
                const LogEntry& entry = *_listVisible[static_cast<size_t>( logIndex )];
                ImGui::PushID( logIndex );

                ImGui::TextDisabled( "[%s]", entry._timeStamp.c_str() );
                ImGui::SameLine( 0.0f, 0.0f );
                EditorThemeUtil::pushTextColor( EditorThemeUtil::getInfoColor() );
                if ( entry._caller.empty() )
                    ImGui::Text( " [%s]", entry._tag.c_str() );
                else
                    ImGui::Text( " [%s:%s]", entry._tag.c_str(), entry._caller.c_str() );
                EditorThemeUtil::popTextColor();
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::TextColored( ConsolePanelInternal::colorForLevel( entry._level ), " [%s]", ConsolePanelInternal::levelName( entry._level ) );
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::TextUnformatted( " - " );
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::TextUnformatted( entry._message.c_str() );

                if ( entry._file.empty() == false )
                {
                    fixed_string<constant::kMaxBuffer256> tooltipText;
                    formatstring( tooltipText.data(), tooltipText.capacity(), "%s(%d) - double-click to open in IDE", entry._file.c_str(), entry._line );
                    EditorWidgets::drawTooltip( tooltipText.c_str() );
                }
                if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                    openEntryInIde( entry );

                if ( ImGui::BeginPopupContextItem( "LogEntryCtx" ) )
                {
                    if ( ImGui::MenuItem( "Open in IDE" ) )
                        openEntryInIde( entry );
                    if ( ImGui::MenuItem( "Copy Message" ) )
                        ImGui::SetClipboardText( entry._message.c_str() );
                    if ( ImGui::MenuItem( "Copy Full Log Line" ) )
                    {
                        string full = "[" + entry._timeStamp + "] [" + entry._tag + "] [" + ConsolePanelInternal::levelName( entry._level ) +
                                      "] - " + entry._message;
                        if ( entry._file.empty() == false )
                            full += " (" + entry._file + ":" + to_string( entry._line ) + ")";
                        ImGui::SetClipboardText( full.c_str() );
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        if ( _bAutoScroll && bNewLogs )
            ImGui::SetScrollHereY( 1.0f );

        EditorChrome::endSection();

        EditorWidgets::drawCountLabel( static_cast<uint32>( _listVisible.size() ), static_cast<uint32>( _listDrawSnapshot.size() ),
                                       "lines" );
        ImGui::SameLine();
        drawCommandLine();
    }

    void ConsolePanel::drawCommandLine()
    {
        ImGui::SetNextItemWidth( -1.0f );
        constexpr ImGuiInputTextFlags kFlags     = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion;
        const bool                    bSubmitted = ImGui::InputTextWithHint( "##ConsoleCommand", "> command or gv_name [value] (Tab completes, Up/Down history, help)", _commandBuffer.data(),
                                                                             _commandBuffer.capacity(), kFlags, &ConsolePanelInternal::onCommandLineEdit, &_devConsole );
        if ( bSubmitted )
        {
            (void)_devConsole.submit( _commandBuffer.c_str() ); // 답 · 실패는 로그로 남아 이 패널에 보인다
            _commandBuffer.clear();
            _bAutoScroll = true;
            ImGui::SetKeyboardFocusHere( -1 ); // 이어서 칠 수 있게 입력 줄에 머문다
        }
    }

    const string& ConsolePanel::getEntryCategory( const LogEntry& entry )
    {
        return entry._caller.empty() ? entry._tag : entry._caller;
    }

    bool ConsolePanel::isMessageInSnapshot( string_view message ) const
    {
        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            if ( entry._message == message )
                return true;
        }
        return false;
    }

    bool ConsolePanel::isMessageVisible( string_view message ) const
    {
        for ( const LogEntry* pEntry : _listVisible )
        {
            if ( pEntry != nullptr && pEntry->_message == message )
                return true;
        }
        return false;
    }

    void ConsolePanel::drawTagFilterPopup()
    {
        if ( ImGui::BeginPopup( "##LogTagFilter" ) == false )
            return;
        // 지금 들고 있는 로그에 나온 태그만 보인다(순서는 처음 나온 순서).
        vector<string> listTag;
        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            const string& category = getEntryCategory( entry );
            bool          bSeen{ false };
            for ( const string& tag : listTag )
            {
                bSeen = bSeen || tag == category;
            }
            if ( bSeen == false )
                listTag.push_back( category );
        }
        std::sort( listTag.begin(), listTag.end() );
        if ( ImGui::MenuItem( "Show All" ) )
            _tagFilter.showAll();
        ImGui::Separator();
        for ( const string& tag : listTag )
        {
            bool bVisible = _tagFilter.isTagVisible( tag );
            if ( ImGui::Checkbox( tag.empty() ? "(none)" : tag.c_str(), &bVisible ) )
                _tagFilter.setTagVisible( tag, bVisible );
        }
        ImGui::EndPopup();
    }

    void ConsolePanel::openEntryInIde( const LogEntry& entry )
    {
        EditorSourceLocation location;
        if ( EditorLogCommands::findLogEntryLocation( entry._message, entry._file, entry._line, location ) )
            (void)EditorLogCommands::openInIde( location ); // 실패는 openInIde 가 경고로 남긴다
    }

    void ConsolePanel::shutdown( IRHIDevice* /*rhiDevice*/ )
    {
        unsubscribe();
    }

    void ConsolePanel::onLogWritten( const LogEntry& entry )
    {
        // 어느 스레드 · 어느 스코프에서 쓴 로그든 콘솔이 들고 있는 사본은 에디터 몫이다.
        SW_MEMORY_SCOPE( Editor );
        std::scoped_lock<mutex> lock{ _entriesMutex };
        _listEntry.push_back( entry );
        while ( _listEntry.size() > constant::kMaxBuffer2048 )
        {
            _listEntry.pop_front();
        }
        _bHasNewLogs = SW_TRUE;
    }

    void ConsolePanel::unsubscribe()
    {
        if ( _logListenerHandle.isValid() )
        {
            Logger::removeGlobalListener( _logListenerHandle );
            _logListenerHandle = {};
        }
    }
} // namespace sw::editor
