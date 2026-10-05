/**
 * @file ComponentRegistry.h
 * @brief 타입별 컴포넌트 등록부입니다 — "그 타입의 컴포넌트를 모두" 를 씬 전체를 훑지 않고 돌려줍니다.
 *
 * [왜 필요한가]
 * `GameObjectManager::forEachComponentOfType` 은 **모든 오브젝트**를 공유 잠금 아래 훑고 오브젝트마다 타입 검사를 합니다. 빛 찾기가 그랬을 때
 * 큐브 20,000 개 벤치에서 게임 스레드 7.6 ms 중 2.9 ms 를 썼고, 상호작용 하는 쪽 하나가 그 훑기로 대상을 찾을 때 Release 오브젝트 10,000 개에서
 * 틱이 약 200 us 늘었습니다(`InteractionBenchTest`). **찾지 말고 등록받습니다** — 언리얼이 `TObjectIterator` 대신 서브시스템이 등록받은 목록을
 * 쓰는 것과 같습니다. 프레임 비용이 "씬의 오브젝트 수" 가 아니라 "그 타입의 수" 가 됩니다.
 *
 * [규약]
 * - 컴포넌트는 `onRegister` 에서 `add<자기 타입>( this )`, `onUnregister` 에서 `remove<자기 타입>( this )` 를 부릅니다. 두 훅은 등록마다 정확히 한 번이라
 *   (해체는 `destroyComponentInstance` 하나를 지난다) 핫 리로드 · 모듈 해제 · 씬 내리기에서도 목록이 맞습니다. 에디터(플레이 전)에서도 찾아집니다.
 * - 키는 **등록할 때 고른 타입**(`T::StaticType()` 의 정규 이름)과 칸 번호입니다. 파생이 기반의 목록에 들려면 기반 타입으로 등록합니다.
 *   정규 이름이라 모듈을 다시 올려 `TypeInfo` 주소가 바뀌어도 같은 칸입니다.
 * - 칸(`channel`)은 한 타입 안의 하위 목록입니다(빛은 종류 — 방향광 · 점광 · 스포트).
 * - 목록은 **등록 순서**를 지키고, 빼도 남은 것의 순서가 그대로입니다("활성인 첫 방향광" 이 등록 순서로 정해진다).
 * - 활성 여부는 부르는 쪽이 봅니다(`Component::isActive`). 등록부는 "무엇이 있나" 만 압니다.
 * - 항목은 컴포넌트 핸들(오브젝트 id + 컴포넌트 id — 다시 쓰지 않는 id 라 세대와 같다)을 함께 듭니다. 프레임을 넘겨 들 것은 핸들로 들고 매니저로 풉니다.
 *
 * @note 락은 `PrimitiveRegistry` 와 같이 **가장 안쪽**입니다. 등록 · 해제는 매니저 락 안에서 불릴 수 있습니다. 조회는 잠그지 않습니다 —
 *       목록이 바뀌는 등록 · 해제는 틱 밖(구조 변경은 틱 뒤로 미뤄진다)에서만 일어나므로 병렬 틱 중의 조회와 겹치지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct TypeInfo;

    class Component;

    /**
     * @class ComponentRegistry
     * @brief 씬(`GameObjectManager`) 하나의 타입별 컴포넌트 목록입니다. 소유하지 않습니다(수명은 GameObject 가 쥡니다).
     */
    class SW_API ComponentRegistry
    {
    public:
        /** @brief 등록된 컴포넌트 하나입니다. */
        struct Entry
        {
            Component*      _pComponent;
            ComponentHandle _handle;
        };

        /** @brief 한 칸의 목록을 `T*` 로 보는 창입니다. 등록 · 해제가 일어나면 버립니다(그 사이에만 씁니다). */
        template <typename T>
        class View
        {
        public:
            /** @brief 항목을 `T*` 로 내는 반복자입니다. */
            class Iterator
            {
            public:
                explicit Iterator( const Entry* pEntry )
                    : _pEntry{ pEntry } {}
                T*        operator*() const { return static_cast<T*>( _pEntry->_pComponent ); }
                Iterator& operator++()
                {
                    ++_pEntry;
                    return *this;
                }
                bool operator==( const Iterator& other ) const { return _pEntry == other._pEntry; }
                bool operator!=( const Iterator& other ) const { return _pEntry != other._pEntry; }

            private:
                const Entry* _pEntry;
            };

            explicit View( const vector<Entry>& listEntry )
                : _pListEntry{ &listEntry } {}

            Iterator begin() const { return Iterator( _pListEntry->data() ); }
            Iterator end() const { return Iterator( _pListEntry->data() + _pListEntry->size() ); }
            size_t   size() const { return _pListEntry->size(); }
            bool     empty() const { return _pListEntry->empty(); }
            /** @brief 등록 순서로 @p index 번째입니다. */
            T* operator[]( size_t index ) const { return static_cast<T*>( ( *_pListEntry )[index]._pComponent ); }
            /** @brief 등록 순서로 @p index 번째의 핸들입니다. */
            ComponentHandle getHandle( size_t index ) const { return ( *_pListEntry )[index]._handle; }

        private:
            const vector<Entry>* _pListEntry;
        };

        ComponentRegistry();
        ~ComponentRegistry();

        ComponentRegistry( const ComponentRegistry& )            = delete;
        ComponentRegistry& operator=( const ComponentRegistry& ) = delete;

        /** @brief @p pComponent 를 타입 T 의 칸 @p channel 에 등록합니다. 이미 있거나 nullptr 이면 무시합니다. */
        template <typename T>
        void add( T* pComponent, uint32 channel = 0 )
        {
            addEntry( computeTypeKey( T::StaticType() ), channel, pComponent );
        }
        /** @brief 등록을 풉니다. 멱등입니다(없으면 할 일이 없다). 남은 것의 순서는 그대로입니다. */
        template <typename T>
        void remove( T* pComponent, uint32 channel = 0 )
        {
            removeEntry( computeTypeKey( T::StaticType() ), channel, pComponent );
        }
        /** @brief 타입 T 의 칸 @p channel 에 등록된 것(등록 순서)입니다. 없으면 빈 창입니다. */
        template <typename T>
        View<T> getAll( uint32 channel = 0 ) const
        {
            return View<T>( findEntries( computeTypeKey( T::StaticType() ), channel ) );
        }
        /** @brief @p handle 의 컴포넌트가 타입 T 의 칸 @p channel 에 등록되어 있으면 true 입니다(지워진 컴포넌트의 핸들이면 false). */
        template <typename T>
        bool isRegistered( ComponentHandle handle, uint32 channel = 0 ) const
        {
            return containsHandle( computeTypeKey( T::StaticType() ), channel, handle );
        }

        /** @brief 등록부의 키입니다 — 타입의 정규 이름 해시입니다. nullptr 이면 0 입니다. */
        static uint32 computeTypeKey( const TypeInfo* pType );

    private:
        /** @brief 한 (타입, 칸) 의 목록입니다. */
        struct Bucket
        {
            uint32        _typeKey;
            uint32        _channel;
            vector<Entry> _listEntry;
        };

        void                 addEntry( uint32 typeKey, uint32 channel, Component* pComponent );
        void                 removeEntry( uint32 typeKey, uint32 channel, const Component* pComponent );
        const vector<Entry>& findEntries( uint32 typeKey, uint32 channel ) const;
        bool                 containsHandle( uint32 typeKey, uint32 channel, ComponentHandle handle ) const;
        Bucket*              findBucket( uint32 typeKey, uint32 channel ) const;

        /** @brief (타입, 칸) 마다 하나입니다. 주소가 움직이지 않게 따로 잡습니다 — 받아 간 창이 다른 칸의 등록으로 무너지지 않는다. */
        vector<unique_ptr<Bucket>> _listBucket;
        /** @brief 등록 · 해제를 지킵니다(가장 안쪽 락). */
        mutable mutex _mutex;
    };
} // namespace sw
