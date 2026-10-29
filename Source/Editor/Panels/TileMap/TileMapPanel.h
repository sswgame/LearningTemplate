/**
 * @file TileMapPanel.h
 * @brief 엔진의 TileMapXMLData 를 편집하는 에디터 측 TileMap XML 페인터입니다.
 */
#pragma once
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorThumbnailCache.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Panels/TileMap/Model/TileMapDocument.h"

struct ImDrawList;
struct ImVec2;

namespace sw::editor
{
    /**
     * @brief Game TileMap XML 의 Visual, Warp, Tile, 플래그 레이어를 페인트합니다.
     * @details Tile 레이어는 타일셋(`.tileset.xml`)의 브러시(일반 타일, 규칙 타일)를 칠한다. 셀에는 브러시만 기록하고, 화면에 보일 아틀라스 셀은 규칙이
     *          고른 값을 셀 위에 숫자로 보여 준다. 이웃을 칠하면 그 숫자가 따라 바뀐다(런타임 `TileMapRendererComponent` 와 같은 `resolveVisual`).
     *          상태와 명령은 `TileMapDocument`(Model/, ImGui 없음)가 들고, 이 패널은 그리기 · 캔버스 보기(확대 · 이동) · 입력 읽기와 되돌리기 기록만 한다.
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
        void drawDocumentContent() override;

        /** @brief 경로, 이름, 크기와 읽기와 쓰기 컨트롤을 그립니다. */
        void drawTileMapFileControls();
        /** @brief 레이어 선택과 엣지 워프 프리셋을 그립니다. */
        void               drawLayerControls();
        [[nodiscard]] bool saveDocument() override;
        /** @brief 썸네일(아틀라스 이미지)을 해제합니다. */
        void shutdown( IRHIDevice* pRHIDevice ) override;
        /** @brief 현재 선택한 플래그 레이어에서 켜진 셀 수를 반환합니다. 플래그 레이어가 아니면 0 입니다. 프로브 `Editor.TileMapFlagCells` 가 읽습니다. */
        uint32 countSelectedFlagCells() const { return _document.countSelectedFlagCells(); }
        /** @brief 맵의 데이터 레이어 수를 반환합니다. 프로브 `Editor.TileMapDataLayers` 가 읽습니다. */
        uint32 getDataLayerCount() const { return static_cast<uint32>( _document.getMap()._listDataLayer.size() ); }
        /** @brief 모든 데이터 레이어에서 기본값과 다른 칸 수의 합을 반환합니다. 고른 레이어와 상관없습니다. 프로브 `Editor.TileMapDataCells` 가 읽습니다. */
        uint32 countDataCells() const { return _document.countDataCells(); }
        /** @brief 현재 도구 번호를 반환합니다. 0 은 Brush, 1 은 Rect, 2 는 Fill, 3 은 Picker 입니다. 프로브 `Editor.TileMapTool` 이 읽습니다. */
        uint32 getPaintToolIndex() const { return static_cast<uint32>( _document.getTool() ); }

    protected:
        /** @brief 왼쪽 도구 영역과 오른쪽 캔버스가 나란히 들어갈 크기로 엽니다. */
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 131.0f, 33.0f ); }

    private:
        using PaintLayer = TileMapDocument::PaintLayer;
        using PaintTool  = TileMapDocument::PaintTool;

        string captureDocumentText() const override { return _document.captureText(); }
        void   applyDocumentText( string_view text ) override { _document.applyText( text ); }
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반 클래스가 결과를 보고 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override { return _document.loadDocument(); }
        /** @brief 경로 칸에 맵을 쓰고, 성공하면 dirty 를 내리고 되돌리기 기준을 맞춥니다. */
        [[nodiscard]] bool saveXML();

        /** @brief 데이터 레이어 값 · 새 데이터 레이어 만들기 컨트롤을 그립니다. */
        void drawDataLayerControls();
        /** @brief Tile 레이어 셀을 그립니다. 아틀라스 이미지의 해당 셀을 그리고, 이미지가 없으면 브러시 색과 규칙이 고른 셀 번호를 그립니다. */
        void drawTileLayerCell( ImDrawList* pDrawList, const ImVec2& cellMin, const ImVec2& cellMax, int32 x, int32 y, void* pAtlasTexture ) const;
        /** @brief 도구 버튼 행을 그립니다. Brush, Rect, Fill, Picker, Erase, 보기 초기화 버튼이 있습니다. */
        void drawToolBar();
        /** @brief 칠하는 캔버스를 그립니다. 확대, 이동, 미리보기, 도구 입력을 처리합니다. */
        void drawCanvas();
        /** @brief 휠 확대(커서 아래 셀을 붙든 채)와 가운데 · 오른쪽 끌기 이동을 처리합니다. */
        void updateCanvasView( const ImVec2& canvasMin, bool bHovered, bool bActive );
        /** @brief 현재 층의 셀을 모두 그립니다(타일 층은 아틀라스 이미지, 워프 층은 워프 셀 표시). */
        void drawGridCells( ImDrawList* pDl, const ImVec2& origin, float32 cell );
        /** @brief 커서 아래 셀이나 Rect 도구로 끄는 사각형의 브러시 미리보기를 그립니다(지우기면 빨강). */
        void drawBrushPreview( ImDrawList* pDl, const ImVec2& origin, float32 cell, const int2& hoverCell, bool bHovered );
        /** @brief 캔버스에서 왼쪽 버튼으로 한 입력을 읽어 모델의 도구에 넘기고, 칠했으면 되돌리기 기록을 남깁니다. */
        void applyToolInput( const int2& hoverCell, bool bHovered );
        /** @brief 타일셋 브러시 목록을 그립니다. 아틀라스 이미지와 이름을 보여 줍니다. */
        void drawTilePalette();

    private:
        TileMapDocument                      _document;            ///< 편집 중인 맵과 칠하기 상태 · 명령
        EditorThumbnailCache                 _atlasThumbnail;      ///< 타일셋 아틀라스 이미지
        float2                               _canvasPan;           ///< 캔버스 원점에서 격자 왼쪽 위까지의 거리(px)
        float32                              _canvasZoom;          ///< 셀 크기 배율(휠로 바꾼다)
        fixed_string<constant::kMaxBuffer64> _newDataLayerName;    ///< 새 데이터 레이어 이름 입력란
        int32                                _newDataLayerDefault; ///< 새 데이터 레이어의 기본값 입력란
    };
} // namespace sw::editor
