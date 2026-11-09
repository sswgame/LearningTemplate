/**
 * @file CharacterEditorPanel.h
 * @brief 캐릭터 편집 창 패널 — 소켓 파일 편집(목록 · 추가 · 이름 바꾸기 · 변환 · 좌우 대칭 · 저장), 스켈레톤 본 트리와 레퍼런스 포즈 편집, 외형 선택 편집(슬롯 · 세트 입히기 ·
 *        체형 · 꾸미기 값 · 규칙 설명 · 프리셋 파일 저장 · 피팅 확인 표시)과, 그 선택을 입은 몸을 보이는 미리보기 씬.
 * @details 상태와 명령(팩 데이터 · 파일 읽기 · 저장 · 프리셋 · 내보내기 · 고른 것)은 모델(`CharacterEditorModel`, 편집 모델 셋을 든다)이 하고 이 패널은 그리기와 미리보기 동기화만 한다.
 *          - **문서**: 소켓 파일(`*.sockets.xml`, 종류 `EditorAssetTypeIDs::kData`)이 이 창의 문서다(`EditorDocumentPanel` 계약). 콘텐츠 브라우저에서 소켓 파일을 고르면 이 창이 그 파일로 바뀌고,
 *            고른 것이 없으면 팩 데이터의 소켓 파일을 연다. 저장은 소켓과 레퍼런스 포즈 둘 다(미저장 확인 · 닫기 확인이 같은 계약).
 *          - **미리보기 몸**: 미리보기 씬(`EditorPreviewPanel`)에 몸 오브젝트 하나(`SkeletalMeshComponent` + `CharacterAppearanceComponent`)를 세우고, 외형 선택이 바뀔 때마다
 *            `CharacterAppearanceComponent::previewSelection` 으로 이 창의 외형 데이터(모델의 `AppearanceDatabase`)를 입힌다. 게임 서비스(메인 씬의 것)는 보지 않는다.
 *            선택에 기준 프리셋이 없으면 팩 데이터의 `previewPreset` 위에 입힌다. 외형 데이터를 읽기 전에는 몸이 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Viewport/EditorPreviewGizmo.h"

#include "Engine/Foundation/GameObjectHandle.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodyCustomizationViewModel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorModel.h"

namespace sw
{
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

        /** @brief 자동화 감시 값이 읽는 값입니다. 패널이 그릴 때마다 게시합니다(게임 스레드). 값은 이 패널 인스턴스의 것이라 에디터마다 따로이고 패널과 함께 사라집니다. */
        struct Published
        {
            uint32  _boneCount{ 0 };
            uint32  _socketCount{ 0 };
            uint32  _equippedSlotCount{ 0 };
            uint32  _presetCount{ 0 };
            uint32  _exportCount{ 0 };            ///< 이 패널이 데이터 프리셋으로 내보낸 수
            uint32  _previewAssembleCount{ 0 };   ///< 미리보기 몸을 조립한 수(미리보기 탭을 그릴 때 게시)
            uint32  _clipPlaying{ 0 };            ///< 미리보기 몸이 고른 클립을 재생 중이면 1(미리보기 탭을 그릴 때 게시)
            uint32  _penetrationMarkCount{ 0 };   ///< 지난 그리기에 뷰포트에 찍은 관통 점 수
            uint32  _previewPickVertexCount{ 0 }; ///< 집기 오브젝트(몸 + 부품) 메시의 정점 수 합(미리보기 탭을 그릴 때 게시 — 부품이 빠지거나 몸이 다시 구워지면 바뀐다)
            uint32  _bodyPickCount{ 0 };          ///< 뷰포트에서 몸 · 부품 표면을 맞힌 집기 수(층 배치 · 구간 고르기)
            uint32  _boneGizmoPlaced{ 0 };        ///< 고른 본에 기즈모 표식을 놓았으면 1
            uint32  _socketGizmoPlaced{ 0 };      ///< 고른 소켓에 기즈모 표식을 놓았으면 1
            float32 _selectedSocketX{ 0.0f };     ///< 고른 소켓의 로컬 위치 x(m) — 시나리오가 기즈모 끌기 · 되돌리기를 본다
            uint32  _statusSet{ 0 };              ///< 상태 줄에 글(오류 · 진행)이 있으면 1 — 시나리오가 편집 오류가 남지 않았는지 본다
            uint32  _bodyLayerCount{ 0 };         ///< 외형 선택의 피부 층 수(몸 탭을 그릴 때 게시)
            uint32  _bodyHiddenSlotCount{ 0 };    ///< 플레이어가 숨긴 슬롯 수(몸 탭을 그릴 때 게시)
            uint32  _bodyDataDirty{ 0 };          ///< 몸 데이터 파일(채널 매핑 · 프로필 · 구간)에 저장하지 않은 편집이 있으면 1
        };

        CharacterEditorPanel();

        const utf8* getPanelTitle() const override { return "Character Editor"; }
        /** @brief 외형 탭은 데이터 줄 · 내보내기 · 선택 · 슬롯 · 꾸미기 값을 한 화면에 둔다 — 기본 크기(640x420)는 슬롯 줄이 창 밖에 남는다. */
        EditorTextExtent   getInitialPanelSize() const override { return EditorTextExtent::ofText( 101.0f, 47.0f ); }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;
        bool               canSaveDocument() const override { return isDocumentLoadFailed() == false && _document.isDirty(); }
        /** @brief 미리보기 몸 오브젝트입니다. 외형 데이터를 읽기 전이거나 씬이 없으면 무효입니다(자동화 · 테스트). */
        GameObjectHandle getPreviewBody() const { return _previewBody; }

        /** @brief 창의 모델(상태와 명령)입니다. 피팅 결과도 모델에 올린다(`CharacterEditorModel::setFitResult`). */
        CharacterEditorModel&       getDocument() { return _document; }
        const CharacterEditorModel& getDocument() const { return _document; }
        SocketEditModel&            getSocketModel() { return _document.getSocketModel(); }
        BonePoseEditModel&          getPoseModel() { return _document.getPoseModel(); }
        AppearanceEditModel&        getAppearanceModel() { return _document.getAppearanceModel(); }
        const Published&            getPublished() const { return _published; }
        /** @brief 요청한 썸네일 캡처를 아직 거두지 않았으면 true 입니다 — 자동화의 끝 차단(`CharacterEditorThumbnail`)이 캡처 · 저장 사이 틈을 진행 중인 일로 본다. */
        [[nodiscard]] bool isThumbnailCapturePending() const { return _thumbnailCapturePath.empty() == false; }

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
        /** @brief 맞춤(F)과 창을 처음 열 때의 카메라가 담을 구입니다. */
        bool findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const override;
        /** @brief 기본 정책에 플레이를 더합니다 — 몸의 애니메이터가 `onBeginPlay` 에서 스켈레톤 유닛에 붙어야 클립이 포즈를 낸다. */
        ScenePreviewPolicy getPreviewPolicy() const override;
        /** @brief 미리보기 탭(뷰포트)을 그립니다. 외형 데이터를 읽기 전이면 안내 글입니다. */
        void drawPreviewTab();
        /**
         * @brief 미리보기 몸의 애니메이션 재생 줄을 그립니다 — 클립 콤보(팩 데이터의 클립 폴더), 반복, Play · Stop.
         * @details 재생 중이면 미리보기 씬의 틱이 포즈를 낸다. 몸을 다시 입혀도(체형 · 장비 변경) 재생은 이어진다 — 스켈레톤이 같은 외형이면 같은 클립이 그대로 맞는다.
         */
        void drawAnimationControls( SkeletalAnimatorComponent& animator );
        /** @brief 몸 오브젝트를 세우고, 외형 선택이나 외형 데이터가 바뀌었으면 다시 입힙니다. */
        void syncPreviewBody();
        /** @brief 뷰포트 위에 소켓 파일의 소켓 축(몸이 조립한 자리)을 긋습니다. 고른 소켓은 길고 밝다. */
        void drawSocketAxes( const CharacterAppearanceComponent& appearance );
        /**
         * @brief 미리보기 기즈모를 그 주인(`CharacterEditorModel::getGizmoTarget` — 마지막에 고른 본 · 소켓)의 월드 자리에 놓고, 기즈모로 옮긴 만큼을 주인의 데이터로 바꿉니다.
         * @details 표시 오브젝트(SceneComponent 하나)를 주인의 자리에 놓고 창 선택에 올린다. 다음 그리기에 그 월드 변환이 놓은 값과 다르면 기즈모가 옮긴 것이다 —
         *          부모 월드의 역으로 로컬을 구해 모델에 넣는다(`applyGizmoWorld`, 끄는 동안은 한 되돌리기 단계). 프리뷰 오브젝트만 움직이지 않는다 — 다음 그리기는 모델 값으로 다시 놓는다.
         *          주인이 없거나 자리를 못 구하면 표시 오브젝트를 지운다.
         */
        void syncPreviewGizmo();
        /** @brief 고른 본의 월드 자리(몸 월드 × 덮어쓴 모델 변환)와 그 부모의 월드 자리입니다. 본을 못 찾으면 false 입니다. */
        [[nodiscard]] bool findBoneGizmoFrame( const float4x4& bodyWorld, float4x4& outWorld, float4x4& outParentWorld ) const;
        /** @brief 고른 소켓의 월드 자리(로컬 × 부모 본 × 몸 월드, 지금 포즈)와 그 부모의 월드 자리입니다. 소켓 · 부모를 못 찾으면 false 입니다. */
        [[nodiscard]] bool findSocketGizmoFrame( const GameObject& body, float4x4& outWorld, float4x4& outParentWorld ) const;
        /** @brief 기즈모가 옮긴 월드 자리 @p world 를 @p target 의 데이터로 씁니다 — 본은 레퍼런스 포즈 덮어쓰기, 소켓은 소켓 파일의 부모 기준 로컬 변환입니다. */
        void applyGizmoWorld( CharacterGizmoTarget target, const float4x4& world, const float4x4& parentWorld );
        /** @brief 자동화가 기즈모 가운데를 끌 수 있게 그 화면 자리를 표시(`charactereditor.gizmoCenter`)로 남깁니다. */
        void recordGizmoMark();
        /** @brief 피팅이 찾은 관통 점(`FitPenetration::_listPoint`, 바인드 공간)을 몸의 월드 변환으로 옮겨 빨간 십자로 긋습니다. 관통이 없으면 아무것도 긋지 않습니다. */
        void drawPenetrationPoints( const GameObject& body );
        /**
         * @brief 제약 리그 미리보기를 몸에 맞춥니다 — 켜 두었으면 몸 오브젝트에 후처리 리그(`PoseModifierComponent`)를 얹어 팩 데이터의 리그를 돌리고(스트라이드 워핑 · 양손 IK 같은 것이 클립 위에 걸린다),
         *        껐으면 리그를 끈다. 리그 경로가 없는 게임(`CharacterEditorPaths::_rigPath` 가 빔)에서는 부르지 않는다.
         */
        void syncRigPreview( GameObject& body );
        /** @brief 미리보기 씬의 매니저입니다. 씬이 없으면 nullptr 입니다. */
        GameObjectManager* findPreviewManager() const;
        void               drawFileRow();
        void               drawSocketTab();
        void               drawSocketList();
        void               drawSocketIssues(); ///< 소켓 표의 검사 결과(이름 겹침 · 없는 부모 …)
        void               drawSocketDetail( const SocketDef& socket );
        void               drawSkeletonTab();
        void               drawBoneNode( uint32 boneIndex );
        void               drawBoneDetail();
        void               drawAppearanceTab();
        /** @brief 몸 탭(슬롯 숨김 · 몸 채널과 매핑 · 염색 구간 · 피부 층 · 몸 동작 미리보기)을 그립니다 — ViewModel(`BodyCustomizationViewModel`)이 모은 상태만 읽는다. */
        void drawBodyTab();
        void drawSkinLayerRows( const BodyCustomizationViewState& state );
        void drawBodyDynamicsPreview( const BodyCustomizationViewState& state, GameObject* pBody );
        /** @brief 몸 데이터 파일의 저장 · 되돌리기 줄입니다(채널 매핑 · 프로필 · 구간 세 파일). */
        void drawBodyDataRow();
        /** @brief 채널 표 — 값 · 매핑과, 매핑된 채널의 축 범위 편집 · 매핑이 없는 채널의 축 더하기 · 받지 않음 선언(`*.bodyshape.xml`). */
        void drawBodyChannelRows( const BodyCustomizationViewState& state );
        /** @brief 구간 표 — 별칭 · 묶음 · 염색 여부 편집, 뷰포트에서 집기, 드라이브 강조(`itemvisuals.xml`). */
        void drawBodySectionRows( const BodyCustomizationViewState& state );
        /** @brief 몸 프로필 편집 — 지글 물성 · 주름 · 체액 경로(`bodyprofiles.xml`). */
        void drawBodyProfileRows( const BodyCustomizationViewState& state );
        /**
         * @brief 미리보기 탭의 뷰포트에서 몸 표면 집기를 받습니다 — 구간 고르기(별칭 대응)와 배치 층 옮기기(끄는 동안 한 되돌리기 단계), 강조 구간 · 집은 자리 표시.
         * @details 몸 탭에서 집기를 켜고 미리보기 탭에서 누른다(뷰포트는 한 탭이다). 기즈모가 마우스를 쥐었으면 받지 않는다. Esc 가 끈다.
         *          집기 · 윤곽은 몸과 부품 오브젝트(`collectPickObjects`)를 지금 포즈로 스키닝한 표면(`BodyPickSurface`)으로 한다. 구간 고르기는 부품까지, 층 배치는 몸 피부만 본다.
         */
        void drawBodyPickOverlay( GameObject* pBody );
        /** @brief 집기 오브젝트 — 몸(첫 것)과 외형 조립이 스폰한 부품 오브젝트(옷 · 머리카락 · 소켓 부품)입니다. */
        void collectPickObjects( const GameObject& body, vector<const GameObject*>& outListObject ) const;
        void drawAppearanceDataRow();
        void drawSelectionHeader( const AppearanceSelection& selection );
        void drawSlotRows( const AppearanceSelection& selection );
        void drawCustomizationRows( const AppearanceSelection& selection );
        /**
         * @brief 꾸미기 스키마(`CustomizationSchema`)의 매개변수마다 슬라이더 · 색 · 고르기를 그립니다 — 카테고리(체형 · 얼굴 · 염색 …)로 가르고, 조건이 맞지 않는 매개변수는 숨깁니다.
         * @details 끄는 동안(`_bCustomizationActive`)은 미리보기 몸을 다시 입히지 않는다 — 피팅은 무거워서 놓을 때 한 번 푼다(설계 문서 단계 5). 스키마는 모델이 고른다
         *          (`CharacterEditorModel::findCustomizationSchema` — 선택의 것, 비었으면 기준 프리셋의 것). 스키마가 없으면(데이터를 읽기 전, 프리셋에 스키마가 없음) 이미 있는 값만 숫자로 보인다(`drawCustomizationRows`).
         */
        void drawCustomizationSliders( const AppearanceSelection& selection );
        /** @brief 미리보기 몸의 지금 모습 캡처를 요청합니다(`pollThumbnailCapture` 가 받아 같은 이름 프리셋의 썸네일 슬롯에 넣는다). 프리셋을 먼저 저장해야 합니다. */
        void saveThumbnail();
        /**
         * @brief 요청한 캡처가 끝났으면 그 PNG 를 읽어 문서에 넘기고(`CharacterEditorModel::attachThumbnail`) 임시 파일을 지웁니다.
         * @details shortcut: 렌더 스레드 스크린샷 창구가 파일로만 내놓아 사용자 데이터 폴더의 임시 파일을 거친다, 창구가 바이트를 내주게 되면 바로 받는다.
         */
        void pollThumbnailCapture();
        void drawExplanation();
        void drawFitCheck();
        void drawPresetRow();
        void drawDataPresetRow();
        void drawHistoryRow();

    private:
        CharacterEditorModel                    _document;                 ///< 상태와 명령. 이 패널은 그리기와 미리보기 동기화만 한다
        fixed_string<constant::buffer::kMax128> _newName;                  ///< 새 소켓 · 칸 이름 입력칸
        fixed_string<constant::buffer::kMax128> _renameBuffer;             ///< 고른 소켓의 새 이름 입력칸
        bool                                    _bClipLoop;                ///< 고른 클립을 반복 재생하는지 NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        GameObjectHandle                        _previewBody;              ///< 미리보기 씬의 몸 오브젝트
        uint32                                  _previewBodyRevision;      ///< 몸에 마지막으로 입힌 외형 선택의 세대(`AppearanceEditModel::getRevision`)
        uint32                                  _previewDatabaseRevision;  ///< 몸에 마지막으로 입힌 외형 데이터의 세대(`AppearanceDatabase::getRevision`)
        uint8                                   _bPreviewBodyDressed  : 1; ///< 몸에 한 번이라도 입혔다 — 세대가 같아도 첫 입히기는 한다
        uint8                                   _bModelsWereDirty     : 1; ///< 지난 그리기에 소켓 · 포즈 모델이 미저장이었다(되돌리기로 저장 상태에 돌아오면 dirty 를 내린다)
        uint8                                   _bCustomizationOpen   : 1; ///< 꾸미기 머리글이 펼쳐져 있다 — 뷰포트가 슬라이더 몇 줄 자리를 남긴다
        uint8                                   _bCustomizationActive : 1; ///< 꾸미기 슬라이더를 끄는 중이다(`drawCustomizationSliders`) — 미리보기 몸 다시 입히기를 놓을 때까지 미룬다
        uint8                                   _bGizmoPlaced         : 1; ///< `_placedGizmoWorld` 가 유효하다
        uint8                                   _bBodyPicking         : 1; ///< 뷰포트 몸 표면 집기가 켜져 있다(몸 탭이 켜고 미리보기 탭이 받는다)
        uint8                                   _bBodyPickDrag        : 1; ///< 배치 층을 끄는 중이다(놓으면 되돌리기 단계를 끊는다)
        uint8                                   _bBodyPickShown       : 1; ///< `_bodyPickMarker` 가 유효하다
        uint32                                  _seenFitRevision;          ///< 확인 표시에 올린 피팅 결과의 세대(`CharacterAppearanceComponent::getFitResultRevision`)
        EditorPreviewGizmo                      _gizmo;                    ///< 고른 본 · 소켓의 기즈모(`syncPreviewGizmo`)
        GameObjectHandle                        _gizmoProxy;               ///< 기즈모가 잡는 미리보기 씬의 표시 오브젝트 — 주인의 월드 자리에 놓는다
        float4x4                                _placedGizmoWorld;         ///< 지난 그리기에 표시 오브젝트를 놓은 월드 변환 — 달라졌으면 기즈모가 옮긴 것이다
        CharacterGizmoTarget                    _placedGizmoTarget;        ///< 그 자리를 놓은 주인 — 바뀌면 지난 자리와 견주지 않는다
        bool                                    _bRigPreview;              ///< 제약 리그를 미리보기 몸에 켠다(`syncRigPreview`) NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        Published                               _published;                ///< 감시 값이 읽는 게시 값
        BodyCustomizationViewState              _bodyState;                ///< 몸 탭의 상태(그리기마다 `gather` — 벡터 저장소를 다시 쓴다)
        uint32                                  _bodyHighlightDrive;       ///< 강조할 드라이브의 `_listDrive` 번호(무효면 강조 없음)
        uint32                                  _bodySelectedSection;      ///< 고른 구간 줄(뷰포트에서 집었거나 표에서 눌렀다 — 무효면 없음)
        uint32                                  _bodyPickLayer;            ///< 배치 집기 중인 층 번호(무효면 구간 고르기)
        float3                                  _bodyPickMarker;           ///< 마지막으로 집은 월드 자리
        float32                                 _bodyStepTimer;            ///< 동작 태그 시험의 다음 걸음까지 남은 시간(초)
        string                                  _socketResolveError;       ///< 미리보기가 지난 프레임에 상태 줄에 올린 소켓 풀기 오류(풀리면 지운다)
        string                                  _thumbnailCapturePath;     ///< 요청한 썸네일 캡처의 임시 파일(비면 요청 없음)
        string                                  _thumbnailCapturePreset;   ///< 그 캡처를 붙일 프리셋 이름
        uint32                                  _thumbnailCaptureSerial;   ///< 요청할 때의 스크린샷 처리 수(`RenderThread::getCompletedScreenshotSerial`)
    };
} // namespace sw::editor
