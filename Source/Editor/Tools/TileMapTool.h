/**
 * @file TileMapTool.h
 * @brief 에디터 측 TileMap XML 페인터 (SWGame 링크 없음, Game TileMap 포맷을 미러)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
	/** @brief Game TileMap XML의 Visual / Walkable / Encounter / Warp / PassThrough 레이어를 페인트합니다 */
	class TileMapTool : public IEditorPanel
	{
	public:
		/** @brief 타일맵 도구를 생성합니다. */
		TileMapTool();

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 온디맨드 도구이므로 기본적으로 닫힌 채 시작합니다. */
		bool isToolPanel() const override { return true; }
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Tile Map Tool"; }
		/** @brief 타일맵 페인트 UI를 그립니다. */
		void drawContent() override;

	private:
		// ------------------------------------------------------------------------------
		// 2) 페인트 레이어 · 타일 데이터
		// ------------------------------------------------------------------------------
		/** @brief 현재 브러시가 쓰는 레이어 */
		enum class PaintLayer : uint8
		{
			Visual = 0,
			Walkable,
			Encounter,
			Warp,
			PassThrough
		};

		/** @brief Visual 레이어 셀 (높이 / 틴트 / 아틀라스) */
		struct EditorTileVisual
		{
			uint8 _height{ 0 };
			uint8 _tintR{ 180 };
			uint8 _tintG{ 200 };
			uint8 _tintB{ 160 };
			uint8 _atlasId{ 0 };
		};

		/** @brief Warp 셀 (좌표 / 대상 맵 / 페어 ID) */
		struct EditorTileWarp
		{
			int32  _tileX{ 0 };
			int32  _tileY{ 0 };
			string _targetMap;
			int32  _targetTileX{ 1 };
			int32  _targetTileY{ 1 };
			string _pairId;
		};

		// ------------------------------------------------------------------------------
		// 3) XML 로드/저장 · 페인트
		// ------------------------------------------------------------------------------
		/** @brief 맵 크기를 변경합니다. */
		void resize( int32 width, int32 height );
		/** @brief Resource 상대 경로의 TileMap XML을 불러옵니다. */
		bool loadXml( string_view assetRelativePath );
		/** @brief Resource 상대 경로로 TileMap XML을 저장합니다. */
		bool saveXml( string_view assetRelativePath ) const;
		/** @brief 지정 셀에 현재 레이어를 페인트합니다. */
		void paintCell( int32 x, int32 y );
		/** @brief 가장자리 워프를 페인트합니다. */
		void paintEdgeWarp( int32 edge );
		/** @brief 좌표가 맵 범위 안인지 여부를 반환합니다. */
		bool inBounds( int32 x, int32 y ) const;
		/** @brief (x, y)의 1차원 인덱스를 반환합니다. */
		size_t indexOf( int32 x, int32 y ) const;

	private:
		utf8	   _arrPathBuffer[constant::kMaxBuffer256];
		utf8	   _arrNameBuffer[constant::kMaxBuffer128];
		utf8	   _arrEdgeTargetN[constant::kMaxBuffer128];
		utf8	   _arrEdgeTargetE[constant::kMaxBuffer128];
		utf8	   _arrEdgeTargetS[constant::kMaxBuffer128];
		utf8	   _arrEdgeTargetW[constant::kMaxBuffer128];
		int32	   _arrEdgeTx[4];
		int32	   _arrEdgeTy[4];
		int32	   _width;
		int32	   _height;
		PaintLayer _layer;
		int32	   _paintHeight;
		int32	   _atlasId;
		float32	   _arrTint[3];
		utf8	   _arrWarpTarget[constant::kMaxBuffer128];
		int32	   _warpTx;
		int32	   _warpTy;
		bool	   _bErase;

		vector<uint8>			 _listWalkable;
		vector<uint8>			 _listEncounter;
		vector<uint8>			 _listPassThrough;
		vector<EditorTileVisual> _listVisual;
		vector<EditorTileWarp>	 _listWarps;
		string					 _status;
	};

} // namespace sw
