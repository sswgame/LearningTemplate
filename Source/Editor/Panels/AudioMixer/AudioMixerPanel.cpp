#include "pch.h"

#include "Editor/Panels/AudioMixer/AudioMixerPanel.h"

#include "Core/Common/Defines.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorTextExtentImGui.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Automation/Watch/AutomationWatch.h"

#include <imgui.h>

namespace sw::editor
{
    /** @brief 프로브 `Editor.AudioMixerBusVolume` 이 볼 버스 이름입니다. 시나리오가 `<Variable>` 로 정합니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchBus, "master", "감시 값 Editor.AudioMixerBusVolume 이 볼 믹서 버스 이름 (시나리오용)" );

    namespace
    {
        struct AudioMixerPanelInternal
        {
            /** @brief 미터가 보이는 아래쪽 끝(dB)입니다. 이보다 작은 소리는 빈 칸으로 그린다. */
            static constexpr float32 kMeterFloorDb = -60.0f;
            /** @brief 미터가 이 dB 보다 크면 노랑, kClipDb 보다 크면 빨강으로 그립니다. */
            static constexpr float32 kMeterWarnDb = -18.0f;
            static constexpr float32 kClipDb      = -3.0f;
            /** @brief 프레임마다 피크 표시 값에 곱하는 감쇠입니다(60 fps 에서 약 0.3 초에 반으로). */
            static constexpr float32 kPeakDecayPerFrame = 0.96f;
            static constexpr float32 kStripHeight       = 150.0f;

            struct ProbeState
            {
                uint32  _busCount{ 0 };
                uint32  _mutedCount{ 0 };
                uint32  _soloCount{ 0 };
                float32 _probedBusVolume{ -1.0f };
            };

            /** @brief 감시 값이 읽는 지난 프레임의 값입니다. 에디터 범위의 상태 저장소가 소유한다. */
            static ProbeState& getProbeState() { return getEditorState<ProbeState>(); }

            /** @brief 선형 피크를 dB 로 바꿉니다. 0 이면 바닥 값입니다. */
            static float32 toDecibel( float32 linear )
            {
                return linear > 0.000001f ? 20.0f * ::log10f( linear ) : kMeterFloorDb - 1.0f;
            }

            static Color getMeterColor( float32 decibel )
            {
                if ( decibel > kClipDb )
                    return EditorThemeUtil::getErrorColor();
                if ( decibel > kMeterWarnDb )
                    return EditorThemeUtil::getWarningColor();
                return EditorThemeUtil::getSuccessColor();
            }

            static void openPanel()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<AudioMixerPanel>( true ); // 패널은 이 파일에 등록되어 있다
            }

            [[nodiscard]] static bool readBusCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( AudioMixerPanel::getBusCount() );
                return true;
            }

            [[nodiscard]] static bool readMutedCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( AudioMixerPanel::getMutedCount() );
                return true;
            }

            [[nodiscard]] static bool readSoloCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( AudioMixerPanel::getSoloCount() );
                return true;
            }

            [[nodiscard]] static bool readBusVolume( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( AudioMixerPanel::getProbedBusVolume() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AudioMixerPanel::AudioMixerPanel()
        : IEditorPanel( false )
        , _listBusStatus{}
        , _listHeldPeak{}
        , _listEventName{}
        , _pEventNameEngine{ nullptr }
        , _eventNameGeneration{ 0 }
        , _selectedEvent{ 0 }
        , _clipPath{}
    {
    }

    uint32 AudioMixerPanel::getBusCount()
    {
        return AudioMixerPanelInternal::getProbeState()._busCount;
    }

    uint32 AudioMixerPanel::getMutedCount()
    {
        return AudioMixerPanelInternal::getProbeState()._mutedCount;
    }

    uint32 AudioMixerPanel::getSoloCount()
    {
        return AudioMixerPanelInternal::getProbeState()._soloCount;
    }

    float32 AudioMixerPanel::getProbedBusVolume()
    {
        return AudioMixerPanelInternal::getProbeState()._probedBusVolume;
    }

    void AudioMixerPanel::drawPreviewBar( IAudioService& audio )
    {
        AudioEngine& engine          = audio.getEngine();
        const bool   bEventNameStale = _pEventNameEngine != &engine || _eventNameGeneration != engine.getEventLibraryGeneration();
        if ( bEventNameStale )
        {
            _pEventNameEngine    = &engine;
            _eventNameGeneration = engine.getEventLibraryGeneration();
            engine.collectEventNames( _listEventName );
            // 라이브러리 로드 순서는 정해져 있지 않으므로 사전순으로 보인다.
            std::sort( _listEventName.begin(), _listEventName.end(), HashedStringLexicalLess{} );
        }
        if ( _selectedEvent >= _listEventName.size() )
            _selectedEvent = 0;

        ImGui::TextUnformatted( "Event" );
        ImGui::SameLine();
        EditorChrome::setNextItemWidthColumns( 27.0f );
        const utf8* pPreview = _listEventName.empty() ? "(no event library loaded)" : _listEventName[_selectedEvent].c_str();
        if ( ImGui::BeginCombo( "##AudioMixerEvent", pPreview ) )
        {
            for ( uint32 index = 0; index < _listEventName.size(); ++index )
            {
                if ( ImGui::Selectable( _listEventName[index].c_str(), index == _selectedEvent ) )
                    _selectedEvent = index;
            }
            ImGui::EndCombo();
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Post Event" ) ) );
        ImGui::BeginDisabled( _listEventName.empty() );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Post Event" ) ) )
            (void)engine.postEvent( _listEventName[_selectedEvent], 0 ); // 재생 id 는 쓰지 않는다 — 끝나면 저절로 멈춘다
        EditorSelfTestMarks::note( "audiomixer.postEvent" );
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextUnformatted( "Clip" );
        ImGui::SameLine();
        EditorChrome::setNextItemWidthColumns( 32.0f );
        (void)ImGui::InputTextWithHint( "##AudioMixerClip", "audio/sample.wav", _clipPath.data(), _clipPath.capacity() );
        EditorSelfTestMarks::note( "audiomixer.clipPath" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Play Clip" ) ) );
        ImGui::BeginDisabled( _clipPath.empty() );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Play Clip" ) ) )
            (void)audio.play( AssetID{ _clipPath.c_str() } ); // 파일이 없으면 false 이고 엔진이 로그를 남긴다
        EditorSelfTestMarks::note( "audiomixer.playClip" );
        ImGui::EndDisabled();
    }

    void AudioMixerPanel::drawBusStrip( IAudioService& audio, uint32 busIndex )
    {
        using Internal               = AudioMixerPanelInternal;
        const AudioBusStatus& status = _listBusStatus[busIndex];
        AudioEngine&          engine = audio.getEngine();
        const float32         dpi    = EditorThemeUtil::getDPIScale();
        const string          name   = status._name.c_str();

        float32& held        = _listHeldPeak[busIndex];
        held                 = MathUtil::max( status._peak, held * Internal::kPeakDecayPerFrame );
        const float32 heldDb = Internal::toDecibel( held );

        ImGui::BeginGroup();
        ImGui::TextUnformatted( name.c_str() );
        if ( heldDb < Internal::kMeterFloorDb )
            ImGui::TextDisabled( "-inf dB" );
        else
            ImGui::Text( "%.1f dB", static_cast<float64>( heldDb ) );

        float32 volume = status._userVolume;
        if ( ImGui::VSliderFloat( "##Volume", ImVec2{ 28.0f * dpi, Internal::kStripHeight * dpi }, &volume, 0.0f, 1.0f, "" ) )
            audio.setBusVolume( status._name, volume );
        EditorSelfTestMarks::note( ( "audiomixer.volume." + name ).c_str() );
        EditorWidgets::drawTooltip( "User volume (the options-menu value) - Ctrl+click types a number" );

        ImGui::SameLine();
        const ImVec2 meterMin  = ImGui::GetCursorScreenPos();
        const ImVec2 meterSize = ImVec2{ 10.0f * dpi, Internal::kStripHeight * dpi };
        ImGui::Dummy( meterSize );
        ImDrawList*  pDrawList = ImGui::GetWindowDrawList();
        const ImVec2 meterMax{ meterMin.x + meterSize.x, meterMin.y + meterSize.y };
        pDrawList->AddRectFilled( meterMin, meterMax, EditorColorUtil::toImU32( Color{ 0.10f, 0.10f, 0.12f, 1.0f } ) );
        const float32 fill = MathUtil::clamp( ( heldDb - Internal::kMeterFloorDb ) / -Internal::kMeterFloorDb, 0.0f, 1.0f );
        if ( fill > 0.0f )
        {
            const ImVec2 fillMin{ meterMin.x, meterMax.y - meterSize.y * fill };
            pDrawList->AddRectFilled( fillMin, meterMax, EditorColorUtil::toImU32( Internal::getMeterColor( heldDb ) ) );
        }
        pDrawList->AddRect( meterMin, meterMax, EditorColorUtil::toImU32( Color{ 0.35f, 0.35f, 0.40f, 1.0f } ) );

        ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( status._bMuted ? EditorThemeUtil::getErrorColor() : EditorThemeUtil::getToggleInactiveColor() ) );
        if ( ImGui::Button( "M", measureExtent( EditorTextExtent::ofText( 4.0f, 0.0f ) ) ) )
            engine.setBusMuted( status._name, status._bMuted == false );
        ImGui::PopStyleColor();
        EditorSelfTestMarks::note( ( "audiomixer.mute." + name ).c_str() );
        EditorWidgets::drawTooltip( "Mute" );
        ImGui::SameLine();
        ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( status._bSolo ? EditorThemeUtil::getWarningColor() : EditorThemeUtil::getToggleInactiveColor() ) );
        if ( ImGui::Button( "S", measureExtent( EditorTextExtent::ofText( 4.0f, 0.0f ) ) ) )
            engine.setBusSolo( status._name, status._bSolo == false );
        ImGui::PopStyleColor();
        EditorSelfTestMarks::note( ( "audiomixer.solo." + name ).c_str() );
        EditorWidgets::drawTooltip( "Solo - only this bus, its ancestors and its descendants are heard" );
        ImGui::EndGroup();
    }

    void AudioMixerPanel::drawContent()
    {
        using Internal              = AudioMixerPanelInternal;
        Internal::ProbeState& probe = Internal::getProbeState();
        IAudioService&        audio = editor::getSubsystem<IAudioService>(); // 전용 서버도 널 서비스가 있어 늘 있다
        if ( audio.isInitialized() == false )
        {
            EditorWidgets::drawEmptyHint( "The audio service is not running" );
            return;
        }
        audio.getEngine().getBusStatuses( _listBusStatus );
        if ( _listBusStatus.empty() )
        {
            EditorWidgets::drawEmptyHint( "No mixer graph has been published yet" );
            return;
        }
        if ( _listHeldPeak.size() != _listBusStatus.size() )
            _listHeldPeak.assign( _listBusStatus.size(), 0.0f );

        probe._busCount        = static_cast<uint32>( _listBusStatus.size() );
        probe._mutedCount      = 0;
        probe._soloCount       = 0;
        probe._probedBusVolume = -1.0f;
        for ( const AudioBusStatus& status : _listBusStatus )
        {
            probe._mutedCount += status._bMuted ? 1u : 0u;
            probe._soloCount += status._bSolo ? 1u : 0u;
            if ( status._name == hashed_string( gv_editorWatchBus.c_str() ) )
                probe._probedBusVolume = status._userVolume;
        }

        drawPreviewBar( audio );
        ImGui::Separator();
        ImGui::BeginChild( "##AudioMixerStrips", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar );
        for ( uint32 busIndex = 0; busIndex < _listBusStatus.size(); ++busIndex )
        {
            ImGui::PushID( static_cast<int32>( busIndex ) );
            drawBusStrip( audio, busIndex );
            ImGui::PopID();
            ImGui::SameLine( 0.0f, 18.0f * EditorThemeUtil::getDPIScale() );
        }
        ImGui::EndChild();
    }

    SW_EDITOR_PANEL( AudioMixerPanel, AudioMixerPanel::kPanelID, EditorPanelCategory::Tool, 2100 );
    SW_EDITOR_COMMAND( AudioMixerOpen, "window.audioMixer", 2230, "Audio Mixer", editoricon::kSettings, "Audio", "오디오 믹서 창을 엽니다(버스 미터 · 음소거 · 솔로 · 볼륨)", "Open the audio mixer", nullptr, &AudioMixerPanelInternal::openPanel, nullptr, NoScene, "MainMenu/Window", None, 0 );

    SW_AUTOMATION_WATCH( editorAudioMixerBusCount, "Editor.AudioMixerBusCount", "Mixer buses the Audio Mixer window lists", &AudioMixerPanelInternal::readBusCount );
    SW_AUTOMATION_WATCH( editorAudioMixerMutedCount, "Editor.AudioMixerMutedCount", "Mixer buses that are muted", &AudioMixerPanelInternal::readMutedCount );
    SW_AUTOMATION_WATCH( editorAudioMixerSoloCount, "Editor.AudioMixerSoloCount", "Mixer buses that are solo", &AudioMixerPanelInternal::readSoloCount );
    SW_AUTOMATION_WATCH( editorAudioMixerBusVolume, "Editor.AudioMixerBusVolume", "User volume of the bus named by gv_editorWatchBus (-1 when there is none)",
                         &AudioMixerPanelInternal::readBusVolume );
} // namespace sw::editor
