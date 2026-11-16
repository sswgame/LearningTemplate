/**
 * @file AudioGraphModel.h
 * @brief 소리 그래프 창의 모델 — 고치는 그래프 사본, 고른 노드, 다음 그리기에서 위치를 정할 새 노드, 잇기 · 넣기 · 지우기 · 옮기기 · 이름 바꾸기 · 출력 고르기,
 *        저장(검사 통과할 때만), 미리 듣기 재생 id. ImGui 를 모릅니다.
 * @details 패널(`AudioGraphPanel`)은 캔버스(imgui-node-editor) · 속성 그리드를 그리고 캔버스가 낸 id 를 넘기기만 한다 — 계층 · 인스펙터 `Model/` 과 같은 선이다
 *          (언리얼 MetaSounds 의 문서 그래프와 Slate 그래프 패널이 나뉜 것과 같다). 연결 규칙 · id 짓기는 `AudioGraphEditUtil` 이다.
 *          연 문서의 경로 · dirty · 되돌리기는 패널의 문서 기반(`EditorDocumentPanel`)이 든다 — 모델은 고친 일을 `takeEdit` 으로 넘길 뿐이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"

namespace sw
{
    class AbsolutePath;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct AudioGraphEdit
     * @brief 모델이 사본을 고친 일 하나입니다. 패널이 되돌리기 이름으로 문서 기반에 넘긴다(`EditorDocumentPanel::notifyDocumentEdited` — 끌기 한 번은 상호작용 구간이 한 단계로 묶는다).
     */
    struct AudioGraphEdit
    {
        const utf8* _pLabel{ nullptr }; ///< 되돌리기 이름. nullptr 이면 고친 것이 없다
    };

    /**
     * @class AudioGraphModel
     * @brief 소리 그래프 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class AudioGraphModel
    {
    public:
        /** @brief 옮긴 것으로 보는 위치 차이(캔버스 좌표)입니다. 부동소수 반올림을 고침으로 읽지 않는다. */
        static constexpr float32 kMoveEpsilon = 0.5f;

        AudioGraphModel();

        // ------------------------------------------------------------------------------
        // 1) 읽기 · 쓰기 · 되돌리기 · 미리 듣기
        // ------------------------------------------------------------------------------
        /**
         * @brief @p path(리소스 id)의 파일을 사본으로 읽습니다. 고른 노드 · 놓을 노드 · 쌓인 편집을 함께 비운다. 위치가 없는 파일은 처음 배치를 합니다.
         * @return 읽었으면 true 입니다. 읽지 못하면 사본을 비웁니다(읽기가 오류를 남긴다).
         */
        [[nodiscard]] bool load( string_view path );
        /** @brief 사본을 되돌리기 글(`captureText`)로 바꿉니다. 고른 노드 · 놓을 노드를 풉니다. 읽지 못하면 오류를 남기고 빈 그래프로 둡니다(이 모델이 쓴 글이라 결함이다). */
        void restore( string_view xmlText );
        /** @brief 되돌리기 · dirty 판정에 쓰는 사본의 글입니다. */
        string captureText() const { return _editGraph.saveToXMLText(); }
        /** @brief 검사를 통과하면 @p diskPath 에 쓰고 true 입니다. 통과하지 못하면 쓰지 않고 오류를 남긴다(사본은 남아 계속 고칠 수 있다). */
        [[nodiscard]] bool saveDocument( const AbsolutePath& diskPath ) const;
        /** @brief 미리 듣기를 낼 오디오가 준비됐으면 true 입니다(에디터 오디오 서비스 — 창은 오디오를 직접 보지 않는다, MVVM 계약). */
        [[nodiscard]] static bool isPreviewAudioReady();
        /** @brief 저장된 @p path 의 파일을 2D 로 한 번 냅니다(버스 `ui`). 앞의 미리 듣기는 멈춘다. 오디오가 준비되지 않았으면 아무것도 하지 않는다. */
        void playPreview( const hashed_string& path );
        /** @brief 미리 듣기를 멈춥니다. */
        void stopPreview();
        /** @brief 미리 듣기가 끝났으면 재생 id 를 잊습니다(멈춤 단추가 끝난 소리를 가리키지 않는다). 그리기마다 부릅니다. */
        void refreshPreview();
        bool isPreviewPlaying() const { return _previewID != 0; }

        const AudioGraphDef& getGraph() const { return _editGraph; }
        /** @brief 속성 그리드가 바로 고치는 사본입니다. 고치면 `markEdited` 를 부릅니다. */
        AudioGraphDef& editGraph() { return _editGraph; }
        /** @brief 속성 그리드가 사본을 고쳤습니다. */
        void markEdited() { recordEdit( "Edit Audio Graph" ); }
        /** @brief 캔버스가 주석 상자(`editGraph()._listComment`)를 고쳤습니다. */
        void markCommentsEdited( const utf8* pLabel ) { recordEdit( pLabel ); }
        /** @brief 지난번 뒤로 고친 일이 있으면 @p outEdit 에 담고 비운 뒤 true 입니다. 패널이 그리기마다 한 번 부른다. */
        [[nodiscard]] bool takeEdit( AudioGraphEdit& outEdit );

        // ------------------------------------------------------------------------------
        // 2) 캔버스 편집 — 캔버스가 낸 id 를 받는다
        // ------------------------------------------------------------------------------
        /**
         * @brief 캔버스 그리기를 시작한다 — 놓을 새 노드를 이번 프레임의 "놓는 중" 으로 옮깁니다(다음 프레임에는 없다).
         * @details 예전에는 놓을 노드를 노드를 그리면서 지워, 같은 프레임 뒤의 옮김 판정이 그 노드를 건너뛰는 조건이 늘 거짓이었다.
         */
        void beginCanvasFrame();
        /** @brief 이번 프레임에 데이터 위치를 캔버스에 넣을 노드면 true 입니다(새로 넣은 노드). */
        bool isPlacingNode( uint32 nodeIndex ) const { return _placingNode >= 0 && static_cast<int32>( nodeIndex ) >= _placingNode; } ///< 새 노드는 늘 뒤에 붙는다 — 놓을 첫 번호부터 끝까지(복제는 여럿)
        /** @brief 핀 id 를 풉니다. 모르는 핀이면 false 입니다. */
        [[nodiscard]] bool tryDecodePin( int32 pinID, bool& outbInput ) const;
        /** @brief 출력 핀 @p fromPin 에서 입력 핀 @p toPin 으로 잇습니다. 규칙(소스 입력 · 겹침 · 고리)이 거절하면 false 입니다. */
        bool connectPins( int32 fromPin, int32 toPin );
        /** @brief 종류 @p type 의 노드를 캔버스 자리 @p canvasPosition 에 넣고 고릅니다. 다음 캔버스 프레임이 그 자리에 놓는다. */
        bool addNode( AudioGraphNodeKind type, const float2& canvasPosition );
        /**
         * @brief 캔버스 노드 id @p listNodeID 의 노드를 비켜 복제합니다(Ctrl+D · 붙여 넣기 — `AudioGraphEditUtil::duplicateNodes`). 한 편집으로 남기고 마지막 사본을 고릅니다.
         * @return 만든 노드 수입니다.
         */
        uint32 duplicateNodes( const vector<hashed_string>& listName, const float2& offset );
        /** @brief 캔버스 노드 id 를 노드 이름으로 바꿉니다(복사 버퍼는 이름을 둔다 — 지우면 번호가 당겨진다). 모르는 id 는 건너뜁니다. */
        void collectNodeNames( const vector<int32>& listNodeID, vector<hashed_string>& outListName ) const;
        /** @brief 캔버스에서 지운 링크 · 노드를 지웁니다. 번호가 밀리지 않게 큰 id 부터, 링크 먼저 지운다. 지운 노드가 있으면 고름 · 놓을 노드를 푼다. */
        void applyDeletions( vector<int32>& inoutListLinkID, vector<int32>& inoutListNodeID );
        /** @brief 캔버스의 노드 위치 @p position 을 데이터에 싣습니다(놓는 중인 노드는 건너뛴다). */
        void recordNodePosition( uint32 nodeIndex, const float2& position );
        /** @brief 캔버스에서 고른 노드 id 를 고른 노드로 둡니다. */
        void selectNodeByID( int32 nodeID );

        // ------------------------------------------------------------------------------
        // 3) 인스펙터
        // ------------------------------------------------------------------------------
        /** @brief 고른 노드입니다. 없으면 nullptr 입니다(그리드가 바로 고친다). */
        [[nodiscard]] AudioGraphNodeDef* editSelectedNode();
        /** @brief 고른 노드를 그래프 출력으로 둡니다. */
        void setSelectedAsOutput();
        /** @brief 고른 노드 이름이 @p previousName 에서 바뀌었으면 다른 노드의 입력 · 그래프 출력도 따라가게 합니다(이름이 연결의 기준이다). */
        void followSelectedRename( const hashed_string& previousName );
        /** @brief 처리 순서(노드 번호)를 다시 셉니다. 링크가 깨졌으면 false 입니다. */
        [[nodiscard]] bool    refreshProcessingOrder() { return _editGraph.makeProcessingOrder( _listOrder ); }
        const vector<uint32>& getProcessingOrder() const { return _listOrder; }

    private:
        /** @brief 고친 일을 쌓습니다. 한 그리기 안의 여러 편집은 마지막 이름 하나로 넘긴다(되돌리기 한 단계). */
        void recordEdit( const utf8* pLabel ) { _pendingEdit = AudioGraphEdit{ pLabel }; }
        /** @brief 노드 번호로 든 고름 · 놓을 노드를 풉니다(그래프를 통째로 바꾸거나 노드를 지웠을 때). */
        void clearNodeRefs();

    private:
        AudioGraphDef  _editGraph;         ///< 고치는 중인 그래프 사본(파일에서 읽음)
        vector<uint32> _listOrder;         ///< 처리 순서 버퍼
        AudioPlayingID _previewID;         ///< 미리 듣기의 재생 id 다(0 = 없음).
        AudioGraphEdit _pendingEdit;       ///< 패널이 아직 가져가지 않은 편집
        int32          _selectedNode;      ///< 고른 노드 번호(-1 = 없음)
        int32          _pendingPlacedNode; ///< 다음 캔버스 프레임에서 위치를 정할 새 노드 번호(-1 = 없음)
        int32          _placingNode;       ///< 이번 캔버스 프레임에 위치를 정하는 노드 번호(-1 = 없음). `beginCanvasFrame` 만 바꾼다
    };
} // namespace sw::editor
