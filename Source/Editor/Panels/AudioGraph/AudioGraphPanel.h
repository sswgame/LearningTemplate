/**
 * @file AudioGraphPanel.h
 * @brief 소리 그래프 창입니다(Window > Audio Graph). `*.audiograph.xml` 하나를 읽어 노드 · 연결 · 값을 고치고, 검사를 통과하면 저장하며, 미리 들어 봅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"

#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AudioGraphPanel
     * @brief 언리얼 MetaSounds 편집기의 자리입니다. 지금은 노드 목록 편집(속성 그리드)과 처리 순서 보기이고, 캔버스 연결 편집은 다음 단계입니다.
     * @details 파일을 사본(`_editGraph`)으로 읽어 고치고, "저장" 은 검사를 통과할 때만 쓴다(`AudioGraphDef::saveToResource`). 엔진이 이미 읽은 그래프는 저장한 파일을
     *          에셋 핫 리로드(`IAudioService::reloadDataFile` → `AudioClipCache::reloadClip`)가 다시 굳히고, 재생 중인 소리는 옛 그래프로 끝난다.
     *          "미리 듣기" 는 저장된 파일을 2D 로 한 번 낸다(`AudioEngine::playClip`, 버스 `ui`).
     */
    class AudioGraphPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. */
        static constexpr const utf8* kPanelID = "audiograph";

        AudioGraphPanel();

        const utf8* getPanelTitle() const override { return "Audio Graph"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        float2      getInitialPanelSize() const override { return float2{ 720.0f, 560.0f }; }

    private:
        /** @brief 입력한 경로의 파일을 사본으로 읽습니다. 읽지 못하면 사본을 비웁니다(읽기가 오류를 남긴다). */
        void loadGraph();
        /** @brief 처리 순서와 노드마다의 종류 · 입력을 한 줄씩 보입니다. 고리 · 없는 입력이면 경고를 보입니다. */
        void drawProcessingOrder();
        /** @brief 저장 · 미리 듣기 · 멈춤 단추입니다. */
        void drawToolbar();
        /** @brief 속성 그리드가 사본을 고쳤다는 알림입니다. */
        void onGraphEdited( const PropertyInfo& prop );

    private:
        AudioGraphDef                        _editGraph;  ///< 고치는 중인 그래프 사본(파일에서 읽음)
        EditorPropertyGrid                   _grid;       ///< 사본의 속성 그리드
        vector<uint32>                       _listOrder;  ///< 그리는 동안 쓰는 처리 순서 버퍼
        hashed_string                        _editPath;   ///< 사본을 읽은 리소스 경로(저장할 곳). 비면 연 파일이 없다.
        AudioPlayingID                       _previewID;  ///< 미리 듣기의 재생 id 다(0 = 없음).
        fixed_string<constant::kMaxPathSize> _pathInput;  ///< 경로 입력 칸
        bool                                 _bEditDirty; ///< 고친 뒤 아직 저장하지 않았다.
    };
} // namespace sw::editor
