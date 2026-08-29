/**
 * @file SpeciesData.h
 * @brief 종족 / 기술 테이블 + 파티 멤버 (game/<pack>/data/species.xml에서 로드)
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
	// 1) 데이터 — 기술 정의, 종족 정의, 런타임 파티 멤버
	//    _name 기본값은 XML 폴백 리터럴 (번역하지 않음)
	// ------------------------------------------------------------------------------
	/** @brief 기술 한 칸 (위력·PP) */
	struct MoveDef
	{
		string _id{ "tackle" };
		string _name{ "Tackle" }; ///< 표시 이름 (strings.xml과 별개 폴백)
		int32  _power{ 40 };
		int32  _ppMax{ 35 };
	};

	/** @brief 종족 한 행 (기초 스탯 + 기술 슬롯 인덱스) */
	struct SpeciesDef
	{
		string _id{ "critter_a" };
		string _name{ "Wild Critter" }; ///< 표시 이름 폴백
		int32  _baseHp{ 40 };
		int32  _baseAtk{ 10 };
		int32  _move0{ 0 }; ///< MoveDef 인덱스
		int32  _move1{ 1 };
	};

	/** @brief 런타임 파티 멤버 (세이브에 들어감) */
	struct PartyMember
	{
		string _speciesId{ "critter_a" };
		string _nickname{};
		int32  _level{ 5 };
		int32  _hp{ 40 };
		int32  _hpMax{ 40 };
		int32  _pp0{ 35 };
		int32  _pp1{ 20 };
		int32  _exp{ 0 };
		int32  _expNext{ 50 };
	};

	// ------------------------------------------------------------------------------
	// 2) SpeciesCatalog — 종족 / 기술 카탈로그 인스턴스
	//    로드 실패 시 최소 폴백을 심어 전투가 비지 않게
	// ------------------------------------------------------------------------------
	/** @brief species.xml 종족·기술 테이블 서비스 */
	class SW_GF_API SpeciesCatalog
	{
	public:
		SpeciesCatalog();
		~SpeciesCatalog();

		SpeciesCatalog( const SpeciesCatalog& )			   = delete;
		SpeciesCatalog& operator=( const SpeciesCatalog& ) = delete;

		/** @brief 리소스 경로에서 기술/종족을 로드합니다. 실패 시 최소 폴백을 심습니다. */
		bool loadFromResource( string_view assetRelativePath );

		/** @brief ID로 종족 정의를 찾습니다. */
		const SpeciesDef* findSpecies( const utf8* pId ) const;
		/** @brief 인덱스로 기술 정의를 찾습니다. */
		const MoveDef* findMove( int32 index ) const;
		/** @brief ID로 기술 인덱스를 찾습니다. */
		int32 findMoveIndex( const utf8* pId ) const;

		/** @brief 야생 조우용 파티 멤버를 만듭니다. */
		PartyMember makeWild( const utf8* pSpeciesId, int32 level = 5 ) const;
		/** @brief 스타터 파티 멤버를 만듭니다. */
		PartyMember makeStarter( const utf8* pSpeciesId = "starter_a", int32 level = 5 ) const;

		/** @brief 로드된 카탈로그를 비웁니다. */
		void clear();

	private:
		void seedFallback();

		vector<MoveDef>	   _moveList;
		vector<SpeciesDef> _speciesList;
	};
} // namespace sw
