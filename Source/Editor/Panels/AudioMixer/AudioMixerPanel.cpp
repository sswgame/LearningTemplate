/**
 * @file AudioMixerPanel.cpp
 * @brief 오디오 믹서 창입니다(Window > Audio Mixer). 버스마다 피크 미터, 볼륨 페이더, 음소거, 솔로를 보이고 고치며, 이벤트와 클립을 미리 듣습니다.
 * @details 버스 상태 · 피크 표시 값 · 감시 값 · 이벤트 목록은 `AudioMixerViewModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기와 클립 경로 입력란만 든다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/Color.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/AudioMixer/Model/AudioMixerViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @class AudioMixerPanel
     * @brief 언리얼 Audio Mixer · 유니티 Audio Mixer 창 · Wwise Mixer 에 해당합니다. 버스 하나가 세로 띠 하나입니다.
     * @details 값은 오디오 스레드가 블록마다 게시한 사본(`AudioEngine::getBusStatuses`)에서 읽는다. 고치는 일은 게임 스레드 명령(`setBusVolume` · `setBusMuted` · `setBusSolo`)이고
     *          다음 블록(약 5 ms)에 적용된다. 사용자 볼륨은 옵션 메뉴와 같은 값이라 에디터에서 올린 소리가 게임 옵션에도 그대로 보인다.
     *          미터는 피크(절댓값 최대)를 dB 로 보이고 천천히 내려간다. 믹서 그래프 데이터를 고치면 에셋 핫 리로드(`AssetHotReload` → 엔진 캐시 `AudioData`)가 다시 읽어 이 창이 따라간다.
     */
    class AudioMixerPanel final : public EditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "audiomixer";
        /** @brief 처음 열 때의 크기(글자 수)입니다. */
        static constexpr EditorTextExtent kInitialPanelSize = EditorTextExtent::ofText( 96.0f, 22.0f );

        AudioMixerPanel();

        const utf8*      getPanelTitle() const override { return "Audio Mixer"; }
        void             drawContent() override;
        EditorTextExtent getInitialPanelSize() const override { return kInitialPanelSize; }

        /** @brief 지난 프레임에 게시된 버스 수입니다(감시 값 `Editor.AudioMixerBusCount`). */
        static uint32 getBusCount();
        /** @brief 음소거인 버스 수입니다(감시 값 `Editor.AudioMixerMutedCount`). */
        static uint32 getMutedCount();
        /** @brief 솔로인 버스 수입니다(감시 값 `Editor.AudioMixerSoloCount`). */
        static uint32 getSoloCount();
        /** @brief 이름이 `gv_editorWatchBus` 인 버스의 사용자 볼륨입니다. 없으면 -1 입니다(감시 값 `Editor.AudioMixerBusVolume`). */
        static float32 getWatchedBusVolume();

    private:
        void drawPreviewBar();
        void drawBusStrip( uint32 busIndex );

    private:
        AudioMixerViewModel                     _model;
        fixed_string<constant::buffer::kMax256> _clipPath;
    };

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
                    return SW_THEME_COLOR( "Signal", "Error" );
                if ( decibel > kMeterWarnDb )
                    return SW_THEME_COLOR( "Signal", "Warning" );
                return SW_THEME_COLOR( "Signal", "Success" );
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
                outValue = static_cast<float64>( AudioMixerPanel::getWatchedBusVolume() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AudioMixerPanel::AudioMixerPanel()
        : EditorPanel( false )
        , _model{}
        , _clipPath{}
    {
    }

    uint32 AudioMixerPanel::getBusCount()
    {
        return AudioMixerViewModel::getWatchValues()._busCount;
    }

    uint32 AudioMixerPanel::getMutedCount()
    {
        return AudioMixerViewModel::getWatchValues()._mutedCount;
    }

    uint32 AudioMixerPanel::getSoloCount()
    {
        return AudioMixerViewModel::getWatchValues()._soloCount;
    }

    float32 AudioMixerPanel::getWatchedBusVolume()
    {
        return AudioMixerViewModel::getWatchValues()._watchedBusVolume;
    }

    void AudioMixerPanel::drawPreviewBar()
    {
        const vector<hashed_string>& listEventName = _model.getEventNames();

        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Event" ) );
        ImGui::SameLine();
        EditorChrome::setNextItemWidthColumns( 27.0f );
        const utf8* pPreview = listEventName.empty() ? "(no event library loaded)" : listEventName[_model.getSelectedEvent()].c_str();
        if ( EditorWidgets::beginCombo( "##AudioMixerEvent", pPreview ) )
        {
            for ( uint32 index = 0; index < listEventName.size(); ++index )
            {
                if ( ImGui::Selectable( listEventName[index].c_str(), index == _model.getSelectedEvent() ) )
                    _model.selectEvent( index );
            }
            ImGui::EndCombo();
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Play" ), SW_LOCKEY( "EdButton", "PostEvent" ), "Post Event" ) ) );
        ImGui::BeginDisabled( listEventName.empty() );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Play" ), SW_LOCKEY( "EdButton", "PostEvent" ), "Post Event" ) ) )
            _model.postSelectedEvent();
        EditorSelfTestMarks::record( "audiomixer.postEvent" );
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Clip" ) );
        ImGui::SameLine();
        EditorChrome::setNextItemWidthColumns( 32.0f );
        (void)EditorWidgets::drawAssetPathField( "##AudioMixerClip", _clipPath, "audio/sample.wav", ".wav,.ogg", "audiomixer.clipPath" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Play" ), SW_LOCKEY( "EdButton", "PlayClip" ), "Play Clip" ) ) );
        ImGui::BeginDisabled( _clipPath.empty() );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Play" ), SW_LOCKEY( "EdButton", "PlayClip" ), "Play Clip" ) ) )
            (void)_model.playClip( _clipPath.c_str() ); // 파일이 없으면 false 이고 엔진이 로그를 남긴다
        EditorSelfTestMarks::record( "audiomixer.playClip" );
        ImGui::EndDisabled();
    }

    void AudioMixerPanel::drawBusStrip( uint32 busIndex )
    {
        using Internal               = AudioMixerPanelInternal;
        const AudioBusStatus& status = _model.getBusStatuses()[busIndex];
        const float32         dpi    = EditorThemeUtil::getDPIScale();
        const string          name   = status._name.c_str();

        const float32 heldDb = _model.getHeldPeakDecibel( busIndex );

        ImGui::BeginGroup();
        // 띠는 앞 띠 옆(SameLine)에 놓여 그 줄의 글자 기준선(단추 높이)을 물려받는다 — 첫 띠만 기준선이 없어 6px 위에 그려졌다(uiaudit 26). 모든 띠를 단추 기준선에 맞춘다.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( name.c_str() );
        if ( heldDb < AudioMixerViewModel::kMeterFloorDb )
            ImGui::TextDisabled( "-inf dB" );
        else
            ImGui::Text( "%.1f dB", static_cast<float64>( heldDb ) );

        float32 volume = status._userVolume;
        if ( EditorWidgets::vSliderFloat( SW_WIDGET_ID( "##Volume" ), ImVec2{ 28.0f * dpi, Internal::kStripHeight * dpi }, &volume, 0.0f, 1.0f, "" ) )
            _model.setBusVolume( busIndex, volume );
        EditorSelfTestMarks::recordFormat( "audiomixer.volume.%#", name );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "UserVolumeTheOptionsMenuValueCtrl" ) );

        ImGui::SameLine();
        const ImVec2 meterMin  = ImGui::GetCursorScreenPos();
        const ImVec2 meterSize = ImVec2{ 10.0f * dpi, Internal::kStripHeight * dpi };
        ImGui::Dummy( meterSize );
        ImDrawList*  pDrawList = ImGui::GetWindowDrawList();
        const ImVec2 meterMax{ meterMin.x + meterSize.x, meterMin.y + meterSize.y };
        pDrawList->AddRectFilled( meterMin, meterMax, EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "FrameBg" ) ) );
        const float32 fill = AudioMixerViewModel::computeMeterFill( heldDb );
        if ( fill > 0.0f )
        {
            const ImVec2 fillMin{ meterMin.x, meterMax.y - meterSize.y * fill };
            pDrawList->AddRectFilled( fillMin, meterMax, EditorColorUtil::toImU32( Internal::getMeterColor( heldDb ) ) );
        }
        pDrawList->AddRect( meterMin, meterMax, EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Border" ) ) );

        ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( status._bMuted ? SW_THEME_COLOR( "Signal", "Error" ) : SW_THEME_COLOR( "Base", "ToggleInactive" ) ) );
        if ( ImGui::Button( "M", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 4.0f, 0.0f ) ) ) )
            _model.setBusMuted( busIndex, status._bMuted == false );
        ImGui::PopStyleColor();
        EditorSelfTestMarks::recordFormat( "audiomixer.mute.%#", name );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "Mute" ) );
        ImGui::SameLine();
        ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( status._bSolo ? SW_THEME_COLOR( "Signal", "Warning" ) : SW_THEME_COLOR( "Base", "ToggleInactive" ) ) );
        if ( ImGui::Button( "S", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 4.0f, 0.0f ) ) ) )
            _model.setBusSolo( busIndex, status._bSolo == false );
        ImGui::PopStyleColor();
        EditorSelfTestMarks::recordFormat( "audiomixer.solo.%#", name );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "SoloOnlyThisBusItsAncestorsAnd" ) );
        ImGui::EndGroup();
    }

    void AudioMixerPanel::drawContent()
    {
        const AudioMixerStatus status = _model.refresh(); // 서비스 · 엔진은 ViewModel 이 찾는다 — 이 창은 값과 명령만 본다
        if ( status == AudioMixerStatus::ServiceStopped )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "TheAudioServiceIsNotRunning" ) );
            return;
        }
        if ( status == AudioMixerStatus::NoMixerGraph )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "NoMixerGraphHasBeenPublishedYet" ) );
            return;
        }

        drawPreviewBar();
        ImGui::Separator();
        ImGui::BeginChild( "##AudioMixerStrips", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar );
        const uint32 busCount = static_cast<uint32>( _model.getBusStatuses().size() );
        for ( uint32 busIndex = 0; busIndex < busCount; ++busIndex )
        {
            ImGui::PushID( static_cast<int32>( busIndex ) );
            drawBusStrip( busIndex );
            ImGui::PopID();
            ImGui::SameLine( 0.0f, 18.0f * EditorThemeUtil::getDPIScale() );
        }
        ImGui::EndChild();
    }

    SW_EDITOR_PANEL( AudioMixerPanel, AudioMixerPanel::kPanelID, EditorPanelCategory::Tool, 2100 );
    SW_EDITOR_PANEL_OPEN_COMMAND( AudioMixerOpen, "window.audioMixer", AudioMixerPanel );

    SW_AUTOMATION_WATCH( editorAudioMixerBusCount, "Editor.AudioMixerBusCount", "Mixer buses the Audio Mixer window lists", &AudioMixerPanelInternal::readBusCount );
    SW_AUTOMATION_WATCH( editorAudioMixerMutedCount, "Editor.AudioMixerMutedCount", "Mixer buses that are muted", &AudioMixerPanelInternal::readMutedCount );
    SW_AUTOMATION_WATCH( editorAudioMixerSoloCount, "Editor.AudioMixerSoloCount", "Mixer buses that are solo", &AudioMixerPanelInternal::readSoloCount );
    SW_AUTOMATION_WATCH( editorAudioMixerBusVolume, "Editor.AudioMixerBusVolume", "User volume of the bus named by gv_editorWatchBus (-1 when there is none)",
                         &AudioMixerPanelInternal::readBusVolume );
} // namespace sw::editor
