/**
 * @file ISaveGame.h
 * @brief 장르 비의존 세이브 슬롯 계약
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) SaveSlot — 맵/좌표/스토리 플래그만
	//    파티 등 장르 필드는 키트 SaveGame이 확장
	// ------------------------------------------------------------------------------
	/** @brief 맵·좌표·플래그만 담는 공통 세이브 슬롯 */
	struct SW_GF_API SaveSlot
	{
		string			   _mapPath;	  ///< 현재 맵 (비어 있으면 GameData::_startMap)
		int32			   _playerX{ 1 }; ///< 플레이어 타일 X
		int32			   _playerY{ 1 }; ///< 플레이어 타일 Y
		map<string, int32> _mapFlag;	  ///< 스토리 플래그 (키 → 값)

		/** @brief 플래그 값을 반환합니다. 없으면 defaultValue입니다. */
		int32 getFlag( string_view key, int32 defaultValue = 0 ) const;
		/** @brief 플래그 값을 설정합니다. */
		void setFlag( string_view key, int32 value );

		/** @brief 공통 필드를 텍스트 포맷으로 파일에 저장합니다. */
		bool saveCommonToTextFile( string_view path ) const;
		/** @brief 파일에서 텍스트 포맷으로 공통 필드를 불러옵니다. */
		bool loadCommonFromTextFile( string_view path );

		/** @brief 공통 필드를 SAV1 바이너리 포맷(CRC32 무결성 검증)으로 저장합니다. */
		bool saveCommonToBinaryFile( string_view path ) const;
		/** @brief SAV1 바이너리 파일에서 공통 필드를 불러옵니다. */
		bool loadCommonFromBinaryFile( string_view path );

		/** @brief 공통 필드를 파일로 저장합니다. (.sav/.bin 확장자 시 바이너리, 그 외 텍스트) */
		bool saveCommonToFile( string_view path ) const;
		/** @brief 파일에서 공통 필드를 불러옵니다. (매직 헤더 감지 후 바이너리 또는 텍스트 로드) */
		bool loadCommonFromFile( string_view path );
	};

	// ------------------------------------------------------------------------------
	// 2) ISaveGame — 키트가 파일 포맷을 구현
	// ------------------------------------------------------------------------------
	/** @brief 장르 키트가 구현하는 파일 세이브 계약 */
	class SW_GF_API ISaveGame
	{
	public:
		/** @brief 슬롯 상태는 파생 클래스가 가집니다. */
		ISaveGame() = default;
		/** @brief 복사를 금지합니다. */
		ISaveGame( const ISaveGame& ) = delete;
		/** @brief 복사 대입을 금지합니다. */
		ISaveGame& operator=( const ISaveGame& ) = delete;
		/** @brief 파생 세이브가 버퍼를 해제할 수 있게 합니다. */
		virtual ~ISaveGame() = default;
		/** @brief 세이브 데이터를 파일로 저장합니다. */
		virtual bool saveToFile( string_view path ) const = 0;
		/** @brief 파일에서 세이브 데이터를 불러옵니다. */
		virtual bool loadFromFile( string_view path ) = 0;
	};
} // namespace sw
