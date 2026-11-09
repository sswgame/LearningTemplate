/**
 * @file StreamingCellsViewModel.h
 * @brief 월드 셀 창의 ViewModel — 지난 나누기 미리보기와 그것을 만든 씬 세대를 들고, 창이 그릴 값(셀 · 위반 줄, 격자 설정, 쿠킹본 셀 수)을 모으며(`gather`),
 *        다시 나누기 · 오브젝트 고르기 명령을 받습니다. ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 언리얼 World Partition 창이 셀 데이터(`UWorldPartitionRuntimeHash`)와 Slate 그리기를 나누는 것과 같다.
 *          나누기는 `SceneCooker::previewStreamingCells` 한 길이라 쿠커와 같은 결과다. 셀 상태는 `SceneStreamingSubsystem::update` 의 규칙과 같다.
 *          미리보기와 고르기는 문서 데이터가 아니라 되돌리기에 남지 않는다. 헤더는 씬 헤더를 보지 않는다 — 미리보기 결과는 .cpp 안의 구현이 든다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class CameraComponent;
    class StreamingCellGrid;
    struct SceneSettings;
    struct StreamingCellCoord;
    struct StreamingCellPreview;
} // namespace sw

namespace sw::editor
{
    /** @brief 게임 카메라 자리에서 본 셀의 상태입니다(`SceneStreamingSubsystem::update` 의 규칙). */
    enum class StreamingCellPreviewState : uint8
    {
        Load,   ///< 로딩 반경 안 — 올린다
        Keep,   ///< 반경 밖, 반경 + 여유 안 — 올라 있으면 둔다
        Unload, ///< 반경 + 여유 밖 — 내린다
        NoCamera
    };

    /** @brief 창이 그릴 내용의 종류입니다. */
    enum class StreamingCellsContent : uint8
    {
        NoActiveScene = 0, ///< 편집 중인 씬이 없다
        NotStreaming,      ///< 씬이 스트리밍하지 않는다
        CookedScene,       ///< 쿠킹본으로 지은 씬이다(올라 있는 셀 수만 보인다)
        NoPreview,         ///< 아직 나눠 보지 않았다
        Violations,        ///< 셀을 넘는 참조가 있다(쿠킹이 실패한다)
        Cells,             ///< 셀 표
        GridInvalid        ///< 미리보기가 선 뒤 격자 설정을 고쳐 격자를 만들 수 없다(오류는 로그에)
    };

    /** @brief 셀을 넘는 참조 한 줄입니다(값만). */
    struct StreamingCellViolationRow
    {
        string _fromName{};
        string _toName{};
    };

    /** @brief 셀 한 줄입니다(값만). */
    struct StreamingCellRow
    {
        int32                     _cellX{ 0 };
        int32                     _cellZ{ 0 };
        uint32                    _objectCount{ 0 };
        StreamingCellPreviewState _state{ StreamingCellPreviewState::NoCamera };
    };

    /** @brief 월드 셀 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct StreamingCellsViewState
    {
        StreamingCellsContent             _content{ StreamingCellsContent::NoActiveScene };
        float32                           _cellSize{ 0.0f };
        float32                           _loadingRadius{ 0.0f };
        float32                           _unloadMargin{ 0.0f };
        uint32                            _maxCellLoadsPerFrame{ 0 };
        uint32                            _loadedCellCount{ 0 }; ///< 쿠킹본 씬에서 올라 있는 셀 수
        uint32                            _cellCount{ 0 };       ///< 쿠킹본 씬의 셀 수
        uint32                            _alwaysLoadedCount{ 0 };
        vector<StreamingCellViolationRow> _listViolation{};
        vector<StreamingCellRow>          _listCell{};
        uint8                             _bLastSplitFailed{ SW_FALSE }; ///< 지난 단추가 나누지 못했다(로그를 보라는 줄을 보인다)
        uint8                             _bHasGameCamera{ SW_FALSE };
    };

    /**
     * @class StreamingCellsViewModel
     * @brief 월드 셀 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class StreamingCellsViewModel
    {
    public:
        StreamingCellsViewModel();
        ~StreamingCellsViewModel();

        /** @brief 활성 씬에서 창이 그릴 값을 @p outState 에 채웁니다. 활성 씬이 바뀌었으면 지난 미리보기를 버립니다(다른 씬의 오브젝트 id 다). */
        void gather( StreamingCellsViewState& outState );
        /**
         * @brief 활성 씬을 다시 나눠 봅니다(Preview Split 단추와 같다).
         * @return 나눴으면 true(셀을 넘는 참조가 있어도 true 이고 위반 목록이 찬다). 나누지 못했으면 false 이고 이유는 로그에 있다.
         */
        bool refreshPreview();
        /** @brief 위반 줄 @p rowIndex 의 두 오브젝트(가리키는 쪽 · 가리켜진 쪽)를 고릅니다. */
        void selectViolationObjects( uint32 rowIndex );
        /** @brief 셀 줄 @p rowIndex 의 엔티티를 고릅니다. */
        void selectCellObjects( uint32 rowIndex );
        /** @brief 늘 올라 있는 엔티티를 고릅니다. */
        void selectAlwaysLoadedObjects();

        /** @brief 게임 카메라 @p pCamera 에서 본 셀 @p cell 의 상태입니다. 카메라가 없으면 `NoCamera` 입니다. */
        static StreamingCellPreviewState computeState( const StreamingCellGrid& grid, const SceneSettings& settings, const StreamingCellCoord& cell, const CameraComponent* pCamera );
        /** @brief 상태 열의 글입니다. */
        static const utf8* getStateLabel( StreamingCellPreviewState state );

    private:
        /** @brief @p listObjectID 의 오브젝트를 활성 씬에서 찾아 고릅니다. 없는 id(0 · 지운 것)는 건너뜁니다. */
        static void selectObjects( const vector<uint64>& listObjectID );
        /** @brief 오브젝트 이름입니다. 활성 씬에 없으면 "?" 입니다. */
        static string findObjectName( uint64 objectID );

    private:
        unique_ptr<StreamingCellPreview> _pPreview;               ///< 지난 미리보기(헤더가 씬 쿠커를 보지 않게 앞 선언으로 든다)
        uint64                           _previewSceneGeneration; ///< 미리보기를 만들 때의 `SceneManager::getSceneGeneration`
        uint8                            _bHasPreview  : 1;
        uint8                            _bLastFailed  : 1; ///< 지난 단추가 나누지 못했다
        [[maybe_unused]] uint8           _reservedFlag : 6;
    };
} // namespace sw::editor
