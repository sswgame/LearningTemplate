/**
 * @file AudioMixerPanel.h
 * @brief 오디오 믹서 창입니다(Window > Audio Mixer). 버스마다 피크 미터, 볼륨 페이더, 음소거, 솔로를 보이고 고치며, 이벤트와 클립을 미리 듣습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"

#include "Engine/Audio/AudioEngineState.h"

namespace sw
{
    class AudioEngine;
    class IAudioService;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AudioMixerPanel
     * @brief 언리얼 Audio Mixer · 유니티 Audio Mixer 창 · Wwise Mixer 에 해당합니다. 버스 하나가 세로 띠 하나입니다.
     * @details 값은 오디오 스레드가 블록마다 게시한 사본(`AudioEngine::getBusStatuses`)에서 읽는다. 고치는 일은 게임 스레드 명령(`setBusVolume` · `setBusMuted` · `setBusSolo`)이고
     *          다음 블록(약 5 ms)에 적용된다. 사용자 볼륨은 옵션 메뉴와 같은 값이라 에디터에서 올린 소리가 게임 옵션에도 그대로 보인다.
     *          미터는 피크(절댓값 최대)를 dB 로 보이고 천천히 내려간다. 믹서 그래프 데이터를 고치면 에셋 핫 리로드(`AssetHotReload` → 엔진 캐시 `AudioData`)가 다시 읽어 이 창이 따라간다.
     */
    class AudioMixerPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "audiomixer";

        AudioMixerPanel();

        const utf8*      getPanelTitle() const override { return "Audio Mixer"; }
        bool             isToolPanel() const override { return true; }
        void             drawContent() override;
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 96.0f, 22.0f ); }

        /** @brief 지난 프레임에 게시된 버스 수입니다(탐침 `Editor.AudioMixerBusCount`). */
        static uint32 getBusCount();
        /** @brief 음소거인 버스 수입니다(탐침 `Editor.AudioMixerMutedCount`). */
        static uint32 getMutedCount();
        /** @brief 솔로인 버스 수입니다(탐침 `Editor.AudioMixerSoloCount`). */
        static uint32 getSoloCount();
        /** @brief 이름이 `gv_editorProbeBus` 인 버스의 사용자 볼륨입니다. 없으면 -1 입니다(탐침 `Editor.AudioMixerBusVolume`). */
        static float32 getProbedBusVolume();

    private:
        void drawPreviewBar( IAudioService& audio );
        void drawBusStrip( IAudioService& audio, uint32 busIndex );

    private:
        vector<AudioBusStatus>                _listBusStatus;
        vector<float32>                       _listHeldPeak;                ///< 버스마다 천천히 내려가는 피크 표시 값(선형)
        vector<hashed_string>                 _listEventName;               ///< 이름 순. 엔진이나 라이브러리 세대가 바뀔 때만 다시 모은다
        const AudioEngine*                    _pEventNameEngine{ nullptr }; ///< `_listEventName` 을 모은 엔진(엔진이 새로 뜨면 세대가 다시 1 부터라 함께 본다)
        uint32                                _eventNameGeneration{ 0 };    ///< `_listEventName` 을 모은 때의 `AudioEngine::getEventLibraryGeneration`
        uint32                                _selectedEvent;
        fixed_string<constant::kMaxBuffer256> _clipPath;
    };
} // namespace sw::editor
