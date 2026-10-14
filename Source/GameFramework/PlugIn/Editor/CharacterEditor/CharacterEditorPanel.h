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

        const utf8*        getPanelTitle() const override { return "Character Editor"; }
        bool               isToolPanel() const override { return true; }
        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;
        bool               canSaveDocument() const override { return _sockets.isDirty() || _pose.isDirty(); }

        SocketEditModel&     getSocketModel() { return _sockets; }
        BonePoseEditModel&   getPoseModel() { return _pose; }
        AppearanceEditModel& getAppearanceModel() { return _appearance; }
        /** @brief 푼 피팅 결과를 확인 표시에 올립니다. 풀어서 넘기는 쪽(미리보기 씬, 슬라이더를 놓을 때 한 번)이 부릅니다. */
        void setFitResult( const FitResult& result );

    private:
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
        void drawHistoryRow();
        void ensureKindTable();
        void loadSkeleton();
        void loadAppearanceData();
        /** @brief 모델 편집 결과를 상태 줄에 보입니다. 실패하면 이유를, 성공하면 빈 줄을 둡니다. */
        void reportEdit( bool bSucceeded );

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
        fixed_string<constant::kMaxBuffer128> _newName;
        fixed_string<constant::kMaxBuffer128> _renameBuffer;
        string                                _status;
        hashed_string                         _selectedSocket;
        hashed_string                         _selectedBone;
        bool                                  _bKindTableReady;
        bool                                  _bDatabaseLoaded;
        bool                                  _bHasFit;
        bool                                  _bExternalChange;
    };
} // namespace sw::editor
