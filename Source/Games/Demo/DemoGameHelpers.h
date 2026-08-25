/**
 * @file DemoGameHelpers.h
 * @brief DemoGame 공용 헬퍼 (여러 TU에서 공유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 데모 헬퍼 — 샘플 액터 · 세이브 경로 · HP 게이지
	//    DemoGame과 다른 TU가 공유 (네임스페이스로 묶지 않음)
	// ------------------------------------------------------------------------------
	/** @brief 모듈 샘플 액터를 파괴합니다. */
	void destroyModuleSampleActors();
	/** @brief 샘플 액터가 없으면 스폰합니다. */
	void spawnSampleActorIfMissing();
	/** @brief 데모 큐브가 없으면 스폰합니다. */
	void spawnDemoCubeIfMissing();
	/** @brief 데모 큐브를 파괴합니다. */
	void destroyDemoCube();
	/** @brief 상대 세이브 경로를 Resource/작업 디렉터리 절대 경로로 해석합니다. */
	string resolveSavePath( string_view relativePath );
	/** @brief max가 0이면 0, 아니면 current/max 비율을 반환합니다. */
	float32 safeFill( int32 current, int32 max );
} // namespace sw
