/**
 * @file TileMapRendererComponent.h
 * @brief 타일맵(`TileMapXmlData` 의 타일 레이어 + `.tileset.xml`)을 그리고 충돌 · 그림자 외곽선 · 이동 비용을 만드는 컴포넌트입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/SpriteInstanceBatch.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Utility/TileMap/TileGridUtil.h"
#include "Engine/Utility/TileMap/TileSetAsset.h"
#include "Engine/Utility/Xml/TileMapXml.h"

namespace sw
{
    /**
     * @class TileMapRendererComponent
     * @brief 타일 레이어를 칸마다 스프라이트 하나로 그립니다(한 텍스처 · 한 머티리얼이라 한 배치). 규칙 타일은 이웃에 따라 모습을 고르고, 애니메이션 타일은
     *        틱마다 프레임을 넘기며, 단단한 칸은 병합한 사각형 바디(물리)와 외곽선(2D 그림자)이 됩니다.
     * @details 유니티 Tilemap + TilemapRenderer + TilemapCollider2D(+ Composite), Godot TileMapLayer(렌더 · physics · navigation layer)의 자리입니다.
     *
     *          맵의 행 0 이 위이고 칸 (x, y) 의 가운데는 오브젝트 원점에서 ((x + 0.5)·s, −(y + 0.5)·s) 입니다(s = 타일셋의 `tileSize`). 오브젝트의
     *          월드 트랜스폼을 따릅니다. 시작할 때 읽고(`onBeginPlay`) 플레이 중에는 `setTileBrush` 로 칠할 수 있습니다 — 칠한 칸과 이웃 여덟 칸의
     *          규칙을 다시 고르고 바디 · 외곽선을 다시 만듭니다.
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Tile Map Renderer", Tooltip = "Draws a tile layer with rule and animated tiles and builds its colliders" )
    class SW_API TileMapRendererComponent : public Component
    {
    public:
        REFLECT_BODY();
        TileMapRendererComponent();
        virtual ~TileMapRendererComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        /** @brief 애니메이션 타일의 프레임을 넘기고, 오브젝트가 움직였으면 칸 자리를 다시 놓습니다. */
        void onTick( float32 deltaTime ) override;

        /** @brief `_tileMapPath` 의 맵과 그 타일셋을 읽습니다. 읽으면 true 입니다(배치 · 바디는 `rebuild` 가 만듭니다). */
        [[nodiscard]] bool loadTileMap();
        /** @brief 이미 읽은 데이터로 맵을 쓰게 합니다(시험 · 절차 생성). */
        void setTileMapData( const TileMapXmlData& map, const TileSetAsset& tileSet );
        /** @brief 칸마다의 스프라이트 배치를 (다시) 만들고 모든 칸 · 바디 · 외곽선을 맞춥니다. 오브젝트 관리자가 없으면 false 입니다. */
        bool rebuild();
        /** @brief 칸에 브러시를 칠합니다(빈 이름은 지우기). 그 칸과 이웃의 모습 · 바디 · 외곽선을 다시 맞춥니다. 모르는 브러시 · 맵 밖은 false 입니다. */
        bool setTileBrush( int32 x, int32 y, string_view brushName );

        /** @brief 칸 하나가 지금 보이는 아틀라스 칸입니다(빈 칸이면 -1). 시각은 애니메이션 시각입니다. */
        int32 getDisplayedCell( int32 x, int32 y ) const;
        /** @brief 칸 가운데의 월드 자리입니다. */
        float3 computeCellCenter( int32 x, int32 y ) const;
        /** @brief 병합된 충돌 사각형(칸 단위)입니다. */
        const vector<TileRect>& getSolidRects() const { return _listSolidRect; }
        /** @brief 외곽선 토막(칸 꼭짓점 단위)입니다. */
        const vector<TileEdge>& getOutlineEdges() const { return _listOutlineEdge; }
        /** @brief 외곽선 토막을 월드 (x0, y0, x1, y1) 로 옮깁니다. 바깥쪽 방향은 @p outListOutward 에 같은 순서로 넣습니다. */
        void computeWorldOutline( vector<float4>& outListSegment, vector<float2>& outListOutward ) const;
        /** @brief 칸마다 이동 비용(`NavGrid` 와 같은 값)을 만듭니다. */
        void computeNavCosts( vector<uint8>& outListCost ) const;
        /** @brief 물리 바디 수입니다(병합한 사각형 수와 같습니다). */
        uint32 getPhysicsBodyCount() const { return static_cast<uint32>( _listBody.size() ); }
        /** @brief 맵 데이터입니다. */
        const TileMapXmlData& getTileMap() const { return _map; }
        /** @brief 타일셋입니다. */
        const TileSetAsset& getTileSet() const { return _tileSet; }

        void setTileMapPath( string_view path ) { _tileMapPath = string( path ); }
        void setMaterialPath( string_view path ) { _materialPath = string( path ); }
        void setSorting( const hashed_string& layerName, int32 orderInLayer );
        void setGenerateColliders( bool bGenerate ) { _bGenerateColliders = bGenerate; }

    private:
        /** @brief 칸 하나의 스프라이트 항목을 맞춥니다(모습 · 자리 · 보임). */
        void refreshCellEntry( int32 x, int32 y );
        /** @brief 단단한 칸 표시에서 바디 · 외곽선을 다시 만듭니다. */
        void refreshCollision();
        /** @brief 만든 물리 바디를 모두 뺍니다. */
        void removePhysicsBodies();
        /** @brief 오브젝트의 월드 행렬입니다(장면 컴포넌트가 없으면 단위 행렬). */
        float4x4 getOwnerWorld() const;
        size_t   indexOf( int32 x, int32 y ) const { return static_cast<size_t>( y ) * static_cast<size_t>( _map._width ) + static_cast<size_t>( x ); }

        PROPERTY( Category = "Tile Map", DisplayName = "Tile Map", AssetPath, AssetType = "TileMap", Tooltip = "Tile map XML whose tile layer is drawn" )
        string _tileMapPath;
        PROPERTY( Category = "Tile Map", DisplayName = "Material", AssetPath, AssetType = "Material", Tooltip = "Sprite material for the tiles (point filter or lit)" )
        string _materialPath;
        PROPERTY( Category = "Sorting", DisplayName = "Sorting Layer", Tooltip = "Sorting layer name from render2d.xml" )
        hashed_string _sortingLayer;
        PROPERTY( Category = "Sorting", DisplayName = "Order In Layer", Tooltip = "Draw order inside the sorting layer", Min = -32767.0, Max = 32767.0 )
        int32 _orderInLayer;
        PROPERTY( Category = "Physics", DisplayName = "Collider Layer", Tooltip = "Collision layer of the merged solid rectangles" )
        int32 _colliderLayer;
        PROPERTY( Category = "Physics", DisplayName = "Generate Colliders", Tooltip = "Turn solid tiles into merged rectangle bodies" )
        bool _bGenerateColliders;

        TileMapXmlData      _map;              ///< 읽은 맵. 저장하지 않습니다
        TileSetAsset        _tileSet;          ///< 읽은 타일셋
        vector<uint16>      _listBrushIndex;   ///< 칸마다 타일셋 브러시 번호 + 1 (0 = 빈 칸)
        vector<uint32>      _listAnimatedCell; ///< 애니메이션 모습을 보이는 칸 번호
        vector<TileRect>    _listSolidRect;    ///< 병합한 충돌 사각형
        vector<TileEdge>    _listOutlineEdge;  ///< 외곽선 토막
        vector<SlotHandle>  _listBody;         ///< 만든 물리 바디
        SpriteInstanceBatch _batch;            ///< 칸마다 항목 하나
        float4x4            _lastOwnerWorld;   ///< 마지막으로 칸을 놓은 오브젝트 월드 행렬
        float32             _elapsedSeconds;   ///< 애니메이션 시각
        uint8               _bLoaded;          ///< 맵 · 타일셋을 읽었는가
    };
} // namespace sw
