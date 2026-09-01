/**
 * @file SaveGame.h
 * @brief TurnBattle 영속 세이브 (맵 + 파티 + 스토리 플래그). 일시적 전투 상태는 제외합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/SaveGame.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) TurnBattleSaveGame — 리플렉션 기반 공통 슬롯 필드 + 파티
	// ------------------------------------------------------------------------------
	/** @brief 맵·좌표·플래그·파티를 리플렉션으로 파일에 저장합니다. */
	REFLECT()
	struct SW_GF_API TurnBattleSaveGame : public SaveGame, public IFlagStore
	{
	public:
		REFLECT_BODY();

		PROPERTY( Alias = "mapPath" )
		string _mapPath{}; ///< 현재 맵 (비어 있으면 GameData::_startMap)

		PROPERTY( Alias = "playerX" )
		int32 _playerX{ 1 };

		PROPERTY( Alias = "playerY" )
		int32 _playerY{ 1 };

		PROPERTY( Alias = "listParty" )
		vector<PartyMember> _listParty{};

		PROPERTY( Alias = "mapFlag" )
		map<string, int32> _mapFlag{}; ///< 스토리 플래그

		/** @brief 파티를 비웁니다. */
		void clearParty();
		/** @brief 외부 파티로 교체합니다. */
		void setPartyFrom( const vector<PartyMember>& listParty );
		/** @brief 스타터 파티가 없으면 채웁니다. */
		void ensureStarterParty();

		/** @brief 플래그 값을 반환합니다. 없으면 defaultValue입니다. */
		int32 getFlag( string_view key, int32 defaultValue = 0 ) const override;
		/** @brief 플래그 값을 설정합니다. */
		void setFlag( string_view key, int32 value ) override;

		/** @brief 세이브 데이터를 파일로 저장합니다. */
		bool saveToFile( string_view path ) const override;
		/** @brief 파일에서 세이브 데이터를 불러옵니다. */
		bool loadFromFile( string_view path ) override;
	};

	namespace turnbattle
	{
		using SaveGame = TurnBattleSaveGame;
	} // namespace turnbattle
} // namespace sw
