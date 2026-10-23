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
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/EditorPreviewPanel.h"
#include "Editor/Viewport/EditorPreviewGizmo.h"

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
    class GameObject;
    class GameObjectManager;
    class SkeletalAnimatorComponent;
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
         * @details 편집 기록이 모델 안에 있어 패널이 편집마다 알리지 않는다 — 그리기마다 모델을 기준으로 다시 맞춘다(`DataTablePanel` 과 같은 모양).
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
        /** @brief 기본 정책에 플레이를 더합니다 — 몸의 애니메이터가 `onBeginPlay` 에서 스켈레톤 유닛에 붙어야 클립이 포즈를 낸다. */
        ScenePreviewPolicy getPreviewPolicy() const override;
        /** @brief 미리보기 탭(뷰포트)을 그립니다. 외형 데이터를 읽기 전이면 안내 글입니다. */
        void drawPreviewTab();
        /**
         * @brief 미리보기 몸의 애니메이션 재생 줄을 그립니다 — 클립 콤보(팩 데이터의 클립 폴더), 반복, Play · Stop.
         * @details 재생 중이면 미리보기 씬의 틱이 포즈를 낸다. 몸을 다시 입혀도(체형 · 장비 변경) 재생은 이어진다 — 스켈레톤이 같은 외형이면 같은 클립이 그대로 맞는다.
         */
        void drawAnimationControls( SkeletalAnimatorComponent& animator );
        /** @brief 팩 데이터의 클립 폴더에서 클립 이름(`.animclip` 파일 이름)을 모읍니다. 폴더가 디스크에 없으면 목록이 빕니다(팩만 있는 배포본). */
        void loadClipNames();
        /** @brief 몸 오브젝트를 세우고, 외형 선택이나 외형 데이터가 바뀌었으면 다시 입힙니다. */
        void syncPreviewBody();
        /** @brief 뷰포트 위에 소켓 파일의 소켓 축(몸이 조립한 자리)을 긋습니다. 고른 소켓은 길고 밝다. */
        void drawSocketAxes( const CharacterAppearanceComponent& appearance );
        /**
         * @brief 스켈레톤 탭에서 고른 본(`_selectedBone`)의 기즈모를 미리보기 씬에 놓고, 기즈모로 옮긴 만큼을 레퍼런스 포즈 덮어쓰기(`BonePoseEditModel::setTranslation` · `setRotationDegrees`)로 바꿉니다.
         * @details 표시 오브젝트(SceneComponent 하나)를 본의 월드 자리(몸 월드 × 덮어쓴 모델 변환)에 놓고 창 선택에 올린다. 다음 그리기에 그 월드 변환이 놓은 값과 다르면
         *          기즈모가 옮긴 것이다 — 부모 월드의 역으로 로컬을 구해 모델에 넣는다(끄는 동안은 한 되돌리기 단계). 본을 고르지 않았으면 표시 오브젝트를 지운다.
         */
        void syncBoneGizmo();
        /** @brief 피팅이 찾은 관통 점(`FitPenetration::_listPoint`, 바인드 공간)을 몸의 월드 변환으로 옮겨 빨간 십자로 긋습니다. 관통이 없으면 아무것도 긋지 않습니다. */
        void drawPenetrationPoints( const GameObject& body );
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
        /**
         * @brief 꾸미기 스키마(`CustomizationSchema`)의 매개변수마다 슬라이더 · 색 · 고르기를 그립니다 — 카테고리(체형 · 얼굴 · 염색 …)로 가르고, 조건이 맞지 않는 매개변수는 숨깁니다.
         * @details 끄는 동안(`_bCustomizationActive`)은 미리보기 몸을 다시 입히지 않는다 — 피팅은 무거워서 놓을 때 한 번 푼다(설계 문서 단계 5). 스키마는 선택의 것(`_schema`)이고,
         *          비었으면 기준 프리셋의 것이다. 스키마가 없으면(데이터를 읽기 전, 프리셋에 스키마가 없음) 이미 있는 값만 숫자로 보인다(`drawCustomizationRows`).
         */
        void drawCustomizationSliders( const AppearanceSelection& selection );
        /** @brief 미리보기 몸의 지금 모습을 이미지 파일로 쓰고 같은 이름의 프리셋 저장소 항목에 경로를 겁니다. 프리셋을 먼저 저장해야 합니다. */
        void saveThumbnail();
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
        fixed_string<constant::kMaxPathSize>  _clipFolder; ///< 미리보기 몸 애니메이터의 클립 폴더(팩 데이터의 `clipFolder`)
        fixed_string<constant::kMaxPathSize>  _presetPath;
        fixed_string<constant::kMaxBuffer128> _presetName;
        fixed_string<constant::kMaxPathSize>  _exportFolder; ///< 데이터 프리셋을 쓸 폴더(리소스 id). 기본은 외형 폴더의 `presets/`
        fixed_string<constant::kMaxBuffer128> _exportID;     ///< 데이터 프리셋 id — 씬의 컴포넌트가 가리키는 이름
        fixed_string<constant::kMaxBuffer128> _newName;
        fixed_string<constant::kMaxBuffer128> _renameBuffer;
        string                                _status;
        hashed_string                         _selectedSocket;
        hashed_string                         _selectedBone;
        vector<hashed_string>                 _listClipName;     ///< 클립 폴더의 클립 이름들(콤보 목록, 이름순)
        hashed_string                         _selectedClip;     ///< 콤보에서 고른 클립
        bool                                  _bClipLoop;        ///< 고른 클립을 반복 재생하는지
        bool                                  _bClipNamesLoaded; ///< 클립 이름을 한 번 모았다(팩 데이터가 바뀌면 다시 모은다)
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
        bool                                  _bCustomizationActive;    ///< 꾸미기 슬라이더를 끄는 중이다(`drawCustomizationSliders`) — 미리보기 몸 다시 입히기를 놓을 때까지 미룬다
        uint32                                _seenFitRevision;         ///< 확인 표시에 올린 피팅 결과의 세대(`CharacterAppearanceComponent::getFitResultRevision`)
        EditorPreviewGizmo                    _gizmo;                   ///< 고른 본의 기즈모(`syncBoneGizmo`)
        GameObjectHandle                      _boneGizmoTarget;         ///< 기즈모가 잡는 미리보기 씬의 표시 오브젝트 — 고른 본의 월드 자리에 놓는다
        float4x4                              _placedBoneWorld;         ///< 지난 그리기에 표시 오브젝트를 놓은 월드 변환 — 달라졌으면 기즈모가 옮긴 것이다
        bool                                  _bBoneGizmoPlaced;        ///< `_placedBoneWorld` 가 유효하다
        PackDataState                         _packDataState;
    };
} // namespace sw::editor
