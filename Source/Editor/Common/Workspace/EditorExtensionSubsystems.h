/**
 * @file EditorExtensionSubsystems.h
 * @brief 확장 모듈(에디터 플러그인)이 등록 줄(`SW_EDITOR_SUBSYSTEM`)로 추가하는 에디터 서브시스템을 만들어 에디터 범위에 등록하고, 모듈이 언로드되기 전에 종료합니다.
 * @details 언리얼에서 에디터가 플러그인 모듈의 `UEditorSubsystem` 파생 클래스를 찾아 만들어 주는 것에 해당한다. 확장 모듈은 에디터가 뜬 뒤에도
 *          핫 리로드로 로드되고 언로드되므로, 에디터 컨텍스트가 처음 한 번 만드는 내장 서브시스템과 따로 둔다. 등록 목록의 세대가 바뀌면 맞추고
 *          (`syncWithRegistry`), 모듈을 내리기 전에 그 줄이 빠지면 그 자리에서 종료한다(`syncWithRegistry`). 확장 코드는 함수 정적 싱글턴을 두지 않고
 *          `editor::findSubsystem<T>()` 로 찾는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
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
     * @details 만들기와 범위 등록, 해제는 등록한 모듈 안에서 인스턴스화한 함수다. 타입을 아는 쪽이 등록한다.
     */
    struct EditorSubsystemRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "subsystem";
        /** @brief 같은 모듈의 패널 · 명령이 쓴다 — 그것들이 먼저 내려가게 나중에 뗀다. */
        static constexpr uint32 kDetachPass = 1;

        unique_ptr<EditorSubsystem> ( *_pCreate )();
        bool ( *_pBind )( SubsystemScope& scope, EditorSubsystem& subsystem );
        void ( *_pUnbind )( SubsystemScope& scope );

        /** @brief 등록 줄이 가리키는 함수들입니다(`SW_EDITOR_SUBSYSTEM` 이 채운다). */
        template <typename TSubsystem>
        static unique_ptr<EditorSubsystem> create()
        {
            return make_unique<TSubsystem>();
        }
        template <typename TSubsystem>
        [[nodiscard]] static bool bind( SubsystemScope& scope, EditorSubsystem& subsystem )
        {
            return scope.bind( static_cast<TSubsystem*>( &subsystem ) );
        }
        template <typename TSubsystem>
        static void unbind( SubsystemScope& scope )
        {
            scope.unbind<TSubsystem>();
        }
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
        /** @brief 기여받은 서브시스템을 모두 역순으로 종료합니다. */
        void shutdownSubsystem() override { releaseAll(); }

        /** @brief 등록 목록과 맞춥니다. 사라진 줄의 인스턴스를 종료하고, 새 줄은 만들어 초기화한 뒤 범위에 등록합니다. 세대가 같으면 비교 한 번으로 끝납니다. */
        void syncWithRegistry();
        /** @brief 모두 역순으로 종료합니다. 다음 `syncWithRegistry` 가 다시 만듭니다. */
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

        /** @brief @p index 의 서브시스템을 범위에서 해제하고 종료한 뒤 소멸시킵니다. */
        void releaseAt( size_t index );

        SubsystemScope& _scope;
        vector<Entry>   _listEntry;
        uint32          _syncedGeneration;
    };
} // namespace sw::editor

/** @brief 확장의 에디터 서브시스템을 등록합니다. 서브시스템의 .cpp 에 한 줄 둡니다. 에디터가 만들어 에디터 범위에 등록하고, `editor::findSubsystem<T>()` 로 찾습니다. */
#define SW_EDITOR_SUBSYSTEM( TSubsystem, order )                                                                                                      \
    SW_EDITOR_REGISTER( ::sw::editor::EditorSubsystemRegistration, Subsystem_##TSubsystem, { TSubsystem::kSubsystemName, order },                     \
                        &::sw::editor::EditorSubsystemRegistration::create<TSubsystem>, &::sw::editor::EditorSubsystemRegistration::bind<TSubsystem>, \
                        &::sw::editor::EditorSubsystemRegistration::unbind<TSubsystem> )
