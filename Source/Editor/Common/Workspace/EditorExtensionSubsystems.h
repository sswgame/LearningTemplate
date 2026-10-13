/**
 * @file EditorExtensionSubsystems.h
 * @brief 확장 모듈(에디터 플러그인)이 등록 줄(`SW_EDITOR_SUBSYSTEM`)로 기여하는 에디터 서브시스템을 만들고, 에디터 범위에 걸고, 모듈이 내려가기 전에 내립니다.
 * @details 언리얼에서 플러그인 모듈의 `UEditorSubsystem` 파생을 에디터가 찾아 만들어 주는 자리다. 확장 모듈은 에디터가 뜬 뒤에도 로드 · 언로드되므로(핫 리로드)
 *          에디터 컨텍스트가 처음 한 번 만드는 내장 서브시스템과 따로 둔다 — 등록 목록의 세대가 바뀌면 맞추고(`syncWithRegistry`), 모듈 언로드 직전에
 *          그 이미지의 것을 내린다(`releaseSubsystemsWithin`). 확장 코드는 `editor::getService<T>()` 로 찾는다(함수 정적 싱글턴을 두지 않는다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw::editor
{
    /**
     * @struct EditorSubsystemRegistration
     * @brief 확장이 기여하는 에디터 서브시스템 한 종류의 등록 항목입니다. 서브시스템의 .cpp 가 `SW_EDITOR_SUBSYSTEM` 으로 둡니다.
     * @details 만들기와 범위에 걸기 · 풀기는 등록한 모듈 안에서 인스턴스화한 함수다 — 타입을 아는 쪽이 건다.
     */
    struct EditorSubsystemRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "subsystem";

        unique_ptr<EditorSubsystem> ( *_pCreate )();
        bool ( *_pBind )( SubsystemScope& scope, EditorSubsystem& subsystem );
        void ( *_pUnbind )( SubsystemScope& scope );
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 등록 항목이 가리키는 함수들입니다. */
    template <typename TSubsystem>
    struct EditorSubsystemRegistrationUtil
    {
        static unique_ptr<EditorSubsystem> create() { return make_unique<TSubsystem>(); }
        static bool                        bind( SubsystemScope& scope, EditorSubsystem& subsystem ) { return scope.bind( static_cast<TSubsystem*>( &subsystem ) ); }
        static void                        unbind( SubsystemScope& scope ) { scope.unbind<TSubsystem>(); }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorExtensionSubsystems
     * @brief 확장이 기여한 에디터 서브시스템의 소유자입니다(`EditorContext` 의 하위 객체).
     */
    class SW_EDITOR_API EditorExtensionSubsystems final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorExtensionSubsystems";

        /** @brief 기여받은 서브시스템을 @p scope(에디터 범위)에 겁니다. 범위는 이 객체보다 오래 삽니다. */
        explicit EditorExtensionSubsystems( SubsystemScope& scope );
        ~EditorExtensionSubsystems() override;
        EditorExtensionSubsystems( const EditorExtensionSubsystems& )            = delete;
        EditorExtensionSubsystems& operator=( const EditorExtensionSubsystems& ) = delete;

        const utf8* getSubsystemName() const override { return kSubsystemName; }
        /** @brief 기여받은 서브시스템을 모두 역순으로 내립니다. */
        void shutdownSubsystem() override { releaseAll(); }

        /** @brief 등록 목록과 맞춥니다 — 사라진 줄의 인스턴스를 내리고 새 줄은 만들어 초기화한 뒤 범위에 겁니다. 세대가 같으면 비교 한 번입니다. */
        void syncWithRegistry();
        /** @brief 등록 항목이 [@p pBegin, @p pEnd) 안(내려갈 모듈 이미지)에 있는 서브시스템을 내립니다. 내린 수를 반환합니다. */
        uint32 releaseSubsystemsWithin( const void* pBegin, const void* pEnd );
        /** @brief 모두 역순으로 내립니다. 다음 `syncWithRegistry` 가 다시 만듭니다. */
        void releaseAll();
        /** @brief 지금 살아 있는 기여 서브시스템 수입니다. */
        uint32 getSubsystemCount() const { return static_cast<uint32>( _listEntry.size() ); }

    private:
        /** @brief 기여 서브시스템 하나입니다. */
        struct Entry
        {
            const EditorSubsystemRegistration* _pRegistration{ nullptr };
            unique_ptr<EditorSubsystem>        _pSubsystem;
        };

        /** @brief @p index 의 서브시스템을 범위에서 풀고 종료 · 소멸합니다. */
        void releaseAt( size_t index );

        SubsystemScope& _scope;
        vector<Entry>   _listEntry;
        uint32          _syncedGeneration;
    };
} // namespace sw::editor

/** @brief 확장의 에디터 서브시스템을 등록합니다. 서브시스템의 .cpp 에 한 줄 둡니다. 에디터가 만들어 에디터 범위에 걸고 `editor::getService<T>()` 로 찾습니다. */
#define SW_EDITOR_SUBSYSTEM( TSubsystem, order )                                                                                                              \
    SW_EDITOR_REGISTER( ::sw::editor::EditorSubsystemRegistration, Subsystem_##TSubsystem, { TSubsystem::kSubsystemName, order },                             \
                        &::sw::editor::EditorSubsystemRegistrationUtil<TSubsystem>::create, &::sw::editor::EditorSubsystemRegistrationUtil<TSubsystem>::bind, \
                        &::sw::editor::EditorSubsystemRegistrationUtil<TSubsystem>::unbind )
