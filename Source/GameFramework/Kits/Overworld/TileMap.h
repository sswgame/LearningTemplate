/**
 * @file TileMap.h
 * @brief HD-2D 오버월드 타일맵 (그리드 + TileFlags + 워프 / 조우)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) TileFlags — 보행 / 조우 / 워프 / 솔리드 / 통과
	//    Solid는 Walkable의 역(작성 편의). PassThrough는 낭떠러지 힌트
	// ------------------------------------------------------------------------------
	/** @brief 타일 한 칸의 충돌·이벤트 비트 */
	enum class TileFlags : uint8
	{
		None		= 0,
		Walkable	= 1 << 0,
		Encounter	= 1 << 1,
		Warp		= 1 << 2,
		Solid		= 1 << 3, ///< 차단 (Walkable의 역)
		PassThrough = 1 << 4  ///< 낭떠러지 / 일방 힌트 (보행은 가능)
	};

	// ------------------------------------------------------------------------------
	// 2) 플래그 결합 — enum class라 연산자를 직접 둠
	// ------------------------------------------------------------------------------
	/** @brief 타일 플래그를 OR 결합합니다. */
	inline TileFlags operator|( TileFlags a, TileFlags b ) { return static_cast<TileFlags>( static_cast<uint8>( a ) | static_cast<uint8>( b ) ); }
	/** @brief 타일 플래그를 AND 결합합니다. */
	inline TileFlags operator&( TileFlags a, TileFlags b ) { return static_cast<TileFlags>( static_cast<uint8>( a ) & static_cast<uint8>( b ) ); }
	/** @brief 지정 비트가 켜져 있는지 반환합니다. */
	inline bool hasTileFlag( TileFlags flags, TileFlags bit ) { return ( static_cast<uint8>( flags ) & static_cast<uint8>( bit ) ) != 0; }

	// ------------------------------------------------------------------------------
	// 3) 워프 · 조우 테이블 · HD-2D 비주얼
	// ------------------------------------------------------------------------------
	/** @brief 타일 좌표에서 다른 맵으로 보내는 워프 */
	struct TileWarp
	{
		int32  _tileX{ 0 };
		int32  _tileY{ 0 };
		string _targetMap{};	  ///< 대상 맵 (Resource 상대)
		int32  _targetTileX{ 1 }; ///< 도착 타일 X
		int32  _targetTileY{ 1 }; ///< 도착 타일 Y
		string _pairId{};		  ///< 선택적 WarpDoor 페어 ID
	};

	/** @brief 맵 조우 테이블의 한 행 (가중치 추첨) */
	struct TileEncounterEntry
	{
		string	_speciesId{};
		float32 _weight{ 1.0f }; ///< 상대 가중치
	};

	/** @brief HD-2D 1차: 타일별 가짜 높이 + 틴트 (메시 패스 전까지 소프트웨어/디버그) */
	struct TileVisual
	{
		uint8 _height{ 0 }; ///< 가짜 높이
		uint8 _tintR{ 180 };
		uint8 _tintG{ 200 };
		uint8 _tintB{ 160 };
		uint8 _atlasId{ 0 }; ///< 아틀라스 슬롯
	};

	/** @brief 타일맵 에디터 페인트 레이어 */
	enum class TilePaintLayer : uint8
	{
		Visual = 0,
		Walkable,
		Encounter,
		Warp,
		PassThrough
	};

	// ------------------------------------------------------------------------------
	// 4) TileMap — XML 그리드 + 워프 목록 + 조우 테이블
	// ------------------------------------------------------------------------------
	/** @brief HD-2D 오버월드 타일 그리드 */
	class SW_GF_API TileMap
	{
	public:
		TileMap();

		/** @brief Resource 상대 XML에서 타일맵을 불러옵니다. */
		bool loadFromXml( string_view assetRelativePath );

		/** @brief Resource 상대 XML로 타일맵을 저장합니다. */
		bool saveToXml( string_view assetRelativePath ) const;
		/** @brief 맵 데이터를 비웁니다. */
		void clear();
		/** @brief 맵 크기를 변경합니다. */
		void resize( int32 width, int32 height );

		/** @brief 맵 너비(타일 수)를 반환합니다. */
		int32 getWidth() const { return _width; }
		/** @brief 맵 높이(타일 수)를 반환합니다. */
		int32 getHeight() const { return _height; }
		/** @brief 맵 표시 이름을 반환합니다. */
		const string& getName() const { return _name; }
		/** @brief 맵 표시 이름을 설정합니다. */
		void setName( string_view name ) { _name = name; }
		/** @brief 원본 XML 경로를 반환합니다. */
		const string& getSourcePath() const { return _sourcePath; }
		/** @brief 대응 씬 경로를 반환합니다. */
		const string& getScenePath() const { return _scenePath; }
		/** @brief 대응 씬 경로를 설정합니다. */
		void setScenePath( string_view path ) { _scenePath = path; }
		/** @brief 존 역할 문자열을 반환합니다. */
		const string& getRole() const { return _role; }
		/** @brief 존 역할 문자열을 설정합니다. */
		void setRole( string_view role ) { _role = role; }
		/** @brief 기본 스폰 X를 반환합니다. */
		int32 getSpawnX() const { return _spawnX; }
		/** @brief 기본 스폰 Y를 반환합니다. */
		int32 getSpawnY() const { return _spawnY; }
		/** @brief 기본 스폰 좌표를 설정합니다. */
		void setSpawn( int32 x, int32 y )
		{
			_spawnX = x;
			_spawnY = y;
		}

		/** @brief 맵 조우 테이블을 반환합니다. */
		const vector<TileEncounterEntry>& getEncounters() const { return _encounterEntryList; }
		/** @brief 맵 조우 테이블에서 가중치 추첨합니다. 없으면 빈 문자열. */
		string pickEncounterSpeciesId() const;

		/** @brief 보행 가능 여부를 반환합니다. */
		bool isWalkable( int32 x, int32 y ) const;
		/** @brief 조우 타일 여부를 반환합니다. */
		bool isEncounterTile( int32 x, int32 y ) const;
		/** @brief 통과 타일 여부를 반환합니다. */
		bool isPassThrough( int32 x, int32 y ) const;
		/** @brief 솔리드 타일 여부를 반환합니다. */
		bool isSolid( int32 x, int32 y ) const;
		/** @brief 타일 플래그를 반환합니다. */
		TileFlags getFlags( int32 x, int32 y ) const;
		/** @brief 해당 좌표의 워프를 찾습니다. */
		const TileWarp* findWarp( int32 x, int32 y ) const;
		/** @brief 타일 비주얼을 반환합니다. */
		TileVisual getTileVisual( int32 x, int32 y ) const;

		/** @brief 보행 가능 여부를 설정합니다. */
		void setWalkable( int32 x, int32 y, bool bWalkable );
		/** @brief 조우 타일 여부를 설정합니다. */
		void setEncounter( int32 x, int32 y, bool bEncounter );
		/** @brief 통과 타일 여부를 설정합니다. */
		void setPassThrough( int32 x, int32 y, bool bPassThrough );
		/** @brief 타일 비주얼을 설정합니다. */
		void setTileVisual( int32 x, int32 y, const TileVisual& visual );
		/** @brief 워프를 추가하거나 갱신합니다. */
		void setOrUpdateWarp( const TileWarp& warp );
		/** @brief 워프를 제거합니다. */
		void removeWarp( int32 x, int32 y );
		/** @brief 가장자리 워프 프리셋을 페인트합니다 (0=N,1=E,2=S,3=W). */
		void paintEdgeWarpPreset( int32 edge /*0=N,1=E,2=S,3=W*/, string_view targetMap, int32 tx, int32 ty );

		/** @brief HD-2D 타일 디버그 로그를 남깁니다. */
		void debugLogTileHd2d( int32 x, int32 y ) const;

	private:
		/** @brief 좌표가 맵 범위 안인지 반환합니다. */
		bool inBounds( int32 x, int32 y ) const;
		/** @brief (x, y)의 행 우선 1차원 인덱스를 반환합니다. */
		size_t indexOf( int32 x, int32 y ) const { return static_cast<size_t>( y * _width + x ); }

		string					   _name{};
		string					   _sourcePath{}; ///< loadFromXml 경로
		string					   _scenePath{};  ///< 대응 씬 XML
		string					   _role{};		  ///< ZoneRole 힌트 문자열
		int32					   _width{ 0 };
		int32					   _height{ 0 };
		int32					   _spawnX{ 1 };
		int32					   _spawnY{ 1 };
		vector<uint8>			   _walkableList{};
		vector<uint8>			   _encounterList{};
		vector<uint8>			   _passThroughList{};
		vector<TileVisual>		   _visualList{};
		vector<TileWarp>		   _warpList{};
		vector<TileEncounterEntry> _encounterEntryList{};
	};

} // namespace sw
