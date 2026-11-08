/**
 * @file SequenceModel.h
 * @brief 시퀀서 패널이 고치는 문서입니다 — 애셋 하나와 고른 번호, 그리고 바인딩 · 구간 항목 · 카메라 컷 차선 · 키 트랙 · 키 선택과 끌기 · 프레임 환산 명령입니다. ImGui 를 모릅니다.
 * @details 그리기 · 시간 막대의 지금 프레임 · 되돌리기 이름 · 커브 편집기 상태는 `SequencerPanel` 에 남습니다.
 *          키 찍기 · 지우기 · 옮기기 자체는 `SequenceKeyTrack` 이 맡습니다(런타임 타입이라 값의 규칙은 거기에 있습니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Sequencer/SequenceAsset.h"

#include <optional>

namespace sw::editor
{
    /** @brief 고른 키 하나 — 트랙 번호와 키 프레임입니다(채널마다 같은 프레임의 키는 하나로 고른다, 언리얼 Sequencer 의 키 마름모와 같다). */
    struct SequenceKeyRef
    {
        int32   _track{ -1 };
        float32 _frame{ 0.0f };
    };

    /**
     * @brief 복사한 키 하나 — 복사한 키 가운데 맨 앞 프레임 · 맨 위 트랙에서의 거리와 채널마다의 키(값 · 접선 · 보간)입니다.
     * @details 붙여넣기는 고른 트랙 · 지금 프레임을 맨 위 · 맨 앞으로 삼아 같은 거리에 놓는다(언리얼 Sequencer 의 키 붙여넣기와 같다).
     */
    struct SequenceCopiedKey
    {
        FloatCurveKey _arrChannelKey[kSequenceMaxChannelCount]{}; ///< 채널마다의 키(`_channelMask` 의 비트가 선 채널만 뜻이 있다)
        int32         _trackOffset{ 0 };                          ///< 맨 위 복사 트랙에서 몇 줄 아래인지
        int32         _frameOffset{ 0 };                          ///< 맨 앞 복사 키에서 몇 프레임 뒤인지
        uint32        _channelCount{ 0 };                         ///< 복사한 트랙의 채널 수 — 다른 채널 수의 트랙에는 붙이지 않는다
        uint32        _channelMask{ 0 };                          ///< 키가 있던 채널의 비트
    };

    /** @brief 키 트랙이 저장 · 재생에서 어긋나는 모양입니다(트랙 줄의 오류 배지 · `SequenceModel::findTrackIssue`). 하나만 알린다 — 먼저 고칠 것부터다. */
    enum class SequenceTrackIssue : uint8
    {
        None = 0,            ///< 어긋나지 않았다
        UnknownKind,         ///< 종류 표에 없는 종류다
        ChannelCount,        ///< 채널 수가 종류와 다르다 — 파일이 읽히지 않는다
        MissingBinding,      ///< 시퀀스에 없는 바인딩을 가리킨다 — 파일이 읽히지 않는다
        MissingPropertyPath, ///< 프로퍼티 · 리그 슬롯 트랙인데 대상 경로가 비었다 — 재생이 아무것도 몰지 않는다
        KeysOutsideTimeline, ///< 타임라인 밖의 키가 있다 — 재생이 닿지 않는다
    };

    /**
     * @class SequenceModel
     * @brief 애셋을 통째로 들고 고칩니다.
     * @details 칸을 패널로 하나씩 옮겨 두면 옮기지 않은 칸(카메라 블렌드 · 프리롤)이 저장 때 기본값으로 돌아갑니다.
     *          설계: `_asset` 에는 필수 칸이 빈 항목이 들어가지 않는다 — 칸은 사본(`_itemEdit`)을 고치고 값이 찬 뒤에만 적용한다(언리얼 Sequencer 가 고른 대상으로 스폰 트랙을 만드는 것과 같다). 스냅숏이 편집 중 상태를 받게 하면 저장 · 재생과 읽기 규칙이 갈라진다.
     *          새 항목 · 키 트랙 · 컷의 대상은 고른 바인딩(`_selectedBinding`)이다 — 이름을 적는 칸은 없다.
     */
    class SequenceModel
    {
    public:
        /** @brief 보이는 폭의 하한(프레임)입니다. 키 띠에서 이웃 키가 겹치지 않을 만큼이다. */
        static constexpr int32 kMinViewFrameSpan = 4;

        SequenceAsset     _asset;
        SequenceTrackItem _itemEdit;        ///< 항목 칸이 고치는 사본(초안 또는 고른 항목). 문서 · 되돌리기 스냅숏에 들어가지 않습니다
        int32             _selectedItem;    ///< 고른 구간 항목(-1 이면 없음). 타임라인 위젯이 고칩니다
        int32             _selectedTrack;   ///< 고른 키 트랙(-1 이면 없음)
        int32             _curveTrack;      ///< 커브 편집기가 보는 트랙(-1 이면 닫힘)
        int32             _curveChannel;    ///< 커브 편집기가 보는 채널
        uint32            _selectedBinding; ///< 고른 바인딩 id(0 이면 없음). 새 항목 · 트랙 · 컷의 대상입니다

        SequenceModel();

        // 문서 교체
        /** @brief 되돌리기가 준 애셋으로 바꿉니다. 없어진 항목 · 트랙 · 바인딩을 가리키지 않게 고른 번호를 맞추고 키 선택을 비웁니다. */
        void restoreAsset( const SequenceAsset& asset );
        /** @brief 새로 연 문서의 선택입니다 — 항목은 없음, 트랙 · 바인딩은 첫 것, 커브 편집기와 초안은 닫힘, 키 선택은 비움입니다. */
        void resetSelection();

        // 바인딩
        /** @brief 이름 @p name 의 바인딩을 더하고 고른 뒤 id 를 돌려줍니다. 이름이 비면 0 입니다. */
        uint32 addBinding( string_view name );
        /** @brief 바인딩 이름(표시용)을 바꿉니다. 없는 바인딩 · 빈 이름이면 false 입니다. */
        [[nodiscard]] bool renameBinding( uint32 bindingID, string_view name );
        /** @brief 쓰는 항목 · 트랙이 없는 바인딩을 지웁니다. 쓰이고 있거나 없으면 false 입니다(쓰는 곳을 먼저 지운다 — 언리얼 바인딩 삭제와 같이 대상 잃은 트랙을 남기지 않는다). */
        [[nodiscard]] bool removeBinding( uint32 bindingID );

        // 구간 항목
        /**
         * @brief 항목을 타임라인 앞에 더하고 번호를 돌려줍니다(이름은 @p kindName 과 겹치지 않는 번호, 길이는 타임라인 안, 대상은 고른 바인딩). 열려 있던 초안은 닫습니다.
         * @details 필수 칸이 빈 종류(고른 바인딩이 없는 클립 · 스폰 · 외형 · 시나리오 ...)는 아직 만들지 않고 초안을 열어 -1 을 돌려줍니다(`isDraftingItem`). 표에 없는 종류도 -1 입니다.
         */
        int32 addItem( SequenceItemKind kind, string_view kindName );
        /** @brief 초안이 열려 있으면 true 입니다. 칸(`_itemEdit`)은 고른 항목이 아니라 초안을 고칩니다. */
        [[nodiscard]] bool isDraftingItem() const;
        /** @brief 초안의 필수 칸이 찼고 바인딩이 시퀀스에 있으면 항목으로 더하고 고른 뒤 번호를 돌려줍니다. 모자라면 초안을 그대로 두고 -1 입니다. */
        int32 commitItemDraft();
        /** @brief 초안을 버립니다. 문서는 바뀌지 않습니다. */
        void cancelItemDraft();
        /** @brief 사본 @p item 이 문서에 들어갈 수 있는지입니다(종류의 칸 규칙 · 바인딩이 시퀀스에 있음). 파일 읽기와 같은 규칙입니다. */
        [[nodiscard]] bool isItemAcceptable( const SequenceTrackItem& item ) const;

        // 항목의 글 칸
        /** @brief 칸이 보일 항목(초안 또는 고른 항목)이 있으면 true 입니다. */
        [[nodiscard]] bool hasEditedItem() const;
        /** @brief 칸의 사본을 고른 항목에서 다시 뜹니다(되돌리기 · 선택 변경이 보인다). 초안이 열려 있거나 고른 항목이 없으면 그대로 둡니다. */
        void syncItemEdit();
        /** @brief 칸의 글 · 바인딩을 고른 항목에 적용합니다. 규칙에 어긋나면 거절하고 칸을 항목의 값으로 되돌려 false 입니다. 구간 · 색 · 종류는 건드리지 않습니다. */
        [[nodiscard]] bool commitItemEdit();
        /** @brief 항목을 지웁니다. 뒤의 항목을 고르고 있었으면 같은 항목을 계속 가리키게 당깁니다. 범위 밖이면 false 입니다. */
        bool removeItem( int32 itemIndex );
        /** @brief 항목의 사본을 끝에 더하고 번호를 돌려줍니다. 범위 밖이면 -1 입니다. 스폰 항목은 바인딩을 둘이 채울 수 없어 -1 입니다. */
        int32 duplicateItem( int32 itemIndex );

        // 카메라 컷 차선
        /** @brief 카메라 컷 항목의 번호를 시작 프레임 순으로 채웁니다(컷 차선이 그 순서로 그린다). */
        void collectCutLane( vector<int32>& outListItemIndex ) const;
        /**
         * @brief @p frame 에서 고른 바인딩의 카메라로 컷을 시작합니다(언리얼 Camera Cut 트랙에 컷을 더하는 것과 같다). 새 컷은 다음 컷 앞(없으면 끝)까지이고,
         *        @p frame 을 덮던 앞 컷은 @p frame 앞에서 끝나게 줄인다. 새 컷을 고르고 번호를 돌려줍니다. 고른 바인딩이 없거나 @p frame 이 타임라인 밖이면 -1 입니다.
         */
        int32 addCameraCut( int32 frame );
        /**
         * @brief @p frame 을 덮는 컷을 그 프레임에서 둘로 나눕니다(같은 카메라 — 언리얼 Sequencer 의 섹션 Split). 뒤 조각을 고르고 번호를 돌려줍니다.
         * @details 컷의 첫 프레임에서는 나눌 것이 없어 -1 입니다. 덮는 컷이 없어도 -1 입니다.
         */
        int32 splitCameraCut( int32 frame );
        /**
         * @brief 컷 @p itemIndex 를 지우고, 바로 앞에 붙어 있던 컷이 그 자리까지 늘어납니다(쇼트 목록에 빈 화면이 생기지 않는다 — 언리얼 Camera Cut 트랙에서 섹션을 지우고 앞 섹션을 늘리는 것과 같다).
         * @return 컷이 아니거나 범위 밖이면 false 입니다.
         */
        [[nodiscard]] bool removeCameraCut( int32 itemIndex );
        /** @brief 컷 @p itemIndex 의 카메라를 바인딩 @p bindingID 로 바꿉니다. 컷이 아니거나 바인딩이 없으면 false 입니다. */
        [[nodiscard]] bool setCutCamera( int32 itemIndex, uint32 bindingID );

        // 키 트랙
        /** @brief 채널을 갖춘 트랙을 고른 바인딩에 더하고 고릅니다. 이름은 종류와 겹치지 않는 번호입니다. 고른 바인딩이 없으면 false 입니다. */
        [[nodiscard]] bool addTrack( SequenceTrackKind kind );
        /** @brief 고른 트랙을 지우고 선택과 커브 편집기를 닫습니다. 고른 트랙이 없으면 false 입니다. */
        [[nodiscard]] bool removeSelectedTrack();
        /**
         * @brief 트랙 @p fromIndex 를 @p toIndex 자리로 옮깁니다(사이의 트랙은 한 칸씩 밀린다). 고른 트랙 · 커브 편집기 · 고른 키가 같은 트랙을 계속 가리킨다.
         * @details 순서는 파일의 `_listTrack` 순서이고 재생 결과는 바뀌지 않는다(트랙은 서로 다른 값을 몬다). 범위 밖이거나 같은 자리면 false 입니다.
         */
        [[nodiscard]] bool moveTrack( int32 fromIndex, int32 toIndex );
        /** @brief 트랙 @p trackIndex 의 어긋난 모양입니다. 범위 밖이면 `None` 입니다(그릴 줄이 없다). */
        [[nodiscard]] SequenceTrackIssue findTrackIssue( int32 trackIndex ) const;
        /** @brief 어긋난 트랙 수입니다(패널 머리의 요약 · 감시 값). */
        [[nodiscard]] uint32 countTrackIssues() const;
        /** @brief 고른 트랙입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] SequenceKeyTrack* findSelectedTrack();
        /** @brief 고른 트랙입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const SequenceKeyTrack* findSelectedTrack() const;
        /** @brief 커브 편집기가 보는 트랙입니다. 닫혀 있거나 번호가 틀리면 nullptr 입니다. */
        [[nodiscard]] SequenceKeyTrack* findCurveTrack();
        /** @brief 커브 편집기가 보는 채널입니다. 닫혀 있거나 번호가 틀리면 nullptr 입니다. */
        [[nodiscard]] FloatCurve* findCurveChannel();

        // 키 선택 · 끌기
        /** @brief 키 하나를 고릅니다. @p bToggle 이면 선택에 넣거나 빼고(Ctrl), 아니면 그 키만 고릅니다. 선택 안의 키를 다시 누르면(토글 아님) 다중 선택을 지킨다 — 끌기 시작이다. */
        void selectKey( int32 trackIndex, float32 frame, bool bToggle );
        /** @brief 그 키를 골랐으면 true 입니다. */
        [[nodiscard]] bool isKeySelected( int32 trackIndex, float32 frame ) const;
        /** @brief 키 선택을 비웁니다. */
        void clearKeySelection() { _listSelectedKey.clear(); }
        /** @brief 고른 키입니다. */
        const vector<SequenceKeyRef>& getSelectedKeys() const { return _listSelectedKey; }
        /**
         * @brief 고른 키를 모두 @p frameDelta 프레임 옮깁니다(정수 프레임 — 스냅). 하나라도 타임라인 밖으로 나가면 옮기지 않고 false 입니다.
         * @details 한 트랙의 키 여럿을 같은 쪽으로 옮길 때 앞선 키가 아직 옮기지 않은 키를 덮지 않게 옮기는 쪽 끝부터 옮긴다. 놓은 자리의 고르지 않은 키는 덮인다.
         */
        [[nodiscard]] bool moveSelectedKeys( int32 frameDelta );
        /** @brief 고른 키를 모두 지우고 지운 키 프레임 수를 돌려줍니다. 선택은 빈다. */
        uint32 removeSelectedKeys();

        // 복사 · 붙여넣기(문서 안의 클립보드 — 되돌리기 스냅숏에 들어가지 않는다)
        /** @brief 고른 키(채널마다의 값 · 접선 · 보간)를 클립보드에 담고 담은 키 수를 돌려줍니다. 고른 키가 없으면 클립보드를 그대로 두고 0 입니다. */
        uint32 copySelectedKeys();
        /**
         * @brief 담은 키를 고른 트랙(맨 위) · @p frame(맨 앞)에 붙이고 붙인 키 수를 돌려줍니다. 붙인 키를 고릅니다.
         * @details 트랙이 모자라거나 채널 수가 다른 트랙 · 타임라인 밖 프레임의 키는 건너뛴다. 그 자리의 키는 덮는다. 고른 트랙이 없으면 0 입니다.
         */
        uint32 pasteKeys( int32 frame );
        /** @brief 담은 키가 있으면 true 입니다. */
        [[nodiscard]] bool hasCopiedKeys() const { return _listCopiedKey.empty() == false; }
        /** @brief 고른 구간 항목을 클립보드에 담습니다. 고른 항목이 없으면 false 입니다. */
        [[nodiscard]] bool copySelectedItem();
        /**
         * @brief 담은 항목을 @p frame 에서 시작하게 붙이고(길이는 그대로, 끝은 타임라인 안으로 자른다) 고른 뒤 번호를 돌려줍니다.
         * @details 이름은 겹치지 않는 번호를 붙인다. 스폰 항목은 바인딩을 둘이 채울 수 없어 -1, 담은 항목이 없거나 @p frame 이 타임라인 밖이면 -1 입니다.
         */
        int32 pasteItem( int32 frame );
        /** @brief 담은 항목이 있으면 true 입니다. */
        [[nodiscard]] bool hasCopiedItem() const { return _bItemCopied == SW_TRUE; }

        // 보기(확대 · 스크롤 — 키 띠 · 컷 차선이 같은 범위를 쓴다, 문서에 저장하지 않는다)
        /** @brief 보이는 첫 프레임입니다. 보기를 맞춘(전체) 상태면 타임라인 첫 프레임입니다. */
        [[nodiscard]] int32 getViewFirstFrame() const;
        /** @brief 보이는 프레임 폭(1 이상)입니다. 보기를 맞춘 상태면 타임라인 길이입니다. */
        [[nodiscard]] int32 getViewFrameSpan() const;
        /** @brief 보기가 타임라인 전체면 true 입니다. */
        [[nodiscard]] bool isViewFitted() const { return _viewFrameSpan <= 0; }
        /**
         * @brief 보이는 폭을 @p factor 배로 바꿉니다(1 보다 작으면 확대). @p anchorRatio(0..1 — 포인터 자리)의 프레임이 그 자리에 남는다(언리얼 · 유니티 타임라인의 Ctrl+휠).
         * @details 폭은 `kMinViewFrameSpan` 과 타임라인 길이 사이로 자르고, 타임라인 전체에 닿으면 맞춘 상태로 돌아간다.
         */
        void zoomView( float32 factor, float32 anchorRatio );
        /** @brief 보이는 범위를 @p frameDelta 프레임 옮깁니다(타임라인 밖으로는 나가지 않는다). 맞춘 상태면 그대로입니다. */
        void scrollView( int32 frameDelta );
        /** @brief 보이는 첫 프레임을 @p frame 으로 둡니다(타임라인 안으로 자른다). 맞춘 상태면 그대로입니다. */
        void setViewFirstFrame( int32 frame );
        /** @brief 타임라인 전체를 봅니다. */
        void fitView() { _viewFrameSpan = 0; }

        // 키 이동 · 프레임 환산
        /** @brief @p frame 앞의 가장 가까운 키의 프레임입니다(@p listKeyFrame 은 시각 순). 타임라인 밖의 키는 건너뜁니다 — 시간 막대가 갈 수 없는 자리입니다. 없으면 비어 있습니다. */
        [[nodiscard]] optional<int32> findPreviousKeyFrame( const vector<float32>& listKeyFrame, int32 frame ) const;
        /** @brief @p frame 뒤의 가장 가까운 키의 프레임입니다. 나머지는 `findPreviousKeyFrame` 과 같습니다. */
        [[nodiscard]] optional<int32> findNextKeyFrame( const vector<float32>& listKeyFrame, int32 frame ) const;
        /** @brief 타임라인 띠의 비율(0..1, 밖은 자른다)이 가리키는 프레임입니다. 띠는 보이는 범위(`getViewFirstFrame` · `getViewFrameSpan`)를 그린다. */
        [[nodiscard]] int32 findFrameAtRatio( float32 ratio ) const;
        /** @brief @p frame 의 타임라인 띠 비율입니다. 보이는 범위 밖이면 0..1 을 벗어납니다. */
        [[nodiscard]] float32 computeRatioAtFrame( float32 frame ) const;
        /** @brief 띠 폭 @p stripWidth 픽셀에서 @p pixelDelta 픽셀 끌기가 몇 프레임인지입니다(가장 가까운 정수 — 키 끌기의 스냅, 보이는 폭 기준). */
        [[nodiscard]] int32 computeFrameDelta( float32 pixelDelta, float32 stripWidth ) const;

        // 채널 값
        /** @brief 회전 채널(트랜스폼 회전 · 렌즈 화각)이면 true 입니다 — 값 칸은 도로 보이고 고칩니다(저장은 라디안, 인스펙터와 같은 규칙). */
        [[nodiscard]] static bool isRotationChannel( const SequenceKeyTrack& track, uint32 channelIndex );
        /** @brief 값 칸에 보일 값입니다. 키가 있거나 커브가 있으면 그 값, 없으면 대상의 지금 값(@p currentValue)이고 회전은 도입니다. */
        [[nodiscard]] static float32 computeChannelDisplayValue( const SequenceKeyTrack& track, uint32 channelIndex, float32 frame, float32 currentValue );
        /** @brief 값 칸의 값(회전은 도)을 @p frame 의 키로 찍습니다(자동 키). 범위 밖 채널이면 false 입니다. */
        [[nodiscard]] static bool setChannelDisplayValue( SequenceKeyTrack& track, uint32 channelIndex, float32 frame, float32 displayValue );

    private:
        /** @brief 키의 프레임(가장 가까운 정수)입니다. 타임라인 밖이면 비어 있습니다. */
        [[nodiscard]] optional<int32> findTimelineFrame( float32 keyFrame ) const;
        /** @brief 타임라인 길이(프레임)입니다. 1 이상입니다. */
        [[nodiscard]] int32 getFrameSpan() const;
        /** @brief `_selectedItem` 이 있는 항목을 가리키면 true 입니다. */
        [[nodiscard]] bool hasSelectedItem() const;

    private:
        vector<SequenceKeyRef>    _listSelectedKey; ///< 고른 키(트랙 · 프레임)
        vector<SequenceCopiedKey> _listCopiedKey;   ///< 복사한 키(`copySelectedKeys`)
        SequenceTrackItem         _copiedItem;      ///< 복사한 항목(`copySelectedItem`)
        int32                     _viewFirstFrame;  ///< 보이는 첫 프레임(맞춘 상태면 쓰지 않는다)
        int32                     _viewFrameSpan;   ///< 보이는 폭(0 이하면 타임라인 전체 — 타임라인 길이가 바뀌어도 따라간다)
        uint8                     _bItemDraft  : 1; ///< `_itemEdit` 이 아직 문서에 없는 초안입니다
        uint8                     _bItemCopied : 1; ///< `_copiedItem` 이 찼다
        [[maybe_unused]] uint8    _reserved    : 6;
    };
} // namespace sw::editor
