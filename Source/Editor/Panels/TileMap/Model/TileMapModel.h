/**
 * @file TileMapModel.h
 * @brief 타일맵 페인터의 모델 — 편집 중인 맵 · 타일셋, 고른 레이어 · 도구 · 브러시 값, 칠하기 · 채우기 · 스포이트 · 엣지 워프, 읽기 · 쓰기 · 되돌리기 글. ImGui 를 모릅니다.
 * @details 패널(`TileMapPanel`)은 그리기 · 캔버스 보기(확대 · 이동) · 입력 읽기만 하고, 상태와 명령은 모두 이것이 든다 — 인스펙터 `Model/` 과 같은 선이다
 *          (유니티 Tile Palette 창이 Tilemap 데이터와 브러시를 따로 두는 것과 같다). 편집한 명령은 true 를 반환하고, 되돌리기 기록은 패널이 남긴다(`notifyDocumentEdited`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/TileMap/TileMapXML.h"
#include "Engine/TileMap/TileSetAsset.h"

namespace sw::editor
{
    /**
     * @struct TileMapToolInput
     * @brief 캔버스에서 이번 프레임에 읽은 왼쪽 버튼 입력입니다. 패널이 ImGui 에서 읽어 넘깁니다.
     */
    struct TileMapToolInput
    {
        int2 _hoverCell{};          ///< 커서 아래 셀(맵 밖일 수 있다)
        bool _bClicked{ false };    ///< 캔버스 위에서 이번 프레임에 눌렀다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool _bPainting{ false };   ///< 캔버스를 누른 채 끌고 있다(Brush 도구) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool _bButtonDown{ false }; ///< 왼쪽 버튼이 눌려 있다(Rect 도구가 놓은 때를 본다) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /**
     * @class TileMapModel
     * @brief 타일맵 페인터의 상태와 명령입니다. 파일 머리말 참고.
     */
    class TileMapModel
    {
    public:
        /** @brief 현재 브러시가 쓰는 레이어 종류입니다. 플래그 레이어는 `getFlagLayer` 가 고릅니다. */
        enum class PaintLayer : uint8
        {
            Visual = 0,
            Warp,
            Tile,
            Flag,
            Data ///< 맵 데이터가 정한 정수 데이터 레이어(`TileMapXMLData::DataLayer`). `getDataLayerIndex` 가 고른다
        };
        /** @brief 레이어 목록에서 플래그 레이어보다 앞에 오는 항목 수입니다. Visual, Warp, Tile 이 해당합니다. */
        static constexpr int32 kFixedPaintLayerCount = 3;

        /** @brief 작업 공간에서 초점이 간 에셋 경로입니다(콘텐츠 브라우저가 고른 것). 없으면 빈 글입니다. View 가 작업 공간 헤더를 보지 않게 여기서 읽는다. */
        static string getFocusedAssetPath();
        /** @brief @p path(리소스 경로 또는 디스크 경로)에 도구 문서 파일이 있으면 true 입니다(`EditorToolAssetCommands::hasToolDocument` — 읽기와 같은 해석). */
        [[nodiscard]] static bool hasDocumentFile( string_view path );

        /** @brief 캔버스를 눌렀을 때 하는 일입니다. 유니티의 Tile Palette 도구에 해당합니다. */
        enum class PaintTool : uint8
        {
            Brush = 0, ///< 누르고 드래그한 셀마다 칠한다
            Rect,      ///< 드래그한 사각형을 놓을 때 칠한다
            Fill,      ///< 누른 셀과 같은 값으로 연결된 셀을 모두 칠한다
            Picker,    ///< 누른 셀의 값을 칠할 값으로 가져오고 Brush 로 돌아간다
            Line       ///< 끌어 그은 선분을 놓을 때 칠한다(Godot 타일맵의 Line). 번호는 감시 값 `Editor.TileMapTool` 이 읽으므로 끝에 더한다
        };

        /** @brief 엣지 워프 프리셋 하나(N · E · S · W)의 대상 맵과 대상 셀 입력칸입니다. */
        struct EdgeWarpPreset
        {
            fixed_string<constant::buffer::kMax128> _target{};
            int32                                   _tileX{ 1 };
            int32                                   _tileY{ 1 };
        };
        static constexpr int32 kEdgeCount = 4;
        /** @brief 새 맵의 한 변 칸 수입니다. */
        static constexpr int32 kNewMapSide = 8;

        /** @brief 에디터 설정의 기본 맵 · 워프 대상을 채우고 8x8 빈 맵으로 시작합니다. */
        TileMapModel();

        // ------------------------------------------------------------------------------
        // 1) 읽기 · 쓰기 · 되돌리기 글
        // ------------------------------------------------------------------------------
        /** @brief 경로 칸의 문서를 읽습니다. 경로나 파일이 없으면 새 맵(Missing)으로 시작합니다 — 앞 문서의 내용이 새 문서에 남지 않습니다. */
        ToolAssetLoadResult loadDocument();
        /** @brief 타일맵 XML 을 읽어 내용을 채우고 경로 칸을 그 경로로 둡니다. 파일이 없으면 새 맵입니다. 실패 이유는 상태 줄에 남깁니다. */
        ToolAssetLoadResult loadXML( string_view assetRelativePath );
        /** @brief 경로 칸의 Resource 상대 경로로 TileMap XML 을 쓰고 결과를 상태 줄에 남깁니다. 성공하면 true 입니다(dirty 해제 · 되돌리기 기준은 패널이 한다). */
        [[nodiscard]] bool saveDocument();
        /** @brief 되돌리기 기록용 맵 글(이름 칸 포함)입니다. */
        string captureText() const;
        /** @brief 되돌리기 기록 글로 맵을 되돌립니다. 읽지 못하면 빈 맵을 보입니다. */
        void applyText( string_view text );

        // ------------------------------------------------------------------------------
        // 2) 편집 명령 — 바꿨으면 true(패널이 되돌리기 기록을 남긴다)
        // ------------------------------------------------------------------------------
        /** @brief 크기 칸(`editInputWidth` · `editInputHeight`)대로 맵 크기를 바꿉니다. 겹치는 칸의 내용은 남습니다. 범위를 넘으면 상태 줄에 알리고 false 입니다. */
        bool applyInputSize();
        /** @brief 스폰 칸(`editInputSpawnX` · `editInputSpawnY`)을 맵의 스폰으로 둡니다. 맵 밖이면 칸을 되돌리고 상태 줄에 알리며 false 입니다(읽기가 거절하는 맵을 만들지 않는다). */
        bool applyInputSpawn();
        /** @brief 이번 프레임의 캔버스 입력을 현재 도구로 처리합니다. 셀을 하나라도 칠했으면 true 입니다. */
        bool applyToolInput( const TileMapToolInput& input );
        /** @brief 가장자리(@p edge: 0 N, 1 E, 2 S, 3 W) 셀 전부에 그 프리셋의 워프를 찍습니다. 대상이 비었거나 바뀐 칸이 없으면 false 입니다. */
        bool paintEdgeWarp( int32 edge );
        /** @brief 데이터 레이어를 더하고 고릅니다. 빈 이름 · 겹치는 이름은 맵이 거절하고 상태 줄에 알립니다. */
        bool addDataLayer( string_view name, int32 defaultValue );
        /** @brief 고른 데이터 레이어를 지웁니다. 데이터 레이어를 고르지 않았으면 false 입니다. 남은 것이 없으면 플래그 레이어로 돌아갑니다. */
        bool removeSelectedDataLayer();
        /** @brief 타일셋 칸의 경로를 맵의 타일셋으로 둡니다. 경로가 그대로이거나 읽지 못하면 맵을 그대로 두고 false 입니다(읽지 못한 경로를 맵에 저장하지 않는다). */
        bool useTileSetInput();

        // ------------------------------------------------------------------------------
        // 3) 레이어 · 도구 · 브러시
        // ------------------------------------------------------------------------------
        /** @brief 레이어 목록에서 현재 레이어의 순번을 반환합니다. Visual, Warp, Tile, 플래그 레이어, 데이터 레이어 순서입니다. */
        int32 getPaintLayerIndex() const;
        /** @brief 레이어 목록의 순번으로 레이어를 선택합니다. */
        void selectPaintLayer( int32 layerIndex );
        /** @brief 레이어 콤보에 보일 이름 목록입니다(순번은 `getPaintLayerIndex` 와 같다). */
        void collectPaintLayerNames( vector<const utf8*>& outListName ) const;

        PaintLayer    getPaintLayer() const { return _layer; }
        TileFlagLayer getFlagLayer() const { return _flagLayer; }
        int32         getDataLayerIndex() const { return _dataLayerIndex; }
        PaintTool     getTool() const { return _tool; }
        /** @brief 도구를 바꿉니다. 끌던 획은 버립니다 — 남겨 두면 돌아왔을 때 옛 칸에서 이어 칠하거나 미리보기가 남습니다. */
        void setTool( PaintTool tool );
        /** @brief Rect · Line 도구로 누른 채 끌고 있으면 그 시작 셀을 채우고 true 입니다. */
        bool readDragStart( int2& outCell ) const;
        /** @brief 지금 획의 번호입니다. 캔버스를 누를 때마다 늘어납니다 — 패널이 되돌리기 묶음 키로 써서 획 하나가 기록 하나가 됩니다. */
        uint32 getStrokeSerial() const { return _strokeSerial; }

        bool&                                    editErase() { return _bErase; }
        bool                                     isErase() const { return _bErase; }
        int32&                                   editPaintHeight() { return _paintHeight; }
        int32&                                   editAtlasID() { return _atlasID; }
        float32*                                 editTint() { return _arrTint; }
        int32&                                   editPaintDataValue() { return _paintDataValue; }
        int32&                                   editBrushIndex() { return _brushIndex; }
        int32                                    getBrushIndex() const { return _brushIndex; }
        fixed_string<constant::buffer::kMax128>& editWarpTarget() { return _warpTarget; }
        int32&                                   editWarpTileX() { return _warpTx; }
        int32&                                   editWarpTileY() { return _warpTy; }
        EdgeWarpPreset&                          editEdgePreset( int32 edge ) { return _arrEdgePreset[edge]; }

        // ------------------------------------------------------------------------------
        // 4) 입력칸 · 맵 읽기
        // ------------------------------------------------------------------------------
        fixed_string<constant::buffer::kMax256>& editPath() { return _pathBuffer; }
        const utf8*                              getPath() const { return _pathBuffer.c_str(); }
        fixed_string<constant::buffer::kMax128>& editName() { return _nameBuffer; }
        fixed_string<constant::buffer::kMax256>& editTileSetInput() { return _tileSetBuffer; }
        int32&                                   editInputWidth() { return _inputWidth; }
        int32&                                   editInputHeight() { return _inputHeight; }
        int32&                                   editInputSpawnX() { return _inputSpawnX; }
        int32&                                   editInputSpawnY() { return _inputSpawnY; }

        const TileMapXMLData& getMap() const { return _map; }
        const TileSetAsset&   getTileSet() const { return _tileSet; }
        /** @brief Tile 레이어 셀의 브러시 번호 + 1 을 맵에서 다시 옮깁니다(모르는 브러시는 0). 그리기 전에 부릅니다. */
        const vector<uint16>& refreshBrushIndices();
        /** @brief 마지막 `refreshBrushIndices` 의 셀별 브러시 번호 + 1 입니다. */
        const vector<uint16>& getBrushIndices() const { return _listBrushIndex; }
        /** @brief 상태 줄 글입니다. 명령의 결과와 실패 이유가 남습니다. */
        const string& getStatus() const { return _status; }

        /** @brief 칸의 지금 레이어 값을 한 줄로 만듭니다(캔버스 머리의 커서 정보). 맵 밖이면 빈 글입니다. */
        string describeCell( int32 x, int32 y ) const;
        /** @brief 좌표가 맵 범위 안이면 true 입니다. */
        bool isInBounds( int32 x, int32 y ) const;
        /** @brief (x, y)의 1차원 인덱스를 반환합니다. */
        size_t indexOf( int32 x, int32 y ) const;

        /** @brief 현재 선택한 플래그 레이어에서 켜진 셀 수를 반환합니다. 플래그 레이어가 아니면 0 입니다. */
        uint32 countSelectedFlagCells() const;
        /** @brief 모든 데이터 레이어에서 기본값과 다른 칸 수의 합을 반환합니다. 고른 레이어와 상관없습니다. */
        uint32 countDataCells() const;

    private:
        /** @brief 맵 크기를 바꿉니다. 겹치는 칸 · 안에 남는 워프를 지키고 스폰을 맵 안으로 옮깁니다(유니티 · Godot 의 맵 크기 바꾸기와 같다). 범위를 넘거나 크기가 그대로면 false 입니다. */
        bool resize( int32 width, int32 height );
        /** @brief 새 맵(`kNewMapSide`)으로 시작합니다. 경로 칸은 그대로입니다. */
        void           startNewMap();
        TileMapXMLData captureMapData() const;
        void           applyMapData( const TileMapXMLData& data );
        /** @brief 맵이 가리키는 타일셋을 읽습니다. 이미 그 경로를 읽었으면 다시 읽지 않습니다. 읽지 못하면 상태 줄에 알립니다. */
        void refreshTileSet();
        /** @brief 지정한 셀에 현재 레이어를 페인트합니다. 바꾼 것이 없으면 false 입니다. */
        bool paintCell( int32 x, int32 y );
        /** @brief `_listStrokeCell` 의 셀을 모두 칠합니다. 하나라도 칠했으면 true 입니다. */
        bool paintStrokeCells();
        /** @brief 현재 레이어에서 셀마다 비교할 값을 채웁니다. 채우기 도구가 씁니다. */
        void collectCellValues( vector<uint64>& outListValue );
        /** @brief 셀의 값을 칠할 값으로 가져옵니다(스포이트). */
        void pickCell( int32 x, int32 y );
        /** @brief 칸에 워프를 찍고 걸을 수 있게 합니다. 이미 있으면 그 자리에서 고쳐 순서와 짝 id(`_pairID`)를 지킵니다. 바뀐 것이 없으면 false 입니다. */
        bool stampWarp( int32 x, int32 y, const utf8* pTargetMap, int32 targetX, int32 targetY );

    private:
        fixed_string<constant::buffer::kMax256> _pathBuffer;
        fixed_string<constant::buffer::kMax128> _nameBuffer; ///< 맵 이름 입력칸. 저장할 때 맵으로 옮긴다
        fixed_string<constant::buffer::kMax128> _warpTarget;
        EdgeWarpPreset                          _arrEdgePreset[kEdgeCount];
        string                                  _status;
        TileMapXMLData                          _map;            ///< 편집 중인 맵(파일 스키마 그대로). 이름은 `_nameBuffer` 가 보관하고 저장할 때 옮긴다.
        fixed_string<constant::buffer::kMax256> _tileSetBuffer;  ///< Tile 레이어의 타일셋 경로 입력란
        TileSetAsset                            _tileSet;        ///< 읽은 타일셋
        string                                  _loadedTileSet;  ///< `_tileSet` 을 읽은 경로. 읽기에 실패한 경로도 기록해서 같은 실패를 반복하지 않는다
        vector<uint16>                          _listBrushIndex; ///< 그릴 때마다 맵에서 옮겨 둔 셀별 브러시 번호 + 1
        vector<int2>                            _listStrokeCell; ///< 이번 프레임에 칠할 셀(지난 셀에서 이번 셀까지의 선분). 멤버로 두어 프레임마다 할당하지 않는다
        vector<uint64>                          _listCellValue;  ///< 채우기 도구가 비교할 셀 값. 멤버로 두어 누를 때마다 할당하지 않는다
        int32                                   _brushIndex;     ///< 칠할 브러시(타일셋 순번)
        float32                                 _arrTint[3];
        int32                                   _inputWidth;
        int32                                   _inputHeight;
        int32                                   _inputSpawnX;
        int32                                   _inputSpawnY;
        int32                                   _paintHeight;
        int32                                   _atlasID;
        int32                                   _warpTx;
        int32                                   _warpTy;
        int2                                    _lastPaintCell;  ///< 지난 프레임에 칠한 셀. `_bStrokeActive` 일 때만 의미가 있다
        int2                                    _dragStartCell;  ///< Rect · Line 도구로 누른 셀. `_bDragActive` 일 때만 의미가 있다
        int32                                   _dataLayerIndex; ///< 칠할 데이터 레이어(`_map._listDataLayer` 번호). `_layer == Data` 일 때만 의미가 있다
        int32                                   _paintDataValue; ///< 데이터 레이어에 칠할 값(지우기는 그 레이어의 기본값)
        uint32                                  _strokeSerial;   ///< 캔버스를 누른 횟수. 획 하나를 되돌리기 기록 하나로 묶는다
        PaintLayer                              _layer;
        TileFlagLayer                           _flagLayer;
        PaintTool                               _tool;
        bool                                    _bErase;            ///< NOLINT(sw-bool-flag): 참조(`bool&`)로 넘긴다
        uint8                                   _bStrokeActive : 1; ///< 지난 프레임에 칠하고 있었다. 이번 프레임은 `_lastPaintCell` 에서 이어서 칠한다
        uint8                                   _bDragActive   : 1; ///< Rect · Line 도구로 누른 채 드래그하고 있다
        uint8                                   _bAwaitRelease : 1; ///< 스포이트로 집은 버튼을 아직 놓지 않았다 — 놓을 때까지 칠하지 않는다
    };
} // namespace sw::editor
