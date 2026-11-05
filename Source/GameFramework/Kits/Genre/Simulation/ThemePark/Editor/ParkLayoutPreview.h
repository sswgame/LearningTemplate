/**
 * @file ParkLayoutPreview.h
 * @brief ThemePark 배치 파일을 찾아 읽어 모델(`ParkLayoutModel`)에 넘기는 서브시스템입니다(ImGui 에 의존하지 않습니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Viewport/Model/EditorVisualizerGeometry.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/Model/ParkLayoutModel.h"

namespace sw::editor
{
    /**
     * @class ParkLayoutPreview
     * @brief 활성 게임 팩의 공원 배치 · 코스터 파일을 읽어 모델에 넘깁니다. 폴더 감시가 바뀌었다고 알리면 다시 읽습니다.
     * @details 이 확장이 기여하는 에디터 서브시스템이다(`SW_EDITOR_SUBSYSTEM`) — 패널과 시각화가 `editor::findSubsystem<ParkLayoutPreview>()` 로 같이 본다.
     *          프레임워크는 게임 이름을 모른다. 배치 · 코스터 파일은 활성 팩 루트(`GameConfig`)와 데이터 종류(`ParkLayout::kDataKind` ·
     *          `CoasterLayoutCatalog::kDataKind`)로 찾는다(`GamePackDataUtil`). 팩에 배치 파일이 없으면 `isMissing()` 이고 다른 게임의 파일로 대체하지 않는다.
     *          프레임마다 파일을 읽지 않는다 — 언리얼 · 유니티처럼 폴더 감시(`IContentFolderWatch`)의 알림을 기다린다. 이 플랫폼에 감시자가 없으면 그때만 내용을 매번 비교한다.
     */
    class ParkLayoutPreview final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ParkLayoutPreview";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        ParkLayoutPreview();

        /**
         * @brief 처음이거나 파일이 바뀌었으면 다시 읽습니다. 다시 읽었으면(처음, 바뀜, @p bForce) true 를 반환합니다. 읽기에 실패해도 true 이고 결과는 `isLoaded` 입니다.
         * @details 팩에 배치 파일이 없으면 미리보기를 비우고 `isMissing()` 이 된다. 파일이 틀리면 미리보기를 비우고 내용이 바뀔 때마다 한 번 경고한다.
         */
        bool refresh( bool bForce = false );
        /** @brief 모델의 선분을 테마 색으로 @p outListSegment 뒤에 붙입니다. */
        void appendSegments( vector<EditorWorldSegment>& outListSegment ) const;

        const ParkLayoutModel&         getModel() const { return _model; }
        ParkLayoutModel&               editModel() { return _model; }
        const vector<ParkRidePreview>& getRides() const { return _model.getRides(); }
        bool                           isLoaded() const { return _model.getState() == ParkLayoutState::Loaded; }
        /** @brief 활성 게임 팩에 공원 배치 파일이 없습니다(이 게임은 공원 배치 데이터가 없다). */
        bool isMissing() const { return _model.getState() == ParkLayoutState::Missing; }
        /** @brief 마지막으로 찾은 배치 파일 경로입니다. 경로를 만들 수 없었으면 그 이유입니다. */
        const string& getLayoutPath() const { return _layoutPath; }

    private:
        /** @brief 배치 · 코스터 파일이 있는 폴더를 감시합니다. 경로가 그대로면 아무것도 하지 않습니다. */
        void syncWatch( const string& coasterPath );
        void onDataFolderChanged() { _bFolderChanged = SW_TRUE; }

    private:
        ParkLayoutModel _model;
        Subscription    _layoutWatch;  ///< 배치 파일 폴더 감시
        Subscription    _coasterWatch; ///< 코스터 파일 폴더 감시(배치와 같은 폴더면 비어 있다)
        string          _layoutPath;   ///< 활성 팩의 배치 파일(`ParkLayout::kDataKind`)
        string          _watchedLayoutPath;
        string          _watchedCoasterPath;
        uint8           _bFolderChanged : 1; ///< 폴더 감시가 알린 뒤 아직 다시 읽지 않았다
        uint8           _bWarned        : 1; ///< 경로 · 읽기 실패를 알렸다(읽히면 다시 알린다)
    };
} // namespace sw::editor
