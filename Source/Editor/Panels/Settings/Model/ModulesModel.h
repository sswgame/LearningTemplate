/**
 * @file ModulesModel.h
 * @brief 모듈 창의 모델 — 카탈로그 · 해석, 종류 · 켜짐 필터, 고른 모듈, 켜고 끄기 미리보기(함께 꺼지고 켜지는 모듈), 프로젝트 매니페스트 고쳐 쓰기. ImGui 를 모릅니다.
 * @details 패널(`ModulesPanel`)은 표 · 상세 · 확인 창을 그리고 검색 글 · 팝업 열기만 든다 — 계층 · 인스펙터 `Model/` 과 같은 선이다
 *          (언리얼 Plugins 창이 `IPluginManager` 데이터와 Slate 화면을 나누는 것과 같다). 쓰는 곳은 `Source/Games/<활성 게임>/SWGame.module.json` 의 `_listModuleOverride` 이고,
 *          적용은 Build(매니페스트가 구성 의존이라 ninja 가 다시 구성한다) 뒤 다시 시작이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Module/ModuleCatalog.h"

namespace sw::editor
{
    struct EditorModuleDetail;

    /** @brief 켜짐 필터입니다. */
    enum class ModuleStateFilter : uint8
    {
        All,
        On,
        Off,
        Count
    };

    /**
     * @class ModulesModel
     * @brief 모듈 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class ModulesModel
    {
    public:
        /** @brief 표에 보이는 종류 순서입니다. */
        static constexpr ModuleKind kArrKindOrder[] = { ModuleKind::GameFramework, ModuleKind::Kit, ModuleKind::EditorExtension, ModuleKind::RHI, ModuleKind::Editor, ModuleKind::Game };

        ModulesModel();

        // ------------------------------------------------------------------------------
        // 1) 카탈로그
        // ------------------------------------------------------------------------------
        /** @brief 아직 읽지 않았으면(매니페스트 0 · 오류 없음) 읽습니다. 패널이 그리기마다 부릅니다. */
        void ensureLoaded();
        /** @brief `Bin/Modules` 의 카탈로그를 다시 읽고 해석합니다. 실패하면 `getLoadError` 가 이유입니다. */
        void                    reload();
        const string&           getLoadError() const { return _loadError; }
        const ModuleCatalog&    getCatalog() const { return _catalog; }
        const ModuleResolution& getResolution() const { return _resolution; }
        /** @brief 해석에서 꺼진 까닭입니다. 켜져 있으면 비어 있습니다. */
        string findInactiveReason( const string& name ) const;

        // ------------------------------------------------------------------------------
        // 2) 필터 · 고름
        // ------------------------------------------------------------------------------
        /** @brief 보일 종류(`ModuleKind` 값, -1 은 전부)입니다. */
        int32             getKindFilter() const { return _kindFilter; }
        void              setKindFilter( int32 kindFilter ) { _kindFilter = kindFilter; }
        ModuleStateFilter getStateFilter() const { return _stateFilter; }
        void              setStateFilter( ModuleStateFilter stateFilter ) { _stateFilter = stateFilter; }
        /** @brief 종류 필터에 맞으면 true 입니다. */
        bool isKindVisible( ModuleKind kind ) const { return _kindFilter < 0 || static_cast<int32>( kind ) == _kindFilter; }
        /** @brief 켜짐 필터에 맞으면 true 입니다. */
        bool isStateVisible( const string& name ) const;

        const string& getSelectedModule() const { return _selectedModule; }
        void          selectModule( const string& name ) { _selectedModule = name; }
        /** @brief 고른 모듈의 상세를 @p outDetail 에 채웁니다. 고른 것이 없거나 새로 고침으로 사라졌으면(고름을 비운다) false 입니다. */
        [[nodiscard]] bool makeSelectedDetail( EditorModuleDetail& outDetail );

        // ------------------------------------------------------------------------------
        // 3) 켜고 끄기 — 미리보기를 물은 뒤 적용한다
        // ------------------------------------------------------------------------------
        /** @brief 끄면 이 창이 사라지거나 프로젝트가 없어지는 종류입니다 — 켜고 끄기 · 초기화를 막는다. */
        static bool        isLocked( ModuleKind kind ) { return kind == ModuleKind::Editor || kind == ModuleKind::Game; }
        static const utf8* getKindName( ModuleKind kind );

        /** @brief @p moduleName 을 @p bEnabled 로 바꿀 때 함께 바뀌는 모듈을 미리 봅니다. 그래프가 깨지면 로그를 남기고 false(묻지 않는다)입니다. */
        [[nodiscard]] bool requestToggle( const string& moduleName, bool bEnabled );
        /** @brief 미리보기 중인 바꿈을 프로젝트 매니페스트에 씁니다. 성공하면 "다시 빌드" 띠가 켜진다. 성공 여부와 관계없이 미리보기를 닫는다. */
        void applyPending();
        /** @brief 미리보기를 닫습니다(바꾸지 않는다). */
        void cancelPending() { _pendingModule.clear(); }

        bool                               hasPending() const { return _pendingModule.empty() == false; }
        const string&                      getPendingModule() const { return _pendingModule; }
        bool                               isPendingEnabled() const { return _bPendingEnabled; }
        const vector<ModuleInactiveEntry>& getNewlyInactive() const { return _listNewlyInactive; }
        const vector<string>&              getNewlyActive() const { return _listNewlyActive; }
        /** @brief 이 실행에서 매니페스트를 고쳤으면 true 입니다 — 빌드 · 다시 시작 띠를 보인다. */
        bool hasChangedSinceStart() const { return _bChangedSinceStart; }

    private:
        [[nodiscard]] bool writeProjectOverride();

        ModuleCatalog               _catalog;
        ModuleResolution            _resolution;
        string                      _loadError;
        string                      _selectedModule; ///< 상세 영역에 보이는 모듈(비면 상세를 그리지 않는다)
        string                      _pendingModule;  ///< 미리보기 중인 모듈(비면 묻지 않는다)
        int32                       _kindFilter;     ///< 보일 종류(`ModuleKind` 값, -1 은 전부)
        ModuleStateFilter           _stateFilter;
        vector<ModuleInactiveEntry> _listNewlyInactive;
        vector<string>              _listNewlyActive;
        bool                        _bPendingEnabled;
        bool                        _bChangedSinceStart; ///< 이 실행에서 매니페스트를 고쳤다 — 빌드 · 다시 시작 띠를 보인다
    };
} // namespace sw::editor
