/**
 * @file EditorStateStore.h
 * @brief 에디터 범위가 소유하는 "타입 하나에 값 하나" 상태 저장소입니다. `.cpp` 안의 함수 정적 접근자(`static T s; return s;`)를 대신합니다.
 * @details 그리는 함수 · 탐침이 읽는 값 · 프레임 사이에 남는 캐시처럼 한 파일 안에서만 쓰는 상태에는 패널 인스턴스처럼 눈에 띄는 주인이 없다.
 *          함수 안 정적 지역은 프로세스에 하나라 서비스 접근 게이트가 보지 못하고, 에디터를 둘 띄우거나 테스트를 병렬로 돌리면 섞이고, 모듈을
 *          다시 불러도 값이 남거나 사라지는 때가 코드로 정해지지 않는다. 이 저장소는 상태를 에디터 범위(`EditorContext` 의 `SubsystemScope`)에 둔다 —
 *          컨텍스트와 함께 생기고 사라지고, 범위마다 따로이며, 테스트는 자기 범위를 만들어 건다(`ScopedEditorScope`).
 *          값은 처음 물을 때 `TState{}` 로 만든다. 주인이 분명한 상태(패널만 쓰는 값은 패널 멤버, 테마는 `EditorTheme`)는 그 주인에게 두고 여기 넣지 않는다.
 *          **EditorModule 안의 코드만 쓴다.** 확장 모듈(플러그인) 코드는 모듈이 언로드될 때 vtable 이 사라지므로 `SW_EDITOR_SUBSYSTEM` 서브시스템을 쓴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    /**
     * @class EditorStateStore
     * @brief 상태 값을 타입마다 하나씩 소유합니다(`EditorContext` 의 하위 객체). 부르는 쪽은 `getEditorState<T>()` 를 쓴다.
     * @details 값은 힙에 두므로 저장소가 커져도 주소가 그대로다. 찾기 · 만들기는 잠그지만 값 자체는 잠그지 않는다 — 값의 스레드 규칙은 쓰는 쪽이 정한다(UI 스레드 하나, 또는 값이 `atomic`).
     *          타입 열쇠는 타입 해시다. 이름이 겹치는 익명 타입 둘이 같은 칸을 쓰지 않게 크기도 확인한다 — 상태 타입은 그 번역 단위의 `XxxInternal` 구조체 안에 두어 이름을 번역 단위마다 다르게 한다.
     */
    class SW_EDITOR_API EditorStateStore final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorStateStore";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        EditorStateStore();
        ~EditorStateStore() override;
        EditorStateStore( const EditorStateStore& )            = delete;
        EditorStateStore& operator=( const EditorStateStore& ) = delete;

        /** @brief 타입 @p TState 의 값입니다. 처음 물으면 `TState{}` 로 만듭니다. 반환한 참조는 저장소가 사라질 때까지 유효합니다. */
        template <typename TState>
        TState& get()
        {
            void* pValue = findOrCreate( getSubsystemTypeHash<TState>(), sizeof( TState ), &EditorStateStore::createHolder<TState> );
            return *static_cast<TState*>( pValue );
        }

        /** @brief 만들어 둔 값의 수입니다(테스트 확인용). */
        uint32 getStateCount() const;

    private:
        /** @brief 값 하나를 담는 상자의 바탕입니다. 소멸자로 값을 지운다. */
        struct HolderBase
        {
            virtual ~HolderBase() = default;
            /** @brief 담은 값의 주소입니다. */
            virtual void* getValue() = 0;
        };

        template <typename TState>
        struct Holder final : HolderBase
        {
            TState _value{};
            void*  getValue() override { return &_value; }
        };

        template <typename TState>
        static unique_ptr<HolderBase> createHolder()
        {
            return make_unique<Holder<TState>>();
        }

        using CreateFunc = unique_ptr<HolderBase> ( * )();

        struct Entry
        {
            uint64                 _typeHash;
            size_t                 _size;
            unique_ptr<HolderBase> _pHolder;
        };

        /** @brief @p typeHash 의 값 주소를 반환합니다. 없으면 @p pfnCreate 로 만들어 더합니다. 같은 열쇠에 크기가 다르면 두 타입이 겹친 것이라 멈춘다. */
        void* findOrCreate( uint64 typeHash, size_t size, CreateFunc pfnCreate );

        mutable mutex _mutex;
        vector<Entry> _listEntry;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 지금 에디터 범위의 타입 @p TState 상태입니다. 에디터 범위가 없거나 거기에 저장소가 없으면 부른 쪽의 잘못이라 멈춥니다.
     * @details 에디터 컨텍스트는 저장소를 맨 먼저 만든다. 컨텍스트 없이 도는 테스트는 `ScopedEditorStateService`(EditorTestServices.h)로 건다.
     */
    template <typename TState>
    TState& getEditorState()
    {
        return getSubsystem<EditorStateStore>().template get<TState>();
    }
} // namespace sw::editor
