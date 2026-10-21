/**
 * @file CharacterEditorPanel.h
 * @brief 캐릭터 편집 창 패널 — 소켓 파일 편집(목록 · 추가 · 이름 바꾸기 · 변환 · 좌우 대칭 · 저장), 스켈레톤 본 트리와 레퍼런스 포즈 편집, 외형 선택 편집(슬롯 · 세트 입히기 ·
 *        체형 · 꾸미기 값 · 규칙 설명 · 프리셋 파일 저장 · 피팅 확인 표시). 미리보기 씬이 없다.
 * @details 데이터 편집은 모두 `SocketEditModel` · `BonePoseEditModel` · `AppearanceEditModel` 이 하고 이 패널은 그리기만 한다. 화면 전체 틀(미리보기 씬 · 뷰포트 기즈모 · 문서 패널 계약)은
 *          `EditorMultiWorld` 단계가 끝난 뒤 이어 붙인다(`docs/plans/EditorCharacterEditor.md` 단계 1).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/UserAppearancePresetStore.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/AppearanceEditModel.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/BonePoseEditModel.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/FitCheckSummary.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/SaveReloadGuard.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/SocketEditModel.h"

namespace sw
{
    struct FitResult;
} // namespace sw

namespace sw::editor
{
    /**
     * @class CharacterEditorPanel
     * @brief 캐릭터 편집 확장의 도구 패널입니다.
     */
    class CharacterEditorPanel final : public IEditorPanel
    {
    public:
        CharacterEditorPanel();

        const utf8* getPanelTitle() const override { return "Character Editor"; }
        bool        isToolPanel() const override { return true; }
        /** @brief 외형 탭은 데이터 줄 · 내보내기 · 선택 · 슬롯 · 꾸미기 값을 한 화면에 둔다 — 기본 크기(640x420)는 슬롯 줄이 창 밖에 남는다. */
        float2             getInitialPanelSize() const override { return float2{ 760.0f, 900.0f }; }
        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;
        bool               canSaveDocument() const override { return _sockets.isDirty() || _pose.isDirty(); }

        SocketEditModel&     getSocketModel() { return _sockets; }
        BonePoseEditModel&   getPoseModel() { return _pose; }
        AppearanceEditModel& getAppearanceModel() { return _appearance; }
        /** @brief 푼 피팅 결과를 확인 표시에 올립니다. 풀어서 넘기는 쪽(미리보기 씬, 슬라이더를 놓을 때 한 번)이 부릅니다. */
        void setFitResult( const FitResult& result );

    private:
        /**
         * @brief 소켓 · 포즈 모델의 미저장 상태를 패널의 dirty 로 옮깁니다(닫기 확인 · 종료 확인 · 제목 표시가 이 값을 본다).
         * @details 편집 기록이 모델 안에 있어 패널이 편집마다 알리지 않는다 — 그리기마다 모델을 정본으로 다시 맞춘다(`DataTablePanel` 과 같은 모양).
         */
        void syncDocumentDirty() override;
        /** @brief "Don't Save" — 두 모델을 마지막으로 읽거나 저장한 상태로 돌립니다. 외형 선택은 파일 문서가 아니라(프리셋 저장은 따로 누른다) 건드리지 않습니다. */
        void revertDocument() override;
        void drawFileRow();
        void drawSocketTab();
        void drawSocketList();
        void drawSocketDetail( const SocketDef& socket );
        void drawSkeletonTab();
        void drawBoneNode( uint32 boneIndex );
        void drawBoneDetail();
        void drawAppearanceTab();
        void drawAppearanceDataRow();
        void drawSelectionHeader( const AppearanceSelection& selection );
        void drawSlotRows( const AppearanceSelection& selection );
        void drawCustomizationRows( const AppearanceSelection& selection );
        void drawExplanation();
        void drawFitCheck();
        void drawPresetRow();
        void drawDataPresetRow();
        /**
         * @brief 지금 선택을 데이터 프리셋 에셋(`<폴더>/<id>.appearance.xml`)으로 쓰고, 이 창과 실행 중인 게임의 외형 데이터에 올립니다.
         * @details 씬의 `CharacterAppearanceComponent` 가 그 id 를 가리키면 조립한다(언리얼 · 유니티의 프리셋 에셋). 세이브 형식(`Save as preset`)과 다른 길이다.
         */
        void exportDataPreset();
        void drawHistoryRow();
        void ensureKindTable();
        /**
         * @brief 활성 게임 팩의 캐릭터 편집 데이터(`CharacterEditorPackData`)를 경로 칸에 채웁니다. 활성 팩이 바뀌었을 때만 다시 읽습니다.
         * @details 팩에 파일이 없으면 `Missing`, 못 읽으면 `Invalid` 이고, 창은 탭 대신 그 상태를 보인다(다른 게임의 경로로 대체하지 않는다).
         */
        void ensurePackData();
        void loadSkeleton();
        void loadAppearanceData();
        /** @brief 모델 편집 결과를 상태 줄에 보입니다. 실패하면 이유를, 성공하면 빈 줄을 둡니다. */
        void reportEdit( bool bSucceeded );

        /** @brief 활성 게임 팩의 캐릭터 편집 데이터 상태입니다. */
        enum class PackDataState : uint8
        {
            Unread,  ///< 아직 읽지 않았다
            Missing, ///< 이 게임 팩에는 캐릭터 편집 데이터가 없다
            Invalid, ///< 경로를 만들 수 없거나 파일을 읽지 못했다(`_packDataMessage`)
            Ready,   ///< 경로 칸을 채웠다
        };

        SocketEditModel                       _sockets;
        BonePoseEditModel                     _pose;
        AppearanceEditModel                   _appearance;
        SaveReloadGuard                       _reloadGuard;
        FitCheckSummary                       _fit;
        unique_ptr<ItemCatalog>               _pItems;
        AppearanceDatabase                    _database;
        UserAppearancePresetStore             _presetStore;
        fixed_string<constant::kMaxPathSize>  _path;
        fixed_string<constant::kMaxPathSize>  _skeletonPath;
        fixed_string<constant::kMaxPathSize>  _refPosePath;
        fixed_string<constant::kMaxPathSize>  _itemsPath;
        fixed_string<constant::kMaxPathSize>  _appearanceFolder;
        fixed_string<constant::kMaxPathSize>  _presetPath;
        fixed_string<constant::kMaxBuffer128> _presetName;
        fixed_string<constant::kMaxPathSize>  _exportFolder; ///< 데이터 프리셋을 쓸 폴더(리소스 id). 기본은 외형 폴더의 `presets/`
        fixed_string<constant::kMaxBuffer128> _exportID;     ///< 데이터 프리셋 id — 씬의 컴포넌트가 가리키는 이름
        fixed_string<constant::kMaxBuffer128> _newName;
        fixed_string<constant::kMaxBuffer128> _renameBuffer;
        string                                _status;
        hashed_string                         _selectedSocket;
        hashed_string                         _selectedBone;
        bool                                  _bKindTableReady;
        bool                                  _bDatabaseLoaded;
        bool                                  _bHasFit;
        bool                                  _bExternalChange;
        ResolvedAppearance                    _preview;       ///< 입혀 본 결과(규칙 설명). 선택이 바뀔 때만 다시 푼다
        AppearanceSelectionReport             _previewReport; ///< `_preview` 와 같이 푼 보고
        uint32                                _previewRevision;
        bool                                  _bPreviewValid;
        string                                _packRoot;        ///< 경로 칸을 채운 활성 게임 팩 루트 — 바뀌면 다시 읽는다
        string                                _packDataPath;    ///< 활성 팩의 캐릭터 편집 데이터 파일
        string                                _packDataMessage; ///< `Invalid` 의 이유
        PackDataState                         _packDataState;
    };
} // namespace sw::editor
