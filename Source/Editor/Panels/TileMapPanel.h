/**
 * @file TileMapPanel.h
 * @brief 에디터 측 TileMap XML 페인터 (Engine TileMapXmlData 편집)
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorDocumentPanel.h"

#include "Engine/TileMap/TileMapXml.h"
#include "Engine/TileMap/TileSetAsset.h"

struct ImDrawList;
struct ImVec2;

namespace sw::editor
{
    /**
     * @brief Game TileMap XML의 Visual / Warp / Tile / 플래그 레이어를 페인트합니다.
     * @details Tile 레이어는 타일셋(`.tileset.xml`)의 브러시(그냥 타일 · 규칙 타일)를 칠합니다. 칸에는 브러시만 적고, 보일 아틀라스 칸은 규칙으로
     *          고른 값을 칸 위에 숫자로 보입니다 — 이웃을 칠하면 그 숫자가 따라 바뀝니다(런타임 `TileMapRendererComponent` 와 같은 `resolveVisual`).
     */
    class TileMapPanel : public EditorDocumentPanel
    {
    public:
        /** @brief 타일맵 도구를 생성합니다. */
        TileMapPanel();

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 타일맵 페인트 UI를 그립니다. */
        void drawContent() override;

        /** @brief 경로·이름·크기·읽기/쓰기 컨트롤을 그립니다. */
        void drawTileMapFileControls();
        /** @brief 레이어 선택과 엣지 워프 프리셋을 그립니다. */
        void               drawLayerControls();
        [[nodiscard]] bool saveDocument() override;

    private:
        string         captureDocumentText() const override;
        void           applyDocumentText( string_view text ) override;
        TileMapXmlData captureMapData() const;
        void           applyMapData( const TileMapXmlData& data );

    private:
        // ------------------------------------------------------------------------------
        // 2) 페인트 레이어 · 타일 데이터
        // ------------------------------------------------------------------------------
        /** @brief 현재 브러시가 쓰는 레이어 종류입니다. 플래그 레이어는 `_flagLayer` 가 고릅니다. */
        enum class PaintLayer : uint8
        {
            Visual = 0,
            Warp,
            Tile,
            Flag
        };
        /** @brief 레이어 목록에서 플래그 레이어보다 앞에 오는 항목 수(Visual · Warp · Tile)입니다. */
        static constexpr int32 kFixedPaintLayerCount = 3;

        // ------------------------------------------------------------------------------
        // 3) XML 로드/저장 · 페인트
        // ------------------------------------------------------------------------------
        /** @brief 맵 크기를 변경합니다. */
        void resize( int32 width, int32 height );
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief 타일맵 XML 을 읽어 내용을 채웁니다. */
        ToolAssetLoadResult loadXml( string_view assetRelativePath );
        /** @brief Resource 상대 경로로 TileMap XML을 저장합니다. */
        [[nodiscard]] bool saveXml( string_view assetRelativePath );
        /** @brief 지정 셀에 현재 레이어를 페인트합니다. */
        void paintCell( int32 x, int32 y );
        /** @brief 가장자리 워프를 페인트합니다. */
        void paintEdgeWarp( int32 edge );
        /** @brief 레이어 목록에서 지금 레이어의 순번입니다(Visual · Warp · 플래그 레이어들). */
        int32 getPaintLayerIndex() const;
        /** @brief 레이어 목록의 순번으로 레이어를 고릅니다. */
        void selectPaintLayer( int32 layerIndex );
        /** @brief 맵이 가리키는 타일셋을 읽습니다(이미 그 경로를 읽었으면 그대로). 못 읽으면 상태 줄에 알립니다. */
        void refreshTileSet();
        /** @brief Tile 레이어 칸을 그립니다 — 브러시 색과 규칙이 고른 아틀라스 칸 번호. */
        void drawTileLayerCell( ImDrawList* pDrawList, const ImVec2& cellMin, const ImVec2& cellMax, int32 x, int32 y ) const;
        /** @brief 좌표가 맵 범위 안인지 여부를 반환합니다. */
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
        TileMapXmlData                        _map;            ///< 편집 중인 맵(파일 스키마 그대로). 이름은 `_nameBuffer` 가 들고 저장 때 옮긴다.
        fixed_string<constant::kMaxBuffer256> _tileSetBuffer;  ///< Tile 레이어의 타일셋 경로 칸
        TileSetAsset                          _tileSet;        ///< 읽은 타일셋
        string                                _loadedTileSet;  ///< `_tileSet` 이 어느 경로의 것인지(실패한 경로도 — 같은 실패를 되풀이해 읽지 않는다)
        vector<uint16>                        _listBrushIndex; ///< 그릴 때마다 맵에서 옮긴 칸마다 브러시 번호 + 1
        vector<int2>                          _listStrokeCell; ///< 이번 프레임에 칠할 칸(지난 칸 → 이번 칸 선분). 멤버라 프레임마다 할당하지 않는다
        int32                                 _brushIndex;     ///< 칠할 브러시(타일셋 순번)
        int32                                 _arrEdgeTx[4];
        int32                                 _arrEdgeTy[4];
        float32                               _arrTint[3];
        int32                                 _inputWidth;
        int32                                 _inputHeight;
        int32                                 _paintHeight;
        int32                                 _atlasId;
        int32                                 _warpTx;
        int32                                 _warpTy;
        int2                                  _lastPaintCell; ///< 지난 프레임에 칠한 칸(`_bStrokeActive` 일 때만 뜻이 있다)
        PaintLayer                            _layer;
        TileFlagLayer                         _flagLayer;
        bool                                  _bErase;
        bool                                  _bStrokeActive; ///< 지난 프레임에 칠하고 있었다 — 이번 프레임은 `_lastPaintCell` 에서 잇는다
    };
} // namespace sw::editor
