#include "pch.h"

#include "Editor/Panels/ConsolePanel.h"

#include "Core/Concurrency/mutex.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

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
                    default:
                        return ImVec4( 1.0f, 1.0f, 1.0f, 1.0f );
                }
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
    ConsolePanel::ConsolePanel()
        : _listEntry{}
        , _listDrawSnapshot{}
        , _listVisible{}
        , _cachedFilter{}
        , _entriesMutex{}
        , _logListenerHandle{}
        , _filterBuffer{}
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
        _listVisible.clear();
        _listVisible.reserve( _listDrawSnapshot.size() );

        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            const uint8 levelIndex = static_cast<uint8>( entry._level );
            if ( levelIndex >= 4 || _arrLevelEnabled[levelIndex] == false )
                continue;

            if ( filterStr.empty() == false )
            {
                auto contains = [&]( const string& text )
                {
                    if ( text.empty() )
                        return false;
                    return StringUtil::stristr( text.c_str(), filterStr.c_str() ) != nullptr;
                };

                if ( contains( entry._message ) == false && contains( entry._tag ) == false && contains( entry._file ) == false )
                    continue;
            }

            _listVisible.push_back( &entry );
        }

        _cachedFilter = filterStr;
        for ( int32 levelIndex = 0; levelIndex < 4; ++levelIndex )
            _arrCachedLevelEnabled[levelIndex] = _arrLevelEnabled[levelIndex];
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

        size_t errorCount{ 0 };
        size_t warnCount{ 0 };
        size_t infoCount{ 0 };
        size_t traceCount{ 0 };
        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            switch ( entry._level )
            {
                case LogLevel::Error:
                    ++errorCount;
                    break;
                case LogLevel::Warning:
                    ++warnCount;
                    break;
                case LogLevel::Info:
                    ++infoCount;
                    break;
                case LogLevel::Trace:
                    ++traceCount;
                    break;
                case LogLevel::Count:
                default:
                    break;
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

            EditorWidgets::drawSearchField( "##log_filter", _filterBuffer, "Filter (tag / message / file)", -1.0f, false );
            EditorWidgets::drawTooltip( "로그 메시지, 모듈 태그, 파일명으로 필터링하여 검색합니다" );
        }

        EditorChrome::endToolbar();

        ImGui::Separator();

        const string filterStr = StringUtil::trim( _filterBuffer.c_str() );

        // 필터 또는 레벨 설정이 바뀌었거나 새 로그가 들어왔을 때만 재계산
        bool bLevelChanged{ false };
        for ( int32 levelIndex = 0; levelIndex < 4; ++levelIndex )
        {
            if ( _arrCachedLevelEnabled[levelIndex] != _arrLevelEnabled[levelIndex] )
            {
                bLevelChanged = true;
                break;
            }
        }

        const bool bFilterChanged = ( filterStr != _cachedFilter );

        if ( bNewLogs || bFilterChanged || bLevelChanged )
        {
            updateFilteredEntries( filterStr );
        }

        editor::EditorSectionDesc logDesc{};
        logDesc._pId   = "##log_scroll";
        logDesc._kind  = editor::EditorSectionKind::Child;
        logDesc._flags = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::FillRemaining |
                         editor::EditorSectionFlags::HorizontalScrollbar;
        EditorChrome::beginSection( logDesc );

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
                ImGui::TextColored( ImVec4( 0.45f, 0.85f, 0.95f, 1.0f ), " [%s]", entry._tag.c_str() );
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::TextColored( ConsolePanelInternal::colorForLevel( entry._level ), " [%s]", ConsolePanelInternal::levelName( entry._level ) );
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::TextUnformatted( " - " );
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::TextUnformatted( entry._message.c_str() );

                if ( ImGui::IsItemHovered() && entry._file.empty() == false )
                    ImGui::SetTooltip( "%s(%d)", entry._file.c_str(), entry._line );

                if ( ImGui::BeginPopupContextItem( "LogEntryCtx" ) )
                {
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
    }

    void ConsolePanel::shutdown( IRHIDevice* /*rhiDevice*/ )
    {
        unsubscribe();
    }

    void ConsolePanel::onLogWritten( const LogEntry& entry )
    {
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
