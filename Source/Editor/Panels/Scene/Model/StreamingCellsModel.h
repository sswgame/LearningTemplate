/**
 * @file StreamingCellsModel.h
 * @brief 월드 셀 창의 모델 — 지난 나누기 미리보기와 그것을 만든 씬 세대, 다시 나누기 · 셀 상태 판정 · 오브젝트 고르기. ImGui 를 모릅니다.
 * @details 패널(`StreamingCellsPanel`)은 표 · 단추를 그리고 누름을 이 모델에 넘긴다 — 인스펙터 · 계층 `Model/` 과 같은 선이다
 *          (언리얼 World Partition 창이 셀 데이터(`UWorldPartitionRuntimeHash`)와 Slate 그리기를 나누는 것과 같다).
 *          나누기는 `SceneCooker::previewStreamingCells` 한 길이라 쿠커와 같은 결과다. 셀 상태는 `SceneStreamingSubsystem::update` 의 규칙과 같다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"

#include "Engine/Scene/SceneCooker.h"

namespace sw
{
    class CameraComponent;
    class StreamingCellGrid;
    struct SceneSettings;
    struct StreamingCellCoord;
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

    /**
     * @class StreamingCellsModel
     * @brief 월드 셀 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class StreamingCellsModel
    {
    public:
        StreamingCellsModel();

        /**
         * @brief 활성 씬을 다시 나눠 봅니다(Preview Split 단추와 같다).
         * @return 나눴으면 true(셀을 넘는 참조가 있어도 true 이고 위반 목록이 찬다). 나누지 못했으면 false 이고 이유는 로그에 있다.
         */
        bool refreshPreview();
        /** @brief 미리보기를 만든 씬 세대가 @p sceneGeneration 과 다르면 버립니다 — 다른 씬의 결과는 그 씬의 오브젝트 id 라 쓸 수 없다. */
        void discardIfSceneChanged( uint64 sceneGeneration );

        /** @brief 지난 미리보기입니다. `hasPreview` 가 false 이면 비어 있습니다. */
        const StreamingCellPreview& getPreview() const { return _preview; }
        /** @brief 지금 활성 씬의 미리보기가 있으면 true 입니다. */
        bool hasPreview() const { return _bHasPreview == SW_TRUE; }
        /** @brief 지난 단추가 나누지 못했으면 true 입니다(로그를 보라는 줄을 보인다). */
        bool didLastSplitFail() const { return _bLastFailed == SW_TRUE; }

        /** @brief 게임 카메라 @p pCamera 에서 본 셀 @p cell 의 상태입니다. 카메라가 없으면 `NoCamera` 입니다. */
        static StreamingCellPreviewState computeState( const StreamingCellGrid& grid, const SceneSettings& settings, const StreamingCellCoord& cell, const CameraComponent* pCamera );
        /** @brief 상태 열의 글입니다. */
        static const utf8* getStateLabel( StreamingCellPreviewState state );
        /** @brief @p listObjectID 의 오브젝트를 활성 씬에서 찾아 고릅니다. 없는 id(0 · 지운 것)는 건너뜁니다. */
        static void selectObjects( const vector<uint64>& listObjectID );
        /** @brief 오브젝트 이름입니다. 활성 씬에 없으면 "?" 입니다. */
        static string findObjectName( uint64 objectID );

    private:
        StreamingCellPreview   _preview;
        uint64                 _previewSceneGeneration; ///< 미리보기를 만들 때의 `SceneManager::getSceneGeneration`
        uint8                  _bHasPreview  : 1;
        uint8                  _bLastFailed  : 1; ///< 지난 단추가 나누지 못했다(로그를 보라는 줄을 보인다)
        [[maybe_unused]] uint8 _reservedFlag : 6;
    };
} // namespace sw::editor
