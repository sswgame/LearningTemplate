/**
 * @file GameFrameworkMinimal.h
 * @brief GameFramework 소비자를 위한 공통 포함
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "GameFramework/Base/IGame.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) FacingDir — 오버월드 스텝 · 액션 룸 공격 박스
	//    두 키트가 같은 4방향을 씀
	// ------------------------------------------------------------------------------
	/** @brief 캐릭터가 바라보는 4방향 (오버월드·액션 키트 공유) */
	enum class FacingDir : uint8
	{
		Down = 0,
		Left,
		Right,
		Up
	};
} // namespace sw
