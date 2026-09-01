/**
 * @file TileMapPanel.h
 * @brief 에디터 측 TileMap XML 페인터 (Engine TileMapXmlData 편집)
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"

#include "Engine/Utility/Xml/TileMapXml.h"

namespace sw::editor
{
	/** @brief Game TileMap XML의 Visual / Walkable / Encounter / Warp / PassThrough 레이어를 페인트합니다 */
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
		bool saveDocument() override;

	private:
		string		   captureDocumentText() const override;
		void		   applyDocumentText( string_view text ) override;
		TileMapXmlData captureMapData() const;
		void		   applyMapData( const TileMapXmlData& data );

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

		// ------------------------------------------------------------------------------
		// 3) XML 로드/저장 · 페인트
		// ------------------------------------------------------------------------------
		/** @brief 맵 크기를 변경합니다. */
		void resize( int32 width, int32 height );
		/** @brief Resource 상대 경로의 TileMap XML을 불러옵니다. */
		bool loadXml( string_view assetRelativePath );
		/** @brief Resource 상대 경로로 TileMap XML을 저장합니다. */
		bool saveXml( string_view assetRelativePath );
		/** @brief 지정 셀에 현재 레이어를 페인트합니다. */
		void paintCell( int32 x, int32 y );
		/** @brief 가장자리 워프를 페인트합니다. */
		void paintEdgeWarp( int32 edge );
		/** @brief 좌표가 맵 범위 안인지 여부를 반환합니다. */
		bool inBounds( int32 x, int32 y ) const;
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
		string								  _scenePath;
		string								  _role;
		string								  _status;
		vector<uint8>						  _listWalkable;
		vector<uint8>						  _listEncounter;
		vector<uint8>						  _listPassThrough;
		vector<TileMapXmlData::Visual>		  _listVisual;
		vector<TileMapXmlData::Warp>		  _listWarp;
		vector<TileMapXmlData::Encounter>	  _listEncounterEntry;
		int32								  _arrEdgeTx[4];
		int32								  _arrEdgeTy[4];
		float32								  _arrTint[3];
		int32								  _width;
		int32								  _height;
		int32								  _inputWidth;
		int32								  _inputHeight;
		int32								  _paintHeight;
		int32								  _atlasId;
		int32								  _warpTx;
		int32								  _warpTy;
		int32								  _spawnX;
		int32								  _spawnY;
		PaintLayer							  _layer;
		bool								  _bErase;
	};
} // namespace sw::editor
