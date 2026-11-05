/**
 * @file ParkLayoutModel.h
 * @brief Park Layout 패널 · 시각화 · 맥락 메뉴가 함께 보는 공원 배치의 상태와 판단입니다. 파일 · 테마 · ImGui 를 모릅니다(글을 받아 짓고, 색은 받는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Editor/Viewport/Model/EditorVisualizerGeometry.h"

namespace sw
{
    class CoasterLayoutCatalog;
    class ParkLayout;
} // namespace sw

namespace sw::editor
{
    class EditorListFilter;

    /** @brief 배치 검사가 찾은 문제의 종류입니다(언리얼 Map Check 처럼 저장 전에 눈으로 본다). */
    enum class ParkRideIssueKind : uint8
    {
        Overlap = 0,             ///< 다른 놀이기구와 바닥 영역이 겹친다
        EntranceInsideFootprint, ///< 줄 입구가 자기 바닥 영역 안이다
        DuplicateID              ///< 다른 놀이기구와 id 가 같다
    };

    /** @brief 놀이기구 하나에 달린 문제입니다. */
    struct ParkRideIssue
    {
        ParkRideIssueKind _kind{ ParkRideIssueKind::Overlap };
        uint32            _otherRide{ 0 }; ///< 상대 놀이기구 번호(`Overlap` · `DuplicateID`)
    };

    /** @brief 놀이기구 표의 정렬 칸입니다. `FileOrder` 는 정렬하지 않고 파일 순서입니다. */
    enum class ParkRideSortColumn : uint8
    {
        FileOrder = 0,
        Name,
        BuildCost,
        Capacity,
        Time,
        Price
    };

    /** @brief 배치 파일을 읽은 결과입니다. */
    enum class ParkLayoutState : uint8
    {
        Unread = 0, ///< 아직 읽지 않았다
        Loaded,     ///< 놀이기구가 읽혔다
        Missing,    ///< 활성 팩에 배치 파일이 없다(이 게임은 공원 배치 데이터가 없다)
        Failed      ///< 파일이 있지만 읽지 못했거나 틀렸다
    };

    /** @brief 겹쳐 그릴 선분의 색입니다. 부르는 쪽이 테마 토큰에서 풀어 넘깁니다. */
    struct ParkLayoutColors
    {
        float4 _gate{ 1.0f, 1.0f, 1.0f, 1.0f };
        float4 _track{ 1.0f, 1.0f, 1.0f, 1.0f };
        float4 _selected{ 1.0f, 1.0f, 1.0f, 1.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 놀이기구 하나의 미리보기입니다. */
    struct ParkRidePreview
    {
        string                _name;
        hashed_string         _id;
        vector<float3>        _listTrackPoint; ///< 코스터의 트랙 점(닫힌 회로면 첫 점으로 돌아온다). 평지 놀이기구는 비어 있다
        vector<ParkRideIssue> _listIssue;      ///< 배치 검사가 찾은 문제
        float3                _position{};
        float3                _size{};      ///< 바닥 영역과 높이. 코스터는 스테이션 상자다(`ParkLayoutModel::kStationSize`)
        float3                _entrance{};  ///< 줄 입구(평지 놀이기구만 — 코스터의 입구는 테스트 운행 뒤에 정해진다)
        float3                _boundsMin{}; ///< 트랙까지 담는 범위
        float3                _boundsMax{};
        float4                _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        int32                 _buildCost{ 0 };
        int32                 _capacity{ 0 };
        int32                 _price{ 0 };          ///< 탑승료(평지 놀이기구만 — 코스터는 테스트 운행이 정한다)
        float32               _timeSeconds{ 0.0f }; ///< 평지 놀이기구는 한 번 도는 시간, 코스터는 태우는 시간이다
        bool                  _bCoaster{ false };   ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

        /** @brief 에디터 카메라가 담을 가운데입니다(트랙까지 담는 범위의 가운데 — 코스터 전체가 화면에 든다). */
        float3 computeFocusCenter() const;
        /** @brief 에디터 카메라가 담을 반지름입니다. */
        float32 computeFocusRadius() const;
        /** @brief 바닥 영역이 @p other 와 겹치면 true 입니다. shortcut: 원형 놀이기구도 사각형으로 본다, 모양이 늘면 모양별로 나눈다. */
        bool overlapsFootprint( const ParkRidePreview& other ) const;
        /** @brief 줄 입구가 자기 바닥 영역 안이면 true 입니다. 코스터는 입구가 아직 없어 false 입니다. */
        bool isEntranceInsideFootprint() const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ParkLayoutModel
     * @brief 배치 · 코스터 파일의 글로 미리보기를 짓고, 표(거르기 · 정렬 · 고르기) · 합계 · 배치 검사 · 겹쳐 그릴 선분을 내놓습니다.
     * @details 글이 같으면 다시 읽지 않는다 — 틀린 파일도 마찬가지라 경고는 내용이 바뀔 때마다 한 번이다(예전에는 틀린 파일을 프레임마다 다시 읽고 경고했다).
     *          비교는 코스터 글까지 묶는다(코스터 파일만 고쳐도 트랙이 따라온다). 파일 찾기와 읽기는 서브시스템(`ParkLayoutPreview`)의 일이다.
     */
    class ParkLayoutModel
    {
    public:
        /** @brief 고른 놀이기구가 없거나 찾지 못했을 때의 번호입니다. */
        static constexpr uint32 kNoRide = 0xFFFFFFFFu;
        /** @brief 코스터 스테이션 상자의 크기(m)입니다. 데이터에 없는 표시용 값입니다. */
        static constexpr float3 kStationSize{ 4.0f, 2.0f, 4.0f };

        ParkLayoutModel();

        /**
         * @brief 배치 글 @p layoutText 와 코스터 글 @p coasterText 로 미리보기를 짓습니다. 글이 지난번과 같고 @p bForce 가 아니면 아무것도 하지 않습니다.
         * @return 다시 읽었으면(실패 포함) true 입니다. 결과는 `getState` 입니다.
         */
        bool applyTexts( string_view layoutText, string_view coasterText, string_view layoutPath, string_view coasterPath, bool bForce );
        /** @brief 미리보기를 비우고 활성 팩에 파일이 없는 상태로 둡니다. */
        void setMissing();
        /** @brief 미리보기를 비우고 읽지 못한 상태로 둡니다. */
        void setFailed();

        ParkLayoutState                getState() const { return _state; }
        const vector<ParkRidePreview>& getRides() const { return _listRide; }
        const float3&                  getGatePosition() const { return _gatePosition; }
        int32                          getStartingCash() const { return _startingCash; }
        int32                          getEntryFee() const { return _entryFee; }
        /** @brief 모든 놀이기구의 짓는 값 합입니다. */
        int64 computeTotalBuildCost() const;
        /** @brief 모든 놀이기구에 달린 문제 수입니다. */
        uint32 countIssues() const;

        // --- 표 ---------------------------------------------------------------
        /** @brief @p filter 를 통과한 놀이기구 번호를 @p column 순서(같으면 파일 순서)로 @p outListRide 에 담습니다. */
        void collectRows( const EditorListFilter& filter, ParkRideSortColumn column, bool bDescending, vector<uint32>& outListRide ) const;
        /** @brief 놀이기구 @p rideIndex 를 고릅니다. 범위 밖이면 고름을 비웁니다. */
        void   selectRide( uint32 rideIndex );
        uint32 getSelectedRide() const { return _selectedRide; }

        // --- 씬 뷰 ---------------------------------------------------------------
        /** @brief 바닥 평면에서 @p position 에 가장 가까운 놀이기구 번호입니다. 없으면 `kNoRide` 입니다. 거리는 @p outDistance 에 담습니다. */
        uint32 findNearestRide( const float3& position, float32& outDistance ) const;
        /** @brief 놀이기구 영역, 줄 입구, 입구 십자, 코스터 트랙의 월드 선분을 @p outListSegment 뒤에 붙입니다. 고른 놀이기구는 고른 색입니다. */
        void appendSegments( const ParkLayoutColors& colors, vector<EditorWorldSegment>& outListSegment ) const;

    private:
        /** @brief 읽은 배치로 놀이기구 목록을 다시 짓습니다. 고름은 같은 id · 이름의 놀이기구로 이어 갑니다. */
        void rebuild( const ParkLayout& layout, const CoasterLayoutCatalog& catalog );
        /** @brief 놀이기구마다 배치 문제를 다시 찾습니다. */
        void rebuildIssues();
        void clearPreview( ParkLayoutState state );

    private:
        vector<ParkRidePreview> _listRide;
        float3                  _gatePosition;
        uint64                  _contentHash;  ///< 지난번 글(배치 + 코스터)의 해시
        uint32                  _selectedRide; ///< 고른 놀이기구 번호
        int32                   _startingCash;
        int32                   _entryFee;
        ParkLayoutState         _state;
    };
} // namespace sw::editor
