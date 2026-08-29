/**
 * @file TileMapXml.h
 * @brief 타일맵 XML 문서 스키마 (Engine 소유, Editor/GameFramework 공용)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/** @brief TileMap XML 전체 문서 */
	struct TileMapXmlData
	{
		/** @brief 타일 한 칸의 가짜 높이·틴트·아틀라스 */
		struct Visual
		{
			uint8 _height{ 0 };
			uint8 _tintR{ 180 };
			uint8 _tintG{ 200 };
			uint8 _tintB{ 160 };
			uint8 _atlasId{ 0 };
		};

		/** @brief 타일 좌표에서 다른 맵으로 보내는 워프 */
		struct Warp
		{
			int32  _tileX{ 0 };
			int32  _tileY{ 0 };
			string _targetMap{};
			int32  _targetTileX{ 1 };
			int32  _targetTileY{ 1 };
			string _pairId{};
		};

		/** @brief 맵 조우 테이블의 한 행 */
		struct Encounter
		{
			string	_speciesId{};
			float32 _weight{ 1.0f };
		};

		string			  _name{};
		string			  _sourcePath{};
		string			  _scenePath{};
		string			  _role{};
		int32			  _width{ 0 };
		int32			  _height{ 0 };
		int32			  _spawnX{ 1 };
		int32			  _spawnY{ 1 };
		vector<uint8>	  _walkableList{};
		vector<uint8>	  _encounterList{};
		vector<uint8>	  _passThroughList{};
		vector<Visual>	  _visualList{};
		vector<Warp>	  _warpList{};
		vector<Encounter> _encounterEntryList{};

		/** @brief Resource 상대 또는 절대 경로에서 타일맵 XML을 읽습니다. */
		SW_API bool load( string_view path );
		/** @brief XML 본문에서 타일맵을 읽습니다. */
		SW_API bool loadFromXml( string_view xml );
		/** @brief Resource 상대 또는 절대 경로로 타일맵 XML을 씁니다. */
		SW_API bool save( string_view path ) const;
		/** @brief 타일맵 XML 본문을 만듭니다. */
		SW_API string toXml() const;
	};
} // namespace sw
