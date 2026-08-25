/**
 * @file SaveGame.h
 * @brief 영속 세이브 블롭 (맵 + 파티 + 스토리 플래그). 일시적 전투 상태는 제외합니다.
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"
#include "GameFramework/Save/ISaveGame.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) TurnBattle 세이브 — 공통 슬롯 필드 + 파티
	//    전투 페이즈는 넣지 않음
	// ------------------------------------------------------------------------------
	/** @brief 맵·좌표·플래그·파티를 파일로 저장합니다. */
	struct SW_GF_API SaveGame : ISaveGame
	{
		string				_mapPath; ///< 현재 맵 (비어 있으면 GameData::_startMap)
		int32				_playerX{ 1 };
		int32				_playerY{ 1 };
		vector<PartyMember> _listParty;
		map<string, int32>	_mapFlags; ///< 스토리 플래그

		/** @brief 파티를 비웁니다. */
		void clearParty();
		/** @brief 외부 파티로 교체합니다. */
		void setPartyFrom( const vector<PartyMember>& party );
		/** @brief 스타터 파티가 없으면 채웁니다. */
		void ensureStarterParty();

		/** @brief 플래그 값을 반환합니다. 없으면 defaultValue입니다. */
		int32 getFlag( string_view key, int32 defaultValue = 0 ) const;
		/** @brief 플래그 값을 설정합니다. */
		void setFlag( string_view key, int32 value );

		/** @brief 세이브 데이터를 파일로 저장합니다. */
		bool saveToFile( string_view path ) const override;
		/** @brief 파일에서 세이브 데이터를 불러옵니다. */
		bool loadFromFile( string_view path ) override;
	};
} // namespace sw
