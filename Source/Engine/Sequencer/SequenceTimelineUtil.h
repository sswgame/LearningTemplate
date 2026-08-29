/**
 * @file SequenceTimelineUtil.h
 * @brief SequenceAsset 프레임을 씬 오브젝트 활성/트랜스폼에 적용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
	class GameObjectManager;
	class SequenceAsset;
	struct SequenceTrackItem;

	/**
	 * @brief 시퀀서 타임라인을 GameObject에 반영하는 공유 헬퍼입니다.
	 */
	struct SW_API SequenceTimelineUtil
	{
		/** @brief 클립에 적용할 트랜스폼이 있으면 true입니다. */
		static bool hasTransform( const SequenceTrackItem& item );
		/** @brief 해당 프레임의 클립 활성/트랜스폼과 이벤트 로그를 적용합니다. */
		static void applyFrame( GameObjectManager* pManager, const SequenceAsset& asset, int32 frame, int32 previousFrame = -1 );
	};
} // namespace sw
