/**
 * @file AudioGraphPanel.h
 * @brief 소리 그래프 창입니다(Window > Audio Graph). `*.audiograph.xml` 하나를 읽어 캔버스(노드 · 연결)와 속성 그리드로 고치고, 검사를 통과하면 저장하며, 미리 들어 봅니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Widgets/EditorNodeGraph.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"

#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    struct EditorGraphPinInfo;

    /**
     * @class AudioGraphPanel
     * @brief 언리얼 MetaSounds 편집기의 자리입니다. 왼쪽 캔버스가 노드를 놓고 잇고(오른쪽 클릭으로 노드 넣기, Delete 로 지우기), 오른쪽이 그래프 설정과 고른 노드의 값을 고칩니다.
     * @details 캔버스와 그리드는 같은 사본(`_editGraph`)을 고친다. 연결의 기준은 받는 노드의 입력 목록이고 캔버스 링크는 그것을 그린 것이다(`AudioGraphEditUtil`).
     *          노드 위치는 데이터(`_editorPosition`)에 실린다. "저장" 은 검사를 통과할 때만 쓴다(`AudioGraphDef::saveToAsset`). 엔진이 이미 읽은 그래프는 저장한 파일을
     *          에셋 핫 리로드(`IAudioService::reloadDataFile` → `AudioClipCache::reloadClip`)가 다시 굳히고, 재생 중인 소리는 옛 그래프로 끝난다.
     *          "미리 듣기" 는 저장된 파일을 2D 로 한 번 낸다(`AudioEngine::playClip`, 버스 `ui`).
     */
    class AudioGraphPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. */
        static constexpr const utf8* kPanelID = "audiograph";

        AudioGraphPanel();

        const utf8*      getPanelTitle() const override { return "Audio Graph"; }
        bool             isToolPanel() const override { return true; }
        void             drawContent() override;
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 147.0f, 34.0f ); }

    private:
        /** @brief 입력한 경로의 파일을 사본으로 읽습니다. 읽지 못하면 사본을 비웁니다(읽기가 오류를 남긴다). 위치가 없는 파일은 처음 배치를 합니다. */
        void loadGraph();
        /** @brief 저장 · 미리 듣기 · 멈춤 단추입니다. */
        void drawToolbar();
        /** @brief 노드 · 핀 · 링크를 그리고 잇기 · 넣기 · 지우기 · 옮기기를 모델에 반영합니다. */
        void drawCanvas( float32 canvasWidth );
        /** @brief 노드 하나의 몸(이름 · 종류 · 출력 표시)과 핀입니다. */
        void drawCanvasNode( uint32 nodeIndex );
        /** @brief 캔버스에서 지운 링크 · 노드를 모델에서 지웁니다. */
        void applyCanvasDeletions();
        /** @brief 그래프 설정(출력 · 길이)과 고른 노드의 그리드, 처리 순서입니다. */
        void drawInspector();
        /** @brief 캔버스 프레임워크가 묻는 핀 정보입니다. 모르는 핀이면 false 입니다. */
        [[nodiscard]] bool findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo );
        /** @brief 속성 그리드가 사본을 고쳤다는 알림입니다. */
        void onGraphEdited( const PropertyInfo& prop );

    private:
        AudioGraphDef                        _editGraph;         ///< 고치는 중인 그래프 사본(파일에서 읽음)
        EditorNodeGraph                      _nodeGraph;         ///< 캔버스
        EditorPropertyGrid                   _graphGrid;         ///< 그래프 설정(출력 · 길이)의 그리드
        EditorPropertyGrid                   _nodeGrid;          ///< 고른 노드의 그리드
        vector<uint32>                       _listOrder;         ///< 그리는 동안 쓰는 처리 순서 버퍼
        hashed_string                        _editPath;          ///< 사본을 읽은 리소스 경로(저장할 곳). 비면 연 파일이 없다.
        AudioPlayingID                       _previewID;         ///< 미리 듣기의 재생 id 다(0 = 없음).
        int32                                _selectedNode;      ///< 고른 노드 번호(-1 = 없음)
        int32                                _pendingPlacedNode; ///< 다음 그리기에서 위치를 정할 새 노드 번호(-1 = 없음)
        fixed_string<constant::kMaxPathSize> _pathInput;         ///< 경로 입력 칸
        bool                                 _bEditDirty;        ///< 고친 뒤 아직 저장하지 않았다.
    };
} // namespace sw::editor
