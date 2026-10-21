/**
 * @file CharacterEditorPanel.h
 * @brief 캐릭터 편집 창 패널 — 소켓 파일 편집(목록 · 추가 · 이름 바꾸기 · 변환 · 좌우 대칭 · 저장), 스켈레톤 본 트리와 레퍼런스 포즈 편집, 외형 선택 편집(슬롯 · 세트 입히기 ·
 *        체형 · 꾸미기 값 · 규칙 설명 · 프리셋 파일 저장 · 피팅 확인 표시)과, 그 선택을 입은 몸을 보이는 미리보기 씬.
 * @details 데이터 편집은 모두 `SocketEditModel` · `BonePoseEditModel` · `AppearanceEditModel` 이 하고 이 패널은 그리기와 미리보기 동기화만 한다.
 *          - **문서**: 소켓 파일(`*.sockets.xml`, `EditorAssetType::Data`)이 이 창의 문서다(`EditorDocumentPanel` 계약). 콘텐츠 브라우저에서 소켓 파일을 고르면 이 창이 그 파일로 바뀌고,
 *            고른 것이 없으면 팩 데이터의 소켓 파일을 연다. 저장은 소켓과 레퍼런스 포즈 둘 다(미저장 확인 · 닫기 확인이 같은 계약).
 *          - **미리보기 몸**: 미리보기 씬(`EditorPreviewPanel`)에 몸 오브젝트 하나(`SkeletalMeshComponent` + `CharacterAppearanceComponent`)를 세우고, 외형 선택이 바뀔 때마다
 *            `CharacterAppearanceComponent::previewSelection` 으로 이 창의 외형 데이터(`_database`)를 입힌다. 게임 서비스(메인 씬의 것)는 보지 않는다.
 *            선택에 기준 프리셋이 없으면 팩 데이터의 `previewPreset` 위에 입힌다. 외형 데이터를 읽기 전에는 몸이 없다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/EditorPreviewPanel.h"

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

    class CharacterAppearanceComponent;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class CharacterEditorPanel
     * @brief 캐릭터 편집 확장의 도구 패널입니다.
     */
    class CharacterEditorPanel final : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 열기(`EditorPanelManager::setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "charactereditor.main";

        CharacterEditorPanel();

        const utf8* getPanelTitle() const override { return "Character Editor"; }
        /** @brief 외형 탭은 데이터 줄 · 내보내기 · 선택 · 슬롯 · 꾸미기 값을 한 화면에 둔다 — 기본 크기(640x420)는 슬롯 줄이 창 밖에 남는다. */
        float2             getInitialPanelSize() const override { return float2{ 760.0f, 900.0f }; }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;
        bool               canSaveDocument() const override { return isDocumentLoadFailed() == false && ( _sockets.isDirty() || _pose.isDirty() ); }
        /** @brief 미리보기 몸 오브젝트입니다. 외형 데이터를 읽기 전이거나 씬이 없으면 무효입니다(자동화 · 테스트). */
        GameObjectHandle getPreviewBody() const { return _previewBody; }

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
        /** @brief 소켓 파일(`*.sockets.xml`)만 문서로 받습니다. */
        bool acceptsDocumentPath( string_view assetPath ) const override;
        /** @brief 문서(소켓 파일)를 읽습니다. 포커스한 소켓 파일이 있으면 그것, 아니면 경로 칸(팩 데이터의 소켓 파일)입니다. */
        ToolAssetLoadResult loadDocument() override;
        /** @brief 미리보기 씬을 만든 직후 — 카메라를 몸 높이에 맞추고 몸 핸들을 비웁니다(다음 그리기의 `syncPreviewBody` 가 세운다). */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 미리보기 탭(뷰포트)을 그립니다. 외형 데이터를 읽기 전이면 안내 글입니다. */
        void drawPreviewTab();
        /** @brief 몸 오브젝트를 세우고, 외형 선택이나 외형 데이터가 바뀌었으면 다시 입힙니다. */
        void syncPreviewBody();
        /** @brief 뷰포트 위에 소켓 파일의 소켓 축(몸이 조립한 자리)을 긋습니다. 고른 소켓은 길고 밝다. */
        void drawSocketAxes( const CharacterAppearanceComponent& appearance );
        /** @brief 미리보기 씬의 매니저입니다. 씬이 없으면 nullptr 입니다. */
        GameObjectManager* findPreviewManager() const;
        /** @brief 소켓 파일을 경로 칸(`_path`)에서 읽습니다. 성공하면 true 이고 선택 · 외부 변경 표시를 비웁니다. */
        [[nodiscard]] bool loadSocketFile();
        void               drawFileRow();
        void               drawSocketTab();
        void               drawSocketList();
        void               drawSocketDetail( const SocketDef& socket );
        void               drawSkeletonTab();
        void               drawBoneNode( uint32 boneIndex );
        void               drawBoneDetail();
        void               drawAppearanceTab();
        void               drawAppearanceDataRow();
        void               drawSelectionHeader( const AppearanceSelection& selection );
        void               drawSlotRows( const AppearanceSelection& selection );
        void               drawCustomizationRows( const AppearanceSelection& selection );
        void               drawExplanation();
        void               drawFitCheck();
        void               drawPresetRow();
        void               drawDataPresetRow();
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
        string                                _packRoot;                ///< 경로 칸을 채운 활성 게임 팩 루트 — 바뀌면 다시 읽는다
        string                                _packDataPath;            ///< 활성 팩의 캐릭터 편집 데이터 파일
        string                                _packDataMessage;         ///< `Invalid` 의 이유
        hashed_string                         _previewPresetID;         ///< 팩 데이터의 미리보기 몸 프리셋(선택에 기준 프리셋이 없을 때)
        GameObjectHandle                      _previewBody;             ///< 미리보기 씬의 몸 오브젝트
        uint32                                _previewBodyRevision;     ///< 몸에 마지막으로 입힌 외형 선택의 세대(`AppearanceEditModel::getRevision`)
        uint32                                _previewDatabaseRevision; ///< 몸에 마지막으로 입힌 외형 데이터의 세대(`AppearanceDatabase::getRevision`)
        bool                                  _bPreviewBodyDressed;     ///< 몸에 한 번이라도 입혔다 — 세대가 같아도 첫 입히기는 한다
        bool                                  _bModelsWereDirty;        ///< 지난 그리기에 소켓 · 포즈 모델이 미저장이었다(되돌리기로 저장 상태에 돌아오면 dirty 를 내린다)
        PackDataState                         _packDataState;
    };
} // namespace sw::editor
