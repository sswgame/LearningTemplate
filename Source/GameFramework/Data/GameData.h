/**
 * @file GameData.h
 * @brief Resource/.../data/gamedata.xml — 배포 게임플레이 경로/튜닝
 * @note 팩 선택은 Host Config/Game/GameConfig.json (Dev) 또는 Shipping 베이크.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) GameData — 맵·오디오·입력·조우 튜닝
	//    엔진 셸은 EngineData, 에디터 도구는 EditorData
	// ------------------------------------------------------------------------------

	/** @brief gamedata.xml 게임플레이 경로와 튜닝 값 */
	struct SW_GF_API GameData
	{
		string	_startMap{};													 ///< 시작 맵
		string	_titleScene{};													 ///< 타이틀 씬
		string	_entranceScene{};												 ///< 타이틀 다음 씬
		string	_battleMap{};													 ///< 전투 맵
		string	_battleScene{};													 ///< 전투 씬
		string	_defaultSavePath{};												 ///< 기본 세이브 슬롯
		string	_speciesData{};													 ///< 종족 테이블
		string	_stringsData{};													 ///< 문자열 테이블 (단일 파일 폴백)
		string	_localizationDirectory{};										 ///< 다국어 팩 디렉터리
		string	_defaultLanguage{ "ko_kr" };									 ///< 기본 활성 언어
		string	_fallbackLanguage{ "en_us" };									 ///< 대체(Fallback) 언어
		string	_monstersData{};												 ///< 몬스터 테이블
		string	_starterId{ "starter_a" };										 ///< 초기 파티 종족 ID
		string	_defaultEncounterId{ "critter_a" };								 ///< 폴백 조우 종족 ID
		string	_dungeonBgm{};													 ///< 던전 BGM
		string	_bossBgm{};														 ///< 보스 BGM
		string	_bossDefeatSfx{};												 ///< 보스 처치 효과음
		string	_attackSfx{};													 ///< 공격 효과음
		string	_inputMap{};													 ///< 게임플레이 InputMap
		string	_renderPipeline{ "engine/pipeline/forwardpipeline.xml" };		 ///< 렌더 파이프라인
		string	_defaultMaterial{ "engine/materials/defaultmaterial.material" }; ///< 폴백 머티리얼 (엔진 기본)
		string	_glassMaterialInstance{};										 ///< 유리 MIC
		float32 _encounterRate{ 0.33f };										 ///< 조우 타일 확률
		int32	_starterLevel{ 5 };												 ///< 스타터 레벨
		int32	_maxPartySize{ 6 };												 ///< 파티 상한

		/** @brief 리소스 경로(XML)에서 부트스트랩 테이블을 로드합니다. 빈 경로면 팩 루트의 data/gamedata.xml 을 씁니다. */
		bool loadFromResource( string_view assetRelativePath = {} );
	};

	// ------------------------------------------------------------------------------
	// 2) BootstrapConfig — 팩 루트 + GameData
	//    팩 루트는 각 게임 모듈이 configureBootstrap 에서 지정한다.
	// ------------------------------------------------------------------------------
	/** @brief Resource 아래 팩 루트와 gamedata 로딩 */
	struct SW_GF_API BootstrapConfig
	{
		string	 _packRoot{}; ///< Resource 상대 팩 폴더
		GameData _data{};	  ///< `{packRoot}/data/gamedata.xml` 테이블

		/** @brief packRoot 아래 상대 경로를 Resource 상대 경로로 만듦 */
		string resolve( string_view packRelative ) const;

		/** @brief `{packRoot}/data/gamedata.xml`을 로드하고 컴포넌트 Defaults 경로를 연결합니다. */
		bool load( string_view gamedataFileName = "data/gamedata.xml" );
	};
} // namespace sw
