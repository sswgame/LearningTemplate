#include "pch.h"

#include "Editor/Panels/AudioEvents/AudioEventBrowserPanel.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Localization/LocText.h"

#include <cstdio>
#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct AudioEventBrowserPanelInternal
        {
            static void openPanel()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen( "audioevents", true ); // 패널은 이 파일에 등록되어 있다
            }

            /** @brief 사용률 막대 하나를 그립니다. 상한에 닿으면 경고색입니다. */
            static void drawUsageBar( const utf8* pLabel, uint32 count, uint32 limit, float32 usage, bool bLimitReached )
            {
                ImGui::TextUnformatted( pLabel );
                ImGui::SameLine( 160.0f * EditorThemeUtil::getDpiScale() );
                utf8 arrOverlay[64]{};
                if ( limit == 0 )
                    (void)std::snprintf( arrOverlay, sizeof( arrOverlay ), "%u / -", count );
                else
                    (void)std::snprintf( arrOverlay, sizeof( arrOverlay ), "%u / %u", count, limit );
                ImGui::PushStyleColor( ImGuiCol_PlotHistogram, EditorColorUtil::toImVec4( bLimitReached ? style::kWarn : style::kOk ) );
                ImGui::ProgressBar( usage, ImVec2{ -1.0f, 0.0f }, arrOverlay );
                ImGui::PopStyleColor();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AudioEventBrowserPanel::AudioEventBrowserPanel()
        : IEditorPanel( false )
        , _listEventName{}
        , _listVisibleEvent{}
        , _listFieldRow{}
        , _listInstanceStatus{}
        , _listInstanceRow{}
        , _selectedEvent{}
        , _selectedLibrary{}
        , _filter{}
        , _bOnlySelectedEvent{ false }
    {
    }

    void AudioEventBrowserPanel::drawEventList()
    {
        (void)EditorWidgets::drawSearchField( "##AudioEventFilter", _filter, nullptr, -1.0f );
        AudioEventBrowserUtil::filterEventNames( _listEventName, string_view{ _filter.c_str() }, _listVisibleEvent );
        if ( _listEventName.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioEvents", "NoLibrary" ) );
            return;
        }
        if ( _listVisibleEvent.empty() )
        {
            EditorWidgets::drawNoSearchResultHint( string_view{ _filter.c_str() } );
            return;
        }
        for ( const hashed_string& name : _listVisibleEvent )
        {
            if ( ImGui::Selectable( name.c_str(), name == _selectedEvent ) )
                _selectedEvent = name;
        }
        EditorSelfTestMarks::note( "audioevents.list" );
    }

    void AudioEventBrowserPanel::drawEventDetail( IAudioService& audio )
    {
        AudioEngine& engine = audio.getEngine();
        if ( _selectedEvent.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioEvents", "SelectEvent" ) );
            return;
        }
        AudioEventDesc       eventDesc;
        AudioAttenuationDesc attenuation;
        if ( engine.findEventDesc( _selectedEvent, eventDesc, _selectedLibrary, &attenuation ) == false )
        {
            // 라이브러리가 언로드됐거나 다시 읽혀 이벤트가 사라졌다.
            _selectedEvent   = hashed_string{};
            _selectedLibrary = hashed_string{};
            return;
        }

        ImGui::TextUnformatted( _selectedEvent.c_str() );
        ImGui::TextDisabled( "%s", _selectedLibrary.c_str() );

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, SW_LOCKEY( "EditorAudioEvents", "PostEvent" ) ) ) )
            (void)engine.postEvent( _selectedEvent, 0 ); // 재생 id 는 쓰지 않는다 — 끝나면 저절로 멈춘다
        EditorSelfTestMarks::note( "audioevents.postEvent" );
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioEvents", "OpenDataFile" ) ) )
        {
            // 이벤트 데이터를 고치는 길은 데이터 파일이다(엔진에 라이브러리 쓰기가 없다). 여는 도구가 없으면 경로를 클립보드에 둔다.
            if ( EditorAssetCommands::openPath( _selectedLibrary.view() ) == false )
                ImGui::SetClipboardText( _selectedLibrary.c_str() );
        }
        EditorSelfTestMarks::note( "audioevents.openDataFile" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioEvents", "OpenDataFileTip" ) );

        AudioEventBrowserUtil::makeEventFieldRows( eventDesc, attenuation, _listFieldRow );
        if ( ImGui::BeginTable( "##AudioEventFields", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV ) )
        {
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnField" ), ImGuiTableColumnFlags_WidthFixed, 140.0f * EditorThemeUtil::getDpiScale() );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnValue" ), ImGuiTableColumnFlags_WidthStretch );
            for ( const AudioEventFieldRow& row : _listFieldRow )
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::TextUnformatted( row._pLabel );
                ImGui::TableSetColumnIndex( 1 );
                ImGui::TextUnformatted( row._value.c_str() );
            }
            ImGui::EndTable();
        }
        if ( eventDesc._listClip.empty() == false && ImGui::TreeNode( SW_LOCKEY( "EditorAudioEvents", "Clips" ) ) )
        {
            for ( const AudioClipEntry& clip : eventDesc._listClip )
            {
                ImGui::BulletText( "%s  (w %.2f, %.1f dB)", clip._path.c_str(), static_cast<float64>( clip._weight ), static_cast<float64>( clip._volumeDb ) );
            }
            ImGui::TreePop();
        }
    }

    void AudioEventBrowserPanel::drawPlayingInstances()
    {
        (void)ImGui::Checkbox( SW_LOCKEY( "EditorAudioEvents", "OnlySelected" ), &_bOnlySelectedEvent );
        const hashed_string eventFilter = _bOnlySelectedEvent ? _selectedEvent : hashed_string{};
        AudioEventBrowserUtil::makeInstanceRows( _listInstanceStatus, eventFilter, _listInstanceRow );
        if ( _listInstanceRow.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioEvents", "NothingPlaying" ) );
            return;
        }
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
        if ( ImGui::BeginTable( "##AudioEventInstances", 5, flags ) )
        {
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnEvent" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnBus" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnElapsed" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnRemaining" ) );
            ImGui::TableSetupColumn( SW_LOCKEY( "EditorAudioEvents", "ColumnVoices" ) );
            ImGui::TableHeadersRow();
            for ( const AudioInstanceStatus& row : _listInstanceRow )
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                if ( row._bStopping )
                    ImGui::TextDisabled( "%s", row._event.c_str() );
                else
                    ImGui::TextUnformatted( row._event.c_str() );
                ImGui::TableSetColumnIndex( 1 );
                ImGui::TextUnformatted( row._bus.c_str() );
                ImGui::TableSetColumnIndex( 2 );
                ImGui::Text( "%.2f s", static_cast<float64>( row._elapsedSeconds ) );
                ImGui::TableSetColumnIndex( 3 );
                ImGui::TextUnformatted( AudioEventBrowserUtil::formatRemainingSeconds( row._remainingSeconds ).c_str() );
                ImGui::TableSetColumnIndex( 4 );
                ImGui::Text( "%u (%u virtual)", row._voiceCount, row._virtualVoiceCount );
            }
            ImGui::EndTable();
        }
    }

    void AudioEventBrowserPanel::drawVoiceProfiler( const AudioEngineStats& stats )
    {
        using Internal                  = AudioEventBrowserPanelInternal;
        const AudioVoiceProfile profile = AudioEventBrowserUtil::makeVoiceProfile( stats );
        Internal::drawUsageBar( SW_LOCKEY( "EditorAudioEvents", "RealVoices" ), profile._realVoiceCount, profile._maxRealVoiceCount, profile._realUsage, profile._bRealLimitReached );
        Internal::drawUsageBar( SW_LOCKEY( "EditorAudioEvents", "AllVoices" ), profile._voiceCount, profile._maxVoiceCount, profile._voiceUsage, profile._bVoiceLimitReached );
        ImGui::Text( "%s: %u", SW_LOCKEY( "EditorAudioEvents", "VirtualVoices" ), profile._virtualVoiceCount );
        ImGui::Text( "%s: %u", SW_LOCKEY( "EditorAudioEvents", "PeakRealVoices" ), profile._peakRealVoiceCount );
        ImGui::Text( "%s: %u", SW_LOCKEY( "EditorAudioEvents", "Instances" ), profile._instanceCount );
        ImGui::Text( "%s: %llu / %llu", SW_LOCKEY( "EditorAudioEvents", "PlayedDropped" ), static_cast<uint64>( profile._playedEventCount ),
                     static_cast<uint64>( profile._droppedEventCount ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioEvents", "PlayedDroppedTip" ) );
    }

    void AudioEventBrowserPanel::drawContent()
    {
        IAudioService* pAudio = editor::findSubsystem<IAudioService>();
        if ( pAudio == nullptr || pAudio->isInitialized() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioEvents", "NoService" ) );
            return;
        }
        AudioEngine& engine = pAudio->getEngine();
        engine.collectEventNames( _listEventName );
        engine.getInstanceStatuses( _listInstanceStatus );
        const AudioEngineStats stats = engine.getStats();

        const float32 dpi = EditorThemeUtil::getDpiScale();
        ImGui::BeginChild( "##AudioEventList", ImVec2{ 220.0f * dpi, 0.0f }, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX );
        drawEventList();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild( "##AudioEventRight", ImVec2{ 0.0f, 0.0f } );
        if ( ImGui::BeginTabBar( "##AudioEventTabs" ) )
        {
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EditorAudioEvents", "TabProperties" ) ) )
            {
                drawEventDetail( *pAudio );
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EditorAudioEvents", "TabPlaying" ) ) )
            {
                drawPlayingInstances();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EditorAudioEvents", "TabVoices" ) ) )
            {
                drawVoiceProfiler( stats );
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }

    SW_EDITOR_PANEL( AudioEventBrowserPanel, "audioevents", EditorPanelCategory::Tool, 2101 );
    SW_EDITOR_COMMAND( AudioEventBrowserOpen, "window.audioEvents", 2231, "Audio Events", editoricon::kSettings, "Audio",
                       "오디오 이벤트 창을 엽니다(이벤트 속성 · 재생 중 인스턴스 · 보이스 프로파일러)", "Open the audio event browser", {}, &AudioEventBrowserPanelInternal::openPanel, nullptr,
                       "MainMenu/Window" );
} // namespace sw::editor
