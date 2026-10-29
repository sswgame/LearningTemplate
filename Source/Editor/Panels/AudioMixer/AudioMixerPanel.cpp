#include "pch.h"

#include "Editor/Panels/AudioMixer/AudioMixerPanel.h"

#include "Core/Foundation/Defines.h"
#include "Core/Log/LogOnce.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Automation/Watch/AutomationWatch.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct AudioMixerPanelInternal
        {
            /** @brief 미터가 이 dB 보다 크면 노랑, kClipDb 보다 크면 빨강으로 그립니다. */
            static constexpr float32 kMeterWarnDb = -18.0f;
            static constexpr float32 kClipDb      = -3.0f;
            static constexpr float32 kStripHeight = 150.0f;

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
        , _model{}
        , _clipPath{}
    {
    }

    uint32 AudioMixerPanel::getBusCount()
    {
        return AudioMixerModel::getProbe()._busCount;
    }

    uint32 AudioMixerPanel::getMutedCount()
    {
        return AudioMixerModel::getProbe()._mutedCount;
    }

    uint32 AudioMixerPanel::getSoloCount()
    {
        return AudioMixerModel::getProbe()._soloCount;
    }

    float32 AudioMixerPanel::getProbedBusVolume()
    {
        return AudioMixerModel::getProbe()._probedBusVolume;
    }

    void AudioMixerPanel::drawPreviewBar( IAudioService& audio )
    {
        AudioEngine& engine = audio.getEngine();
        _model.refreshEventNames( engine );
        const vector<hashed_string>& listEventName = _model.getEventNames();

        ImGui::TextUnformatted( "Event" );
        ImGui::SameLine();
        EditorChrome::setNextItemWidthColumns( 27.0f );
        const utf8* pPreview = listEventName.empty() ? "(no event library loaded)" : listEventName[_model.getSelectedEvent()].c_str();
        if ( ImGui::BeginCombo( "##AudioMixerEvent", pPreview ) )
        {
            for ( uint32 index = 0; index < listEventName.size(); ++index )
            {
                if ( ImGui::Selectable( listEventName[index].c_str(), index == _model.getSelectedEvent() ) )
                    _model.selectEvent( index );
            }
            ImGui::EndCombo();
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Post Event" ) ) );
        ImGui::BeginDisabled( listEventName.empty() );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Post Event" ) ) )
            _model.postSelectedEvent( engine );
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
        const AudioBusStatus& status = _model.getBusStatuses()[busIndex];
        AudioEngine&          engine = audio.getEngine();
        const float32         dpi    = EditorThemeUtil::getDPIScale();
        const string          name   = status._name.c_str();

        const float32 heldDb = _model.getHeldPeakDecibel( busIndex );

        ImGui::BeginGroup();
        ImGui::TextUnformatted( name.c_str() );
        if ( heldDb < AudioMixerModel::kMeterFloorDb )
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
        const float32 fill = AudioMixerModel::computeMeterFill( heldDb );
        if ( fill > 0.0f )
        {
            const ImVec2 fillMin{ meterMin.x, meterMax.y - meterSize.y * fill };
            pDrawList->AddRectFilled( fillMin, meterMax, EditorColorUtil::toImU32( Internal::getMeterColor( heldDb ) ) );
        }
        pDrawList->AddRect( meterMin, meterMax, EditorColorUtil::toImU32( Color{ 0.35f, 0.35f, 0.40f, 1.0f } ) );

        ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( status._bMuted ? EditorThemeUtil::getErrorColor() : EditorThemeUtil::getToggleInactiveColor() ) );
        if ( ImGui::Button( "M", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 4.0f, 0.0f ) ) ) )
            engine.setBusMuted( status._name, status._bMuted == false );
        ImGui::PopStyleColor();
        EditorSelfTestMarks::note( ( "audiomixer.mute." + name ).c_str() );
        EditorWidgets::drawTooltip( "Mute" );
        ImGui::SameLine();
        ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( status._bSolo ? EditorThemeUtil::getWarningColor() : EditorThemeUtil::getToggleInactiveColor() ) );
        if ( ImGui::Button( "S", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 4.0f, 0.0f ) ) ) )
            engine.setBusSolo( status._name, status._bSolo == false );
        ImGui::PopStyleColor();
        EditorSelfTestMarks::note( ( "audiomixer.solo." + name ).c_str() );
        EditorWidgets::drawTooltip( "Solo - only this bus, its ancestors and its descendants are heard" );
        ImGui::EndGroup();
    }

    void AudioMixerPanel::drawContent()
    {
        IAudioService& audio = editor::getSubsystem<IAudioService>(); // 전용 서버도 널 서비스가 있어 늘 있다
        if ( audio.isInitialized() == false )
        {
            EditorWidgets::drawEmptyHint( "The audio service is not running" );
            return;
        }
        if ( _model.refreshBuses( audio.getEngine() ) == false )
        {
            EditorWidgets::drawEmptyHint( "No mixer graph has been published yet" );
            return;
        }

        drawPreviewBar( audio );
        ImGui::Separator();
        ImGui::BeginChild( "##AudioMixerStrips", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar );
        const uint32 busCount = static_cast<uint32>( _model.getBusStatuses().size() );
        for ( uint32 busIndex = 0; busIndex < busCount; ++busIndex )
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
