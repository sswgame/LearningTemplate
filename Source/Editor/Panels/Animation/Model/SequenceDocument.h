/**
 * @file SequenceDocument.h
 * @brief 시퀀서 패널이 고치는 문서입니다 — 애셋 하나와 고른 번호, 그리고 구간 항목 · 키 트랙 · 키 이동 · 프레임 환산 명령입니다. ImGui 를 모릅니다.
 * @details 그리기 · 시간 막대의 지금 프레임 · 되돌리기 이름 · 커브 편집기 상태는 `SequencerPanel` 에 남습니다.
 *          키 찍기 · 지우기 자체는 `SequenceKeyTrack` 이 맡습니다(런타임 타입이라 값의 규칙은 거기에 있습니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Sequencer/SequenceAsset.h"

#include <optional>

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class SequenceDocument
     * @brief 애셋을 통째로 들고 고칩니다.
     * @details 칸을 패널로 하나씩 옮겨 두면 옮기지 않은 칸(카메라 블렌드 · 프리롤)이 저장 때 기본값으로 돌아갑니다.
     */
    class SequenceDocument
    {
    public:
        SequenceAsset _asset;
        int32         _selectedItem;  ///< 고른 구간 항목(-1 이면 없음). 타임라인 위젯이 고칩니다
        int32         _selectedTrack; ///< 고른 키 트랙(-1 이면 없음)
        int32         _curveTrack;    ///< 커브 편집기가 보는 트랙(-1 이면 닫힘)
        int32         _curveChannel;  ///< 커브 편집기가 보는 채널

        SequenceDocument();

        // 문서 교체
        /** @brief 되돌리기가 준 애셋으로 바꿉니다. 없어진 항목 · 트랙을 가리키지 않게 고른 번호를 맞춥니다. */
        void restoreAsset( const SequenceAsset& asset );
        /** @brief 새로 연 문서의 선택입니다 — 항목은 없음, 트랙은 첫 트랙, 커브 편집기는 닫힘입니다. */
        void resetSelection();

        // 구간 항목
        /**
         * @brief 패널이 새로 만들어 줄 수 있는 종류면 true 입니다.
         * @details 스폰 · 외형 · 시나리오는 프리팹 · 프리셋 · 시나리오 칸이 있어야 읽히는데 패널에 그 칸이 없습니다. 만들면 저장한 파일이
         *          읽기 오류가 되고, 되돌리기 스냅숏도 읽지 못합니다. 그런 항목은 파일에서 오면 그대로 보존합니다.
         */
        [[nodiscard]] static bool canAddItemKind( SequenceItemKind kind );
        /** @brief 새로 만들 수 있는 종류의 수입니다. */
        [[nodiscard]] static uint32 getAddableItemKindCount();
        /** @brief 새로 만들 수 있는 종류 가운데 @p addableIndex 번째입니다. 범위 밖이면 `SequenceItemKind::Count` 입니다. */
        [[nodiscard]] static SequenceItemKind getAddableItemKind( uint32 addableIndex );
        /** @brief 항목을 타임라인 앞에 더하고 번호를 돌려줍니다(이름은 @p kindName 과 겹치지 않는 번호, 길이는 타임라인 안). 만들 수 없는 종류면 -1 입니다. */
        int32 addItem( SequenceItemKind kind, string_view kindName );
        /** @brief 항목을 지웁니다. 뒤의 항목을 고르고 있었으면 같은 항목을 계속 가리키게 당깁니다. 범위 밖이면 false 입니다. */
        bool removeItem( int32 itemIndex );
        /** @brief 항목의 사본을 끝에 더하고 번호를 돌려줍니다. 범위 밖이면 -1 입니다. */
        int32 duplicateItem( int32 itemIndex );

        // 키 트랙
        /** @brief 채널을 갖춘 트랙을 더하고 고릅니다. 이름은 종류와 겹치지 않는 번호입니다. */
        void addTrack( SequenceTrackKind kind, string_view targetObject );
        /** @brief 고른 트랙을 지우고 선택과 커브 편집기를 닫습니다. 고른 트랙이 없으면 false 입니다. */
        [[nodiscard]] bool removeSelectedTrack();
        /** @brief 고른 트랙입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] SequenceKeyTrack* findSelectedTrack();
        /** @brief 고른 트랙입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const SequenceKeyTrack* findSelectedTrack() const;
        /** @brief 커브 편집기가 보는 트랙입니다. 닫혀 있거나 번호가 틀리면 nullptr 입니다. */
        [[nodiscard]] SequenceKeyTrack* findCurveTrack();
        /** @brief 커브 편집기가 보는 채널입니다. 닫혀 있거나 번호가 틀리면 nullptr 입니다. */
        [[nodiscard]] FloatCurve* findCurveChannel();

        // 키 이동 · 프레임 환산
        /** @brief @p frame 앞의 가장 가까운 키의 프레임입니다(@p listKeyFrame 은 시각 순). 타임라인 밖의 키는 건너뜁니다 — 시간 막대가 갈 수 없는 자리입니다. 없으면 비어 있습니다. */
        [[nodiscard]] std::optional<int32> findPreviousKeyFrame( const vector<float32>& listKeyFrame, int32 frame ) const;
        /** @brief @p frame 뒤의 가장 가까운 키의 프레임입니다. 나머지는 `findPreviousKeyFrame` 과 같습니다. */
        [[nodiscard]] std::optional<int32> findNextKeyFrame( const vector<float32>& listKeyFrame, int32 frame ) const;
        /** @brief 타임라인 띠의 비율(0..1, 밖은 자른다)이 가리키는 프레임입니다. */
        [[nodiscard]] int32 findFrameAtRatio( float32 ratio ) const;
        /** @brief @p frame 의 타임라인 띠 비율입니다. 타임라인 밖이면 0..1 을 벗어납니다. */
        [[nodiscard]] float32 computeRatioAtFrame( float32 frame ) const;

        // 채널 값
        /** @brief 트랜스폼 트랙의 회전 채널이면 true 입니다 — 값 칸은 도로 보이고 고칩니다(저장은 라디안, 인스펙터와 같은 규칙). */
        [[nodiscard]] static bool isRotationChannel( const SequenceKeyTrack& track, uint32 channelIndex );
        /** @brief 값 칸에 보일 값입니다. 키가 있거나 커브가 있으면 그 값, 없으면 대상의 지금 값(@p currentValue)이고 회전은 도입니다. */
        [[nodiscard]] static float32 computeChannelDisplayValue( const SequenceKeyTrack& track, uint32 channelIndex, float32 frame, float32 currentValue );
        /** @brief 값 칸의 값(회전은 도)을 @p frame 의 키로 찍습니다(자동 키). 범위 밖 채널이면 false 입니다. */
        [[nodiscard]] static bool setChannelDisplayValue( SequenceKeyTrack& track, uint32 channelIndex, float32 frame, float32 displayValue );
        /**
         * @brief 키 트랙 대상의 지금 값을 채널 순서로 읽습니다(트랜스폼은 로컬 이동, 회전(라디안), 크기이고, 리그 슬롯은 가중치, 프로퍼티는 그 값입니다).
         * @details 키 찍기(지금 상태를 키로)만 쓰는 읽기라 에디터에 둔다 — 대상 · 프로퍼티 묶기는 재생과 같은 런타임 규칙(`SequenceTimelineUtil::findTarget` · `findTrackProperty`)을 쓴다.
         * @return 대상, 컴포넌트, 숫자 프로퍼티를 찾지 못하면 false 입니다.
         */
        [[nodiscard]] static bool readTrackValues( GameObjectManager* pManager, const SequenceKeyTrack& track, float32* pOutArrValue, uint32 valueCapacity );

    private:
        /** @brief 키의 프레임(가장 가까운 정수)입니다. 타임라인 밖이면 비어 있습니다. */
        [[nodiscard]] std::optional<int32> findTimelineFrame( float32 keyFrame ) const;
        /** @brief 타임라인 길이(프레임)입니다. 1 이상입니다. */
        [[nodiscard]] int32 getFrameSpan() const;
    };
} // namespace sw::editor
