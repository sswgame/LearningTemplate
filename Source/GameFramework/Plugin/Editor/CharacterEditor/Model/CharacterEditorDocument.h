/**
 * @file CharacterEditorDocument.h
 * @brief 캐릭터 편집 창의 모델 — 팩 데이터 경로, 소켓 · 레퍼런스 포즈 파일 읽기 · 저장, 외형 데이터 · 프리셋 · 데이터 프리셋 내보내기, 고른 소켓 · 본 · 클립, 규칙 설명 풀이. ImGui 와 미리보기 씬을 모릅니다.
 * @details 패널(`CharacterEditorPanel`)은 그리기와 미리보기 씬 동기화만 하고, 상태와 명령은 모두 이것이 든다 — 인스펙터의 `Model/` 과 같은 선이다(언리얼의 에셋 편집기 툴킷이
 *          `FAssetEditorToolkit`(화면)과 편집 대상 객체(데이터)를 나누는 것과 같다). 결과는 상태 줄(`getStatus`)에 남기고, 실패 이유를 화면이 따로 만들지 않는다.
 *          편집 모델 셋(`SocketEditModel` · `BonePoseEditModel` · `AppearanceEditModel`)의 편집 기록은 그 모델 안에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/UserAppearancePresetStore.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/AppearanceEditModel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BonePoseEditModel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/FitCheckSummary.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/SaveReloadGuard.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/SocketEditModel.h"

namespace sw
{
    struct FitResult;
    struct LocalStoreCompletion;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct CharacterEditorPaths
     * @brief 창의 입력칸이 그대로 고치는 경로 · 이름입니다. 팩 데이터(`CharacterEditorPackData`)가 채우고 사람이 바꿀 수 있습니다.
     */
    struct CharacterEditorPaths
    {
        fixed_string<constant::kMaxPathSize>  _socketPath; ///< 문서인 소켓 파일(`*.sockets.xml`, 리소스 id)
        fixed_string<constant::kMaxPathSize>  _skeletonPath;
        fixed_string<constant::kMaxPathSize>  _refPosePath;
        fixed_string<constant::kMaxPathSize>  _itemsPath;
        fixed_string<constant::kMaxPathSize>  _appearanceFolder;
        fixed_string<constant::kMaxPathSize>  _clipFolder; ///< 미리보기 몸 애니메이터의 클립 폴더(팩 데이터의 `clipFolder`)
        fixed_string<constant::kMaxPathSize>  _rigPath;    ///< 미리보기 몸에 얹어 볼 제약 리그(팩 데이터의 선택 칸 `rig`, 비면 리그 단추가 없다)
        fixed_string<constant::kMaxBuffer128> _presetName;
        fixed_string<constant::kMaxPathSize>  _exportFolder; ///< 데이터 프리셋을 쓸 폴더(리소스 id). 기본은 외형 폴더의 `presets/`
        fixed_string<constant::kMaxBuffer128> _exportID;     ///< 데이터 프리셋 id — 씬의 컴포넌트가 가리키는 이름
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class CharacterEditorDocument
     * @brief 캐릭터 편집 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class CharacterEditorDocument
    {
    public:
        /** @brief 활성 게임 팩의 캐릭터 편집 데이터 상태입니다. */
        enum class PackDataState : uint8
        {
            Unread,  ///< 아직 읽지 않았다
            Missing, ///< 이 게임 팩에는 캐릭터 편집 데이터가 없다
            Invalid, ///< 경로를 만들 수 없거나 파일을 읽지 못했다(`getPackDataMessage`)
            Ready,   ///< 경로 칸을 채웠다
        };

        CharacterEditorDocument();

        // ------------------------------------------------------------------------------
        // 팩 데이터와 종류 표
        // ------------------------------------------------------------------------------
        /**
         * @brief 활성 게임 팩의 캐릭터 편집 데이터(`CharacterEditorPackData`)를 경로 칸에 채웁니다. 활성 팩이 바뀌었을 때만 다시 읽습니다.
         * @details 팩에 파일이 없으면 `Missing`, 못 읽으면 `Invalid` 이고, 창은 탭 대신 그 상태를 보인다(다른 게임의 경로로 대체하지 않는다).
         */
        void                 ensurePackData();
        PackDataState        getPackDataState() const { return _packDataState; }
        const string&        getPackDataPath() const { return _packDataPath; }
        const string&        getPackDataMessage() const { return _packDataMessage; }
        const hashed_string& getPreviewPresetID() const { return _previewPresetID; }
        /** @brief 엔진 기본 소켓 종류 표와 좌우 이름 규칙을 한 번 읽어 소켓 모델에 겁니다. */
        void ensureKindTable();

        // ------------------------------------------------------------------------------
        // 문서(소켓 · 레퍼런스 포즈 파일)
        // ------------------------------------------------------------------------------
        /** @brief 소켓 파일을 경로 칸에서 읽습니다. 성공하면 true 이고 고른 소켓 · 외부 변경 표시를 비웁니다. */
        [[nodiscard]] bool loadSocketFile();
        /**
         * @brief 소켓 파일이 디스크에서 바뀌었는지 봅니다(@p nowSeconds 는 저장 직후 우리 쓰기를 거르는 시계). 고치지 않은 모델은 바로 다시 읽습니다.
         * @details 고친 것이 있으면 버릴지 사람이 정한다(`hasExternalChange` 를 보고 화면이 단추를 낸다) — 말없이 덮지 않는다.
         */
        void pollExternalChange( float64 nowSeconds );
        /** @brief 고친 것이 있는 채로 소켓 파일이 디스크에서 바뀌었으면 true 입니다. */
        bool hasExternalChange() const { return _bExternalChange; }
        /** @brief 소켓 · 포즈 모델 가운데 미저장이 있으면 true 입니다. */
        bool isDirty() const { return _sockets.isDirty() || _pose.isDirty(); }
        /** @brief 미저장인 소켓 · 포즈 파일을 씁니다. 하나라도 못 쓰면 false 이고 까닭은 상태 줄에 남습니다. */
        [[nodiscard]] bool saveFiles( float64 nowSeconds );
        /** @brief 두 모델을 마지막으로 읽거나 저장한 상태로 돌립니다. 외형 선택은 파일 문서가 아니라 건드리지 않습니다. */
        void revert();
        /** @brief 소켓 표를 비웁니다("New"). */
        void clearSockets();
        /** @brief 레퍼런스 포즈 파일을 경로 칸에서 읽습니다. 파일이 없으면 저장하면 만든다고 알립니다. */
        void loadReferencePose();
        /** @brief 스켈레톤을 읽고 소켓 모델에 본 이름을 겁니다(소켓 부모 본 검증). */
        void loadSkeleton();

        // ------------------------------------------------------------------------------
        // 소켓 편집 명령 — 성공하면 고른 소켓을 결과로 옮긴다
        // ------------------------------------------------------------------------------
        void addSocket( const hashed_string& name );
        void renameSocket( const hashed_string& name, const hashed_string& newName );
        void mirrorSocket( const hashed_string& name );
        void removeSocket( const hashed_string& name );
        /** @brief 고른 본을 좌우 대칭 짝으로 옮겨 그 짝에 같은 덮어쓰기를 둡니다. */
        void mirrorSelectedBone();

        // ------------------------------------------------------------------------------
        // 외형 데이터 · 프리셋
        // ------------------------------------------------------------------------------
        /** @brief 아이템 파일과 외형 폴더를 읽습니다. 실패하면 외형 데이터가 없는 상태입니다. */
        void loadAppearanceData();
        bool isDatabaseLoaded() const { return _bDatabaseLoaded; }
        /**
         * @brief 사용자 프리셋 슬롯(`CharacterEditorLocalStore::kPresetSlot`)을 읽기 시작합니다. 결과는 끝나면 목록 · 상태 줄에 온다.
         * @details 프리셋은 사용자 데이터라 콘텐츠 트리가 아니라 에디터 쪽 로컬 저장소(`CharacterEditorLocalStore`)의 슬롯에 둔다.
         */
        void loadPresets();
        /** @brief 지금 선택을 이름(`_presetName`)으로 넣은 프리셋 목록을 슬롯에 쓰기 시작합니다. 쓰기가 끝나야 목록과 "저장된 상태" 가 바뀐다(실패하면 그대로). */
        void savePreset();
        /** @brief 목록의 프리셋 @p name 을 선택으로 읽고 이름 칸에 둡니다. */
        void applyPreset( string_view name );
        /** @brief 이름 칸의 프리셋에 썸네일을 붙일 수 있으면 true 입니다. 프리셋을 먼저 저장해야 하고, 앞 저장이 끝나야 한다. 아니면 까닭을 남깁니다. */
        [[nodiscard]] bool canAttachThumbnail();
        /** @brief 미리보기 그림 @p pngBytes 를 프리셋 @p presetName 의 썸네일 슬롯에 쓰고, 끝나면 그 슬롯을 프리셋에 걸어 목록을 다시 씁니다. */
        void attachThumbnail( string_view presetName, vector<uint8> pngBytes );
        /** @brief 프리셋 슬롯 읽기 · 쓰기가 아직 끝나지 않았으면 true 입니다. */
        bool isPresetRequestPending() const { return _presetRequest.isActive() || _thumbnailRequest.isActive(); }
        /**
         * @brief 지금 선택을 데이터 프리셋 에셋(`<폴더>/<id>.appearance.xml`)으로 쓰고, 이 창과 실행 중인 게임의 외형 데이터에 올립니다. 내보냈으면 true 입니다.
         * @details 씬의 `CharacterAppearanceComponent` 가 그 id 를 가리키면 조립한다(언리얼 · 유니티의 프리셋 에셋). 세이브 형식(프리셋 파일)과 다른 길이다.
         */
        [[nodiscard]] bool exportDataPreset();
        /** @brief 기준 프리셋이 없으면 팩 데이터의 미리보기 몸 프리셋을 기준으로 둔 선택입니다(데이터가 정한 몸 — 코드에 몸을 두지 않는다). */
        AppearanceSelection makePreviewSelection() const;
        /** @brief @p selection 의 꾸미기 스키마입니다. 선택의 것, 없으면 기준 프리셋(없으면 미리보기 몸 프리셋)의 것이며 데이터가 없으면 nullptr 입니다. */
        const CustomizationSchemaDef* findCustomizationSchema( const AppearanceSelection& selection ) const;
        /** @brief 기준 프리셋이 외형 데이터에 있으면 true 입니다(규칙 설명은 기준 프리셋이 시드다). */
        bool hasBasePresetInData() const;
        /**
         * @brief 규칙 설명을 지금 선택으로 풉니다. 해석기는 경고를 로그로 남기므로 선택 · 데이터가 바뀔 때만 다시 푼다.
         * @details 결과는 `getResolvedPreview` · `getResolvedReport` 로 읽는다.
         */
        void                             refreshResolvedPreview();
        const ResolvedAppearance&        getResolvedPreview() const { return _preview; }
        const AppearanceSelectionReport& getResolvedReport() const { return _previewReport; }
        /** @brief 선택에서 아이템을 낀 칸의 수입니다. */
        uint32 countEquippedSlots() const;

        // ------------------------------------------------------------------------------
        // 피팅 결과 · 클립
        // ------------------------------------------------------------------------------
        /** @brief 푼 피팅 결과를 확인 표시에 올립니다. 풀어서 넘기는 쪽(미리보기 씬, 슬라이더를 놓을 때 한 번)이 부릅니다. */
        void                   setFitResult( const FitResult& result );
        bool                   hasFit() const { return _bHasFit; }
        const FitCheckSummary& getFit() const { return _fit; }
        /** @brief 팩 데이터의 클립 폴더에서 모은 클립 이름(`.animclip` 파일 이름, 이름순)입니다. 처음 부를 때 폴더를 훑습니다. 폴더가 디스크에 없으면 빕니다(팩만 있는 배포본). */
        const vector<hashed_string>& getClipNames();

        // ------------------------------------------------------------------------------
        // 고른 것 · 상태 줄 · 모델
        // ------------------------------------------------------------------------------
        const hashed_string& getSelectedSocket() const { return _selectedSocket; }
        void                 selectSocket( const hashed_string& name ) { _selectedSocket = name; }
        const hashed_string& getSelectedBone() const { return _selectedBone; }
        void                 selectBone( const hashed_string& name ) { _selectedBone = name; }
        const hashed_string& getSelectedClip() const { return _selectedClip; }
        void                 selectClip( const hashed_string& name ) { _selectedClip = name; }

        /** @brief 상태 줄입니다. 비면 보일 것이 없습니다. */
        const string& getStatus() const { return _status; }
        /** @brief 편집 모델의 명령이 실패 이유를 쓰는 상태 줄입니다(`SocketEditModel::setParent( …, error )`). */
        string& editStatus() { return _status; }
        void    setStatus( string_view status ) { _status = string{ status }; }
        void    clearStatus() { _status.clear(); }
        /** @brief 모델 편집 결과를 상태 줄에 보입니다. 실패하면 이유가 남고, 성공하면 줄을 비웁니다. */
        void reportEdit( bool bSucceeded );

        CharacterEditorPaths&            editPaths() { return _paths; }
        const CharacterEditorPaths&      getPaths() const { return _paths; }
        SocketEditModel&                 getSocketModel() { return _sockets; }
        const SocketEditModel&           getSocketModel() const { return _sockets; }
        BonePoseEditModel&               getPoseModel() { return _pose; }
        const BonePoseEditModel&         getPoseModel() const { return _pose; }
        AppearanceEditModel&             getAppearanceModel() { return _appearance; }
        const AppearanceEditModel&       getAppearanceModel() const { return _appearance; }
        AppearanceDatabase&              getDatabase() { return _database; }
        const AppearanceDatabase&        getDatabase() const { return _database; }
        const UserAppearancePresetStore& getPresetStore() const { return _presetStore; }

    private:
        /** @brief 클립 폴더를 훑어 클립 이름을 모읍니다. */
        void loadClipNames();
        /** @brief 에디터 쪽 로컬 저장소입니다. 없으면 까닭을 상태 줄에 남기고 nullptr 입니다. */
        class CharacterEditorLocalStore* findLocalStore();
        /** @brief @p staged 를 프리셋 슬롯에 쓰기 시작합니다. 끝나면 목록이 @p staged 가 되고, @p pSavedSelection 이 있으면 그것이 "저장된 상태" 가 된다. */
        void submitPresetWrite( UserAppearancePresetStore staged, const AppearanceSelection* pSavedSelection );
        void onPresetsRead( const LocalStoreCompletion& completion );
        void onPresetsWritten( const LocalStoreCompletion& completion );
        void onThumbnailWritten( const LocalStoreCompletion& completion );

        SocketEditModel           _sockets;
        BonePoseEditModel         _pose;
        AppearanceEditModel       _appearance;
        SaveReloadGuard           _reloadGuard;
        FitCheckSummary           _fit;
        unique_ptr<ItemCatalog>   _pItems;
        AppearanceDatabase        _database;
        UserAppearancePresetStore _presetStore;
        UserAppearancePresetStore _stagedPresetStore;   ///< 쓰는 중인 프리셋 목록 - 쓰기가 끝나면 `_presetStore` 가 된다
        AppearanceSelection       _stagedSelection;     ///< 쓰는 중인 저장의 선택(`_bStagedSelection` 일 때) - 끝나면 "저장된 상태"
        string                    _thumbnailPresetName; ///< 썸네일을 쓰는 중인 프리셋
        string                    _thumbnailSlot;       ///< 그 썸네일 슬롯
        Subscription              _presetRequest;       ///< 프리셋 슬롯 읽기 · 쓰기(하나씩)
        Subscription              _thumbnailRequest;    ///< 썸네일 슬롯 쓰기
        CharacterEditorPaths      _paths;
        string                    _status;
        hashed_string             _selectedSocket;
        hashed_string             _selectedBone;
        vector<hashed_string>     _listClipName;    ///< 클립 폴더의 클립 이름들(콤보 목록, 이름순)
        hashed_string             _selectedClip;    ///< 콤보에서 고른 클립
        ResolvedAppearance        _preview;         ///< 입혀 본 결과(규칙 설명). 선택이 바뀔 때만 다시 푼다
        AppearanceSelectionReport _previewReport;   ///< `_preview` 와 같이 푼 보고
        uint32                    _previewRevision; ///< `_preview` 를 푼 외형 선택의 세대
        string                    _packRoot;        ///< 경로 칸을 채운 활성 게임 팩 루트 — 바뀌면 다시 읽는다
        string                    _packDataPath;    ///< 활성 팩의 캐릭터 편집 데이터 파일
        string                    _packDataMessage; ///< `Invalid` 의 이유
        hashed_string             _previewPresetID; ///< 팩 데이터의 미리보기 몸 프리셋(선택에 기준 프리셋이 없을 때)
        PackDataState             _packDataState;
        bool                      _bClipNamesLoaded; ///< 클립 이름을 한 번 모았다(팩 데이터가 바뀌면 다시 모은다)
        bool                      _bKindTableReady;
        bool                      _bDatabaseLoaded;
        bool                      _bHasFit;
        bool                      _bExternalChange;
        bool                      _bPreviewValid;
        bool                      _bStagedSelection;
    };
} // namespace sw::editor
