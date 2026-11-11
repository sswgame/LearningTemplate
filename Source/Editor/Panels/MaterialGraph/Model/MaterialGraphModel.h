/**
 * @file MaterialGraphModel.h
 * @brief 머티리얼 그래프 창의 모델 — 고치는 그래프 사본, 고른 노드, 핀 표, 잇기 · 넣기 · 지우기 · 값 고치기, 컴파일 문제, 미리보기 생성물, 저장. ImGui 를 모릅니다.
 * @details 패널(`MaterialGraphPanel`)은 캔버스와 인스펙터를 그리고 캔버스가 낸 id 를 넘기기만 한다 — 소리 그래프 창(`AudioGraphModel`)과 같은 선이다. 연결의 기준은 받는 노드의 `_listLink` 이고 캔버스 링크는 그것을 그린 것이다.
 *          미리보기는 머티리얼 창의 미리보기 구(`MaterialModel`)를 그대로 쓴다. 저장 전 편집은 작업 도메인(`scratch`)에 생성해 콘텐츠를 건드리지 않는다(`MaterialGraphGeneratedPaths::makePreviewPaths`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Panels/Material/Model/MaterialModel.h"

#include "Engine/Graphics/Material/Dev/MaterialGraph.h"
#include "Engine/Graphics/Material/Dev/MaterialGraphCompileResult.h"

namespace sw
{
    class AbsolutePath;
    class Scene;
} // namespace sw

namespace sw::editor
{
    /** @brief 모델이 사본을 고친 일 하나입니다. 패널이 문서 기반(`notifyDocumentEdited`)에 넘긴다. */
    struct MaterialGraphEdit
    {
        const utf8* _pLabel{ nullptr };       ///< 되돌리기 이름. nullptr 이면 고친 것이 없다
        const utf8* _pCoalesceKey{ nullptr }; ///< 이어진 같은 편집을 한 단계로 합치는 키
    };

    /**
     * @class MaterialGraphModel
     * @brief 머티리얼 그래프 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class MaterialGraphModel
    {
    public:
        /** @brief 노드 하나가 쓰는 핀 id 칸의 수입니다. 핀 번호의 위쪽 절반이 출력 핀이다. */
        static constexpr int32 kPinScale = 256;
        /** @brief 핀 번호에서 출력 핀을 가르는 값입니다. */
        static constexpr int32 kOutputPinBase = 128;
        /** @brief 옮긴 것으로 보는 위치 차이(캔버스 좌표)입니다. */
        static constexpr float32 kMoveEpsilon = 0.5f;
        /** @brief 미리보기를 다시 만드는 간격(프레임)입니다. 끄는 동안 프레임마다 파일을 쓰지 않는다. */
        static constexpr uint32 kPreviewIntervalFrames = 10;

        MaterialGraphModel();

        // ------------------------------------------------------------------------------
        // 1) 읽기 · 쓰기 · 되돌리기
        // ------------------------------------------------------------------------------
        /** @brief @p path 의 파일을 사본으로 읽습니다(`_base` 를 펼친다). 읽지 못하면 false 이고 사본을 비웁니다. */
        [[nodiscard]] bool load( string_view path );
        /** @brief 사본을 되돌리기 글로 바꿉니다. */
        void                 restore( string_view xmlText );
        [[nodiscard]] string captureText() const { return _graph.saveToXMLText(); }
        /** @brief 컴파일을 통과하면(하위 그래프는 검사 없이) @p diskPath 에 씁니다. 통과하지 못하면 쓰지 않고 false 입니다 — 생성물을 만들 수 없는 파일을 남기지 않는다. 생성은 저장 뒤 임포터가 한다. */
        [[nodiscard]] bool saveDocument( const AbsolutePath& diskPath );

        [[nodiscard]] const MaterialGraphDef& getGraph() const { return _graph; }
        MaterialGraphDef&                     editGraph() { return _graph; }
        /** @brief 캔버스가 주석 상자를 고쳤습니다. */
        void markCommentsEdited( const utf8* pLabel, const utf8* pCoalesceKey = nullptr ) { recordEdit( pLabel, pCoalesceKey, false ); }
        /** @brief 지난번 뒤로 고친 일이 있으면 @p outEdit 에 담고 비운 뒤 true 입니다. */
        [[nodiscard]] bool takeEdit( MaterialGraphEdit& outEdit );

        // ------------------------------------------------------------------------------
        // 2) 캔버스 편집
        // ------------------------------------------------------------------------------
        [[nodiscard]] static int32 makeNodeID( uint32 nodeIndex ) { return static_cast<int32>( nodeIndex ) + 1; }
        [[nodiscard]] static int32 makeInputPinID( uint32 nodeIndex, uint32 slot ) { return makeNodeID( nodeIndex ) * kPinScale + static_cast<int32>( slot ); }
        [[nodiscard]] static int32 makeOutputPinID( uint32 nodeIndex, uint32 slot ) { return makeNodeID( nodeIndex ) * kPinScale + kOutputPinBase + static_cast<int32>( slot ); }
        /** @brief 핀 id 를 풉니다. 지금 사본에 없는 핀이면 false 입니다. */
        [[nodiscard]] bool tryDecodePin( int32 pinID, uint32& outNodeIndex, uint32& outSlot, bool& outbInput ) const;
        /** @brief 노드의 입력 핀들입니다(종류 선언, 하위 그래프는 그 파일). 편집이 바뀔 때만 다시 만든다. */
        [[nodiscard]] const vector<MaterialGraphPinInfo>& getInputPins( uint32 nodeIndex ) const { return _listNodePin[nodeIndex]._listInput; }
        [[nodiscard]] const vector<MaterialGraphPinInfo>& getOutputPins( uint32 nodeIndex ) const { return _listNodePin[nodeIndex]._listOutput; }

        /** @brief 출력 핀에서 입력 핀으로 잇습니다. 입력 핀의 앞 링크는 바뀝니다. 고리가 생기면 false 이고 사본은 그대로입니다. */
        bool connectPins( int32 fromPin, int32 toPin );
        /** @brief 입력 핀의 링크를 끊습니다(캔버스의 링크 id 는 입력 핀 id 다). */
        bool disconnectInputPin( int32 inputPin );
        /** @brief 종류 @p kindName 의 노드를 자리 @p canvasPosition 에 넣고 고릅니다. 이름은 종류 이름 + 겹치지 않는 번호입니다. */
        bool addNode( string_view kindName, const float2& canvasPosition );
        /** @brief 캔버스 노드 id 들을 지우고 그 노드를 읽던 링크도 지웁니다. */
        void               removeNodes( const vector<int32>& listNodeID );
        void               beginCanvasFrame();
        [[nodiscard]] bool isPlacingNode( uint32 nodeIndex ) const { return _placingNode >= 0 && static_cast<int32>( nodeIndex ) == _placingNode; }
        void               recordNodePosition( uint32 nodeIndex, const float2& position );
        void               selectNodeByID( int32 nodeID );

        // ------------------------------------------------------------------------------
        // 3) 인스펙터
        // ------------------------------------------------------------------------------
        [[nodiscard]] int32 getSelectedNode() const { return _selectedNode; }
        /** @brief 값 글입니다. 적지 않았으면 @p defaultText 입니다. */
        [[nodiscard]] string readValue( uint32 nodeIndex, string_view name, string_view defaultText ) const;
        /** @brief 노드의 값을 고칩니다. */
        void setValue( uint32 nodeIndex, string_view name, string_view text );
        /** @brief 노드의 에셋을 고칩니다(텍스처 기본값 · 하위 그래프). 핀이 달라질 수 있어 핀 표를 다시 만든다. */
        void setAssetPath( uint32 nodeIndex, string_view path );

        // ------------------------------------------------------------------------------
        // 4) 컴파일 · 미리보기
        // ------------------------------------------------------------------------------
        /** @brief 지금 사본의 컴파일 문제입니다(편집마다 `refreshCompile` 이 다시 센다). */
        [[nodiscard]] const vector<MaterialGraphIssue>& getIssues() const { return _result._listIssue; }
        [[nodiscard]] bool                              isSubgraphDocument() const { return _bSubgraph; }
        /** @brief 편집이 있었으면 다시 컴파일합니다. 그리기마다 부릅니다. */
        void refreshCompile( string_view documentPath );
        /** @brief 새 미리보기 씬에 구를 놓습니다. */
        void populatePreview( Scene& scene );
        /** @brief 컴파일이 통과한 새 내용을 미리보기 자리에 쓰고 구에 겁니다. 문제가 있으면 앞 그림을 그대로 둡니다. 간격(`kPreviewIntervalFrames`)마다 한 번입니다. */
        void refreshPreview( Scene* pScene, string_view documentPath );

    private:
        struct NodePins
        {
            vector<MaterialGraphPinInfo> _listInput;
            vector<MaterialGraphPinInfo> _listOutput;
        };

        void recordEdit( const utf8* pLabel, const utf8* pCoalesceKey, bool bChangesShader );
        void rebuildPins();
        /** @brief 노드 @p fromIndex 의 입력을 거슬러 @p targetIndex 에 닿으면 true 입니다(고리 검사). */
        [[nodiscard]] bool usesNode( uint32 fromIndex, uint32 targetIndex, vector<uint8>& inoutListSeen ) const;

    private:
        MaterialGraphDef           _graph;
        MaterialGraphCompileResult _result;
        MaterialModel              _preview;     ///< 미리보기 구(머티리얼 창과 같은 일) — 사본 칸은 쓰지 않는다
        vector<NodePins>           _listNodePin; ///< 노드 번호와 같은 순서의 핀 표
        string                     _basePath;    ///< 파일의 `_base`(저장이 부모와 다른 칸만 적는다)
        string                     _previewPath; ///< 구에 건 미리보기 머티리얼
        MaterialGraphEdit          _pendingEdit;
        uint32                     _previewFramesLeft;
        int32                      _selectedNode;
        int32                      _pendingPlacedNode;
        int32                      _placingNode;
        uint8                      _bCompileDirty : 1;
        uint8                      _bPreviewDirty : 1;
        uint8                      _bSubgraph     : 1;
    };
} // namespace sw::editor
