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
    class SequencePlayer;

    /**
     * @brief 시퀀서 타임라인을 GameObject 에 반영하는 공용 도우미입니다.
     */
    struct SW_API SequenceTimelineUtil
    {
        /**
         * @brief "이전 프레임이 없다" 는 표시입니다. 이 값을 넘기면 이벤트를 쏘지 않습니다.
         * @details 예전 기본값은 `-1` 이었고 판정도 `previousFrame < 0` 이었습니다. 그래서
         *          **음수 프레임이 있는 시퀀스에서는 멀쩡한 이전 프레임이 이 뜻으로 읽혔고**,
         *          반대로 `_frameMin` 이 0 인 흔한 시퀀스에서는 "첫 프레임을 막 지났다"를
         *          표현할 값(-1)이 이 뜻과 겹쳐 첫 프레임 이벤트를 쏠 방법이 없었습니다.
         */
        static constexpr int32 kNoPreviousFrame = ( -2147483647 - 1 );

        /** @brief 클립에 적용할 트랜스폼이 있으면 true 입니다. */
        static bool hasTransform( const SequenceTrackItem& item );
        /**
         * @brief 그 프레임의 클립 활성 · 트랜스폼을 적용하고, 지나간 이벤트를 모읍니다.
         * @param previousFrame 직전에 적용한 프레임. 이벤트는 그 사이를 **지나간 것**만 셉니다.
         *                      `kNoPreviousFrame` 이면 이벤트를 보지 않습니다.
         * @param pOutListCrossedEvent nullptr 가 아니면 이번에 지나간 이벤트 항목들로 **채웁니다**(먼저 비웁니다).
         *                             가리키는 `SequenceTrackItem` 은 @p asset 안의 원소이므로 에셋보다 오래 살지 않습니다.
         * @details 이벤트에 반응하는 길은 이 출력입니다. `SequencePlayerComponent` 는 이것을 `registerSequenceEvent` 델리게이트로 냅니다.
         *          함께 남기는 `SW_LOG_INFO` 는 Dev 편의이고 배포본에서는 사라집니다.
         */
        static void applyFrame( GameObjectManager* pManager, const SequenceAsset& asset, int32 frame,
                                int32                             previousFrame        = kNoPreviousFrame,
                                vector<const SequenceTrackItem*>* pOutListCrossedEvent = nullptr );
        /**
         * @brief 플레이어의 이번 갱신을 적용합니다(`applyFrame`). 루프를 되감았으면 되감기 전 끝 구간(직전 프레임, `_frameMax`]의
         *        이벤트를 먼저 모읍니다 — 그 구간과 되감은 뒤 구간을 합친 것이 이번 갱신에 지나간 이벤트입니다.
         */
        static void applyPlayback( GameObjectManager* pManager, const SequencePlayer& player,
                                   vector<const SequenceTrackItem*>* pOutListCrossedEvent = nullptr );

    private:
        /** @brief (@p previousFrame, @p frame] 에서 시작하는 이벤트를 @p pOutListCrossedEvent 에 **덧붙입니다**(비우지 않는다). */
        static void appendCrossedEvents( const SequenceAsset& asset, int32 previousFrame, int32 frame,
                                         vector<const SequenceTrackItem*>* pOutListCrossedEvent );
    };
} // namespace sw
