/**
 * @file TileMapPanel.h
 * @brief 엔진의 TileMapXMLData 를 편집하는 에디터 측 TileMap XML 페인터입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorDocumentPanel.h"
#include "Editor/Common/GUI/EditorThumbnailCache.h"

#include "Engine/TileMap/TileMapXML.h"
#include "Engine/TileMap/TileSetAsset.h"

struct ImDrawList;
struct ImVec2;

namespace sw::editor
{
    /**
     * @brief Game TileMap XML 의 Visual, Warp, Tile, 플래그 레이어를 페인트합니다.
     * @details Tile 레이어는 타일셋(`.tileset.xml`)의 브러시(일반 타일, 규칙 타일)를 칠한다. 셀에는 브러시만 기록하고, 화면에 보일 아틀라스 셀은 규칙이
     *          고른 값을 셀 위에 숫자로 보여 준다. 이웃을 칠하면 그 숫자가 따라 바뀐다(런타임 `TileMapRendererComponent` 와 같은 `resolveVisual`).
     */
    class TileMapPanel : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "tile_map";

        /** @brief 타일맵 도구를 생성합니다. */
        TileMapPanel();

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel: 제목과 그리기
        // ------------------------------------------------------------------------------
        /** @brief 타일맵 페인트 UI 를 그립니다. */
        void drawContent() override;

        /** @brief 경로, 이름, 크기와 읽기와 쓰기 컨트롤을 그립니다. */
        void drawTileMapFileControls();
        /** @brief 레이어 선택과 엣지 워프 프리셋을 그립니다. */
        void               drawLayerControls();
        [[nodiscard]] bool saveDocument() override;
        /** @brief 썸네일(아틀라스 이미지)을 해제합니다. */
        void shutdown( IRHIDevice* pRHIDevice ) override;
        /** @brief 현재 선택한 플래그 레이어에서 켜진 셀 수를 반환합니다. 플래그 레이어가 아니면 0 입니다. 프로브 `Editor.TileMapFlagCells` 가 읽습니다. */
        uint32 countSelectedFlagCells() const;
        /** @brief 현재 도구 번호를 반환합니다. 0 은 Brush, 1 은 Rect, 2 는 Fill, 3 은 Picker 입니다. 프로브 `Editor.TileMapTool` 이 읽습니다. */
        uint32 getPaintToolIndex() const { return static_cast<uint32>( _tool ); }

    protected:
        /** @brief 왼쪽 도구 영역과 오른쪽 캔버스가 나란히 들어갈 크기로 엽니다. */
        float2 getInitialPanelSize() const override { return float2{ 980.0f, 620.0f }; }

    private:
        string         captureDocumentText() const override;
        void           applyDocumentText( string_view text ) override;
        TileMapXMLData captureMapData() const;
        void           applyMapData( const TileMapXMLData& data );

    private:
        // ------------------------------------------------------------------------------
        // 2) 페인트 레이어와 타일 데이터
        // ------------------------------------------------------------------------------
        /** @brief 현재 브러시가 쓰는 레이어 종류입니다. 플래그 레이어는 `_flagLayer` 가 고릅니다. */
        enum class PaintLayer : uint8
        {
            Visual = 0,
            Warp,
            Tile,
            Flag
        };
        /** @brief 레이어 목록에서 플래그 레이어보다 앞에 오는 항목 수입니다. Visual, Warp, Tile 이 해당합니다. */
        static constexpr int32 kFixedPaintLayerCount = 3;

        /** @brief 캔버스를 눌렀을 때 하는 일입니다. 유니티의 Tile Palette 도구에 해당합니다. */
        enum class PaintTool : uint8
        {
            Brush = 0, ///< 누르고 드래그한 셀마다 칠한다
            Rect,      ///< 드래그한 사각형을 놓을 때 칠한다
            Fill,      ///< 누른 셀과 같은 값으로 연결된 셀을 모두 칠한다
            Picker     ///< 누른 셀의 값을 칠할 값으로 가져오고 Brush 로 돌아간다
        };

        // ------------------------------------------------------------------------------
        // 3) XML 로드, 저장, 페인트
        // ------------------------------------------------------------------------------
        /** @brief 맵 크기를 바꿉니다. */
        void resize( int32 width, int32 height );
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반 클래스가 결과를 보고 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief 타일맵 XML 을 읽어 내용을 채웁니다. */
        ToolAssetLoadResult loadXML( string_view assetRelativePath );
        /** @brief Resource 상대 경로로 TileMap XML 을 저장합니다. */
        [[nodiscard]] bool saveXML( string_view assetRelativePath );
        /** @brief 지정한 셀에 현재 레이어를 페인트합니다. */
        void paintCell( int32 x, int32 y );
        /** @brief 가장자리 워프를 페인트합니다. */
        void paintEdgeWarp( int32 edge );
        /** @brief 레이어 목록에서 현재 레이어의 순번을 반환합니다. Visual, Warp, 플래그 레이어 순서입니다. */
        int32 getPaintLayerIndex() const;
        /** @brief 레이어 목록의 순번으로 레이어를 선택합니다. */
        void selectPaintLayer( int32 layerIndex );
        /** @brief 맵이 가리키는 타일셋을 읽습니다. 이미 그 경로를 읽었으면 다시 읽지 않습니다. 읽지 못하면 상태 줄에 알립니다. */
        void refreshTileSet();
        /** @brief Tile 레이어 셀을 그립니다. 아틀라스 이미지의 해당 셀을 그리고, 이미지가 없으면 브러시 색과 규칙이 고른 셀 번호를 그립니다. */
        void drawTileLayerCell( ImDrawList* pDrawList, const ImVec2& cellMin, const ImVec2& cellMax, int32 x, int32 y, void* pAtlasTexture ) const;
        /** @brief 도구 버튼 행을 그립니다. Brush, Rect, Fill, Picker, Erase, 보기 초기화 버튼이 있습니다. */
        void drawToolBar();
        /** @brief 칠하는 캔버스를 그립니다. 확대, 이동, 미리보기, 도구 입력을 처리합니다. */
        void drawCanvas();
        /** @brief 캔버스에서 왼쪽 버튼으로 한 입력을 현재 도구로 처리합니다. */
        void applyToolInput( const int2& hoverCell, bool bHovered );
        /** @brief 현재 레이어에서 셀마다 비교할 값을 채웁니다. 채우기 도구가 씁니다. */
        void collectCellValues( vector<uint64>& outListValue );
        /** @brief 셀의 값을 칠할 값으로 가져옵니다(스포이트). */
        void pickCell( int32 x, int32 y );
        /** @brief 타일셋 브러시 목록을 그립니다. 아틀라스 이미지와 이름을 보여 줍니다. */
        void drawTilePalette();
        /** @brief 좌표가 맵 범위 안이면 true 입니다. */
        bool isInBounds( int32 x, int32 y ) const;
        /** @brief (x, y)의 1차원 인덱스를 반환합니다. */
        size_t indexOf( int32 x, int32 y ) const;

    private:
        fixed_string<constant::kMaxBuffer256> _pathBuffer;
        fixed_string<constant::kMaxBuffer128> _nameBuffer;
        fixed_string<constant::kMaxBuffer128> _edgeTargetN;
        fixed_string<constant::kMaxBuffer128> _edgeTargetE;
        fixed_string<constant::kMaxBuffer128> _edgeTargetS;
        fixed_string<constant::kMaxBuffer128> _edgeTargetW;
        fixed_string<constant::kMaxBuffer128> _warpTarget;
        string                                _status;
        TileMapXMLData                        _map;            ///< 편집 중인 맵(파일 스키마 그대로). 이름은 `_nameBuffer` 가 보관하고 저장할 때 옮긴다.
        fixed_string<constant::kMaxBuffer256> _tileSetBuffer;  ///< Tile 레이어의 타일셋 경로 입력란
        TileSetAsset                          _tileSet;        ///< 읽은 타일셋
        string                                _loadedTileSet;  ///< `_tileSet` 을 읽은 경로. 읽기에 실패한 경로도 기록해서 같은 실패를 반복하지 않는다
        vector<uint16>                        _listBrushIndex; ///< 그릴 때마다 맵에서 옮겨 둔 셀별 브러시 번호 + 1
        vector<int2>                          _listStrokeCell; ///< 이번 프레임에 칠할 셀(지난 셀에서 이번 셀까지의 선분). 멤버로 두어 프레임마다 할당하지 않는다
        vector<uint64>                        _listCellValue;  ///< 채우기 도구가 비교할 셀 값. 멤버로 두어 누를 때마다 할당하지 않는다
        EditorThumbnailCache                  _atlasThumbnail; ///< 타일셋 아틀라스 이미지
        float2                                _canvasPan;      ///< 캔버스 원점에서 격자 왼쪽 위까지의 거리(px)
        float32                               _canvasZoom;     ///< 셀 크기 배율(휠로 바꾼다)
        int32                                 _brushIndex;     ///< 칠할 브러시(타일셋 순번)
        int32                                 _arrEdgeTx[4];
        int32                                 _arrEdgeTy[4];
        float32                               _arrTint[3];
        int32                                 _inputWidth;
        int32                                 _inputHeight;
        int32                                 _paintHeight;
        int32                                 _atlasID;
        int32                                 _warpTx;
        int32                                 _warpTy;
        int2                                  _lastPaintCell; ///< 지난 프레임에 칠한 셀. `_bStrokeActive` 일 때만 의미가 있다
        int2                                  _rectStartCell; ///< Rect 도구로 누른 셀. `_bRectActive` 일 때만 의미가 있다
        PaintLayer                            _layer;
        TileFlagLayer                         _flagLayer;
        PaintTool                             _tool;
        bool                                  _bErase;
        bool                                  _bStrokeActive; ///< 지난 프레임에 칠하고 있었다. 이번 프레임은 `_lastPaintCell` 에서 이어서 칠한다
        bool                                  _bRectActive;   ///< Rect 도구로 누른 채 드래그하고 있다
    };
} // namespace sw::editor
