/**
 * @file SequenceTimelineUtil.h
 * @brief SequenceAsset 프레임을 씬 오브젝트 활성/트랜스폼에 적용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct SequenceTrackItem;

    class GameObjectManager;
    class SequenceAsset;

    /**
     * @brief 시퀀서 타임라인을 GameObject에 반영하는 공유 헬퍼입니다.
     */
    struct SW_API SequenceTimelineUtil
    {
        /**
         * @brief "이전 프레임이 없다" — 이 값을 넘기면 이벤트를 쏘지 않습니다.
         * @details 예전 기본값은 `-1` 이었고 판정도 `previousFrame < 0` 이었다. 그래서
         *          **음수 프레임이 있는 시퀀스에서는 멀쩡한 이전 프레임이 이 뜻으로 읽혔고**,
         *          반대로 `_frameMin` 이 0 인 흔한 시퀀스에서는 "첫 프레임을 막 지났다"를
         *          표현할 값(-1)이 이 뜻과 겹쳐 첫 프레임 이벤트를 쏠 방법이 없었다.
         */
        static constexpr int32 kNoPreviousFrame = ( -2147483647 - 1 );

        /** @brief 클립에 적용할 트랜스폼이 있으면 true입니다. */
        static bool hasTransform( const SequenceTrackItem& item );
        /**
         * @brief 해당 프레임의 클립 활성/트랜스폼을 적용하고, 지나간 이벤트를 모읍니다.
         * @param previousFrame 직전에 적용한 프레임. 이벤트는 그 사이를 **지나간 것**만 센다.
         *                      `kNoPreviousFrame` 이면 이벤트를 보지 않는다.
         * @param pOutListCrossedEvent 널이 아니면 이번에 지나간 이벤트 항목들로 **채워 넣는다**(먼저 비운다).
         *                             가리키는 `SequenceTrackItem` 은 @p asset 안의 원소이므로 애셋보다 오래 살지 않는다.
         * @details **이벤트에 반응하는 유일한 방법이 이 출력이다.** 예전에는 지나간 이벤트를
         *          `SW_LOG_INFO` 한 줄로만 알렸는데, 그 매크로는 **배포본에서 통째로 사라진다** —
         *          즉 출시된 게임에서 시퀀서 이벤트 트랙은 아무 일도 하지 않았고, 그 사실이
         *          Dev 에서는 로그가 보이니 드러나지도 않았다. 로그는 Dev 편의로 남겨 둔다.
         */
        static void applyFrame( GameObjectManager* pManager, const SequenceAsset& asset, int32 frame,
                                int32                             previousFrame        = kNoPreviousFrame,
                                vector<const SequenceTrackItem*>* pOutListCrossedEvent = nullptr );
    };
} // namespace sw
