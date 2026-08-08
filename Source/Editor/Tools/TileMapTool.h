#pragma once
/**
 * @file TileMapTool.h
 * @brief Editor-side TileMap XML painter (no SWGame link ??mirrors Game TileMap format)
 */
#include "Windows/IEditorWindow.h"
#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	/** @brief Paint Visual / Walkable / Encounter / Warp / PassThrough layers on Game TileMap XML */
	class TileMapTool : public IEditorWindow
	{
	public:
		TileMapTool();
		bool isToolWindow() const override { return true; }

		const char* getWindowTitle() const override { return "Tile Map Tool"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		enum class PaintLayer : uint8
		{
			Visual = 0,
			Walkable,
			Encounter,
			Warp,
			PassThrough
		};

		struct EditorTileVisual
		{
			uint8 height  = 0;
			uint8 tintR	  = 180;
			uint8 tintG	  = 200;
			uint8 tintB	  = 160;
			uint8 atlasId = 0;
		};

		struct EditorTileWarp
		{
			int32		tileX		 = 0;
			int32		tileY		 = 0;
			std::string targetMap;
			int32		targetTileX = 1;
			int32		targetTileY = 1;
			std::string pairId;
		};

		void resize( int32 width, int32 height );
		bool loadXml( const std::string& assetRelativePath );
		bool saveXml( const std::string& assetRelativePath ) const;
		void paintCell( int32 x, int32 y );
		void paintEdgeWarp( int32 edge );
		bool inBounds( int32 x, int32 y ) const;
		size_t indexOf( int32 x, int32 y ) const;

		char		_pathBuf[256]{};
		char		_nameBuf[128]{};
		char		_edgeTargetN[128]{};
		char		_edgeTargetE[128]{};
		char		_edgeTargetS[128]{};
		char		_edgeTargetW[128]{};
		int32		_edgeTx[4]{ 1, 1, 1, 1 };
		int32		_edgeTy[4]{ 1, 1, 1, 1 };
		int32		_width	= 8;
		int32		_height = 8;
		PaintLayer	_layer	= PaintLayer::Walkable;
		int32		_paintHeight = 1;
		int32		_atlasId	 = 0;
		float32		_tint[3]{ 180.0f / 255.0f, 200.0f / 255.0f, 160.0f / 255.0f };
		char		_warpTarget[128]{};
		int32		_warpTx = 1;
		int32		_warpTy = 1;
		bool		_bErase = false;

		std::vector<uint8>			 _walkable;
		std::vector<uint8>			 _encounter;
		std::vector<uint8>			 _passThrough;
		std::vector<EditorTileVisual> _visual;
		std::vector<EditorTileWarp>	 _warps;
		std::string					 _status;
	};
} // namespace sw
