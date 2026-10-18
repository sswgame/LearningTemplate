/**
 * @file ComponentStateStore.h
 * @brief PROPERTY 가 아닌 컴포넌트 런타임 상태(디렉터의 시뮬레이션)를 게임 상태 스냅샷에 실어, 핫 리로드와 세이브를 넘기는 저장소를 정의합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ObjectIdentity;

    class GameObject;
    class GameObjectManager;

    /**
     * @class ComponentStateStore
     * @brief 컴포넌트마다 바이트 덩어리 하나를 (타입 이름, 컴포넌트 id) 로 들고, 게임 상태 봉투(`GameInstanceBase::serializeState`)의 한 섹션으로 씁니다.
     * @details 리플렉션 PROPERTY 는 씬 오브젝트 스냅샷이 이미 옮긴다.
     *          디렉터가 키트의 보통 클래스로 든 시뮬레이션(밭, 도시, 전장)은 PROPERTY 가 아니라서 여기에 싣는다.
     *          언리얼의 `UObject::Serialize` 오버라이드와 유니티의 `ISerializationCallbackReceiver` 에 해당한다.
     *
     *          컴포넌트 쪽 약속은 다음과 같다. 템플릿이 이름으로 부른다.
     *          - `void writeState( Archive& outArchive ) const`: 지금 상태를 쓴다. 첫 값은 그 컴포넌트의 형식 버전으로 둔다.
     *          - `void restoreState( vector<uint8>&& bytes )`: 다시 만든 컴포넌트가 받는다. 아직 `onBeginPlay` 전이면 들고 있다가 시작할 때 적용한다.
     *            읽지 못하면(버전이나 길이가 맞지 않음) 알리고 새 판으로 시작한다.
     *
     *          짝짓기는 언제나 컴포넌트 id 다. 같은 프로세스(핫 리로드)는 컴포넌트 id 가 되살아난다.
     *          다른 실행의 세이브는 id 가 새로 나가므로, 씬 섹션을 읽은 쪽이 "저장된 id → 새 id" 표를 만들어 `remapComponentIDs` 로 바꾼다
     *          (오브젝트마다 저장된 컴포넌트와 다시 만든 컴포넌트를 안정 키 `이름#n` 과 타입으로 잇는다 — 같은 타입 둘의 순서가 바뀌어도 이름으로 맞는다).
     *          예전에는 같은 타입 안의 전역 순회 순서로 맞췄는데, 그 순서는 실행마다 달라 두 디렉터의 상태가 서로 바뀔 수 있었다.
     */
    class SW_GF_API ComponentStateStore
    {
    public:
        /** @brief 컴포넌트 하나의 상태입니다. */
        struct Entry
        {
            hashed_string _typeName{};
            uint64        _componentID{ 0 };
            vector<uint8> _bytes{};
        };

        /** @brief 다른 실행의 세이브에서 컴포넌트 하나의 저장된 id 와 이번 실행에서 다시 만든 컴포넌트의 id 입니다. */
        struct SavedComponentID
        {
            uint64 _savedID{ 0 };
            uint64 _liveID{ 0 };
        };

        static constexpr uint32 kVersion = 1;

        ComponentStateStore();

        void clear() { _listEntry.clear(); }
        bool isEmpty() const { return _listEntry.empty(); }
        /** @brief 상태 하나를 더합니다. 같은 (타입, id) 가 이미 있으면 바꿉니다. */
        void add( const hashed_string& typeName, uint64 componentID, vector<uint8>&& bytes );
        /** @brief 컴포넌트 하나의 상태를 (타입, id) 로 찾습니다. 없으면 nullptr 입니다. */
        const Entry* findEntry( const hashed_string& typeName, uint64 componentID ) const;
        /**
         * @brief 다른 실행의 세이브를 읽은 뒤 항목의 저장된 id 를 이번 실행의 id 로 바꿉니다. 표에 없는 항목은 지웁니다(그 컴포넌트는 다시 만들어지지 않았다).
         * @details 표에 없는 항목을 남기면 저장된 id 가 우연히 이번 실행의 다른 컴포넌트 id 와 같을 때 그 컴포넌트가 남의 상태를 받는다.
         */
        void remapComponentIDs( const vector<SavedComponentID>& listRemap );
        /**
         * @brief 다른 실행의 세이브에서 오브젝트 하나의 저장된 컴포넌트 id(@p identity)를 다시 만든 @p object 의 컴포넌트 id 에 잇는 표를 더합니다.
         * @details 안정 키(`ComponentStableKey`, `이름#n`)와 타입이 같은 것끼리 잇는다. 키는 씬 파일의 부착 대상과 에디터 선택 복원이 쓰는 규칙이라, 같은 타입
         *          컴포넌트 둘의 순서가 바뀌어도 이름으로 맞는다(타입 차례로 이으면 두 상태가 서로 바뀐다). 키가 맞지 않는 저장 항목은 표에 넣지 않아 `remapComponentIDs` 가 지운다.
         */
        static void          appendComponentIDRemap( const ObjectIdentity& identity, const GameObject& object, vector<SavedComponentID>& outListRemap );
        const vector<Entry>& getEntries() const { return _listEntry; }

        void write( Archive& outArchive ) const;
        /** @brief `write` 로 쓴 것을 읽습니다. 형식이 맞지 않거나 잘렸으면 false 를 반환하고 비워 둡니다. */
        [[nodiscard]] bool read( Archive& archive );

        /**
         * @brief @p manager 의 @p TComponent 마다 `writeState` 를 불러 싣고, 실은 수를 반환합니다.
         * @details 매니저 타입을 템플릿으로 받아서, 이 헤더는 오브젝트 매니저를 include 하지 않는다. 부르는 쪽은 이미 include 했다.
         */
        template <typename TComponent, typename TManager>
        uint32 capture( TManager& manager )
        {
            const hashed_string& typeName = TComponent::StaticType()->_name;
            uint32               count    = 0;
            manager.template forEachComponentOfType<TComponent>( [this, &typeName, &count]( TComponent* pComponent )
            {
                Archive archive;
                pComponent->writeState( archive );
                vector<uint8> bytes;
                archive.writeData( bytes );
                add( typeName, pComponent->getComponentID(), std::move( bytes ) );
                ++count;
            } );
            return count;
        }

        /**
         * @brief @p manager 의 @p TComponent 마다 짝지은 상태를 `restoreState` 로 넘기고, 넘긴 수를 반환합니다.
         * @details 컴포넌트를 먼저 모으고 순회 밖에서 넘긴다. `restoreState` 가 세운 오브젝트를 걷을 수 있는데, 순회 콜백 안(매니저 잠금)에서는 지울 수 없기 때문이다.
         */
        template <typename TComponent, typename TManager>
        uint32 restore( TManager& manager ) const
        {
            vector<TComponent*> listComponent;
            manager.template forEachComponentOfType<TComponent>( [&listComponent]( TComponent* pComponent )
            { listComponent.push_back( pComponent ); } );
            const hashed_string& typeName = TComponent::StaticType()->_name;
            uint32               count    = 0;
            for ( TComponent* pComponent : listComponent )
            {
                const Entry* pEntry = findEntry( typeName, pComponent->getComponentID() );
                if ( pEntry == nullptr )
                    continue;
                vector<uint8> bytes = pEntry->_bytes;
                pComponent->restoreState( std::move( bytes ) );
                ++count;
            }
            return count;
        }

    private:
        vector<Entry> _listEntry;
    };
} // namespace sw

namespace sw
{
    /**
     * @class StatefulComponentRegistration
     * @brief 게임 프레임워크의 컴포넌트 타입 하나를 **모든 게임**의 상태 스냅샷에 올리는 정적 등록입니다. 컴포넌트를 정의한 .cpp 에 하나 둡니다.
     * @details `GameInstanceBase` 생성자가 등록된 타입을 모두 올린다(`registerStatefulComponent` 와 같은 일). 게임마다 한 줄씩 넣으면 빠뜨린 게임만
     *          세이브 · 핫 리로드에서 그 상태를 조용히 잃으므로, 타입을 정의한 쪽이 한 번 등록한다. 층이 위인 컴포넌트(Actor 의 카메라 디렉터)도
     *          기반 층(Foundation)의 게임 인스턴스가 include 하지 않고 올라온다.
     *          등록은 프레임워크 모듈의 정적 초기화에서 돌아 어떤 게임 인스턴스보다 먼저 끝난다. 같은 이름은 두 번 오르지 않는다.
     *          게임 모듈의 타입은 이것을 쓰지 않고 게임 인스턴스 생성자에서 `registerStatefulComponent<T>()` 로 올린다(모듈 리로드와 수명이 같다).
     */
    class SW_GF_API StatefulComponentRegistration
    {
    public:
        using CaptureFn = void ( * )( ComponentStateStore& store, GameObjectManager& manager );
        using RestoreFn = void ( * )( const ComponentStateStore& store, GameObjectManager& manager );

        StatefulComponentRegistration( const utf8* pTypeName, CaptureFn pCapture, RestoreFn pRestore );
        ~StatefulComponentRegistration();
        StatefulComponentRegistration( const StatefulComponentRegistration& )            = delete;
        StatefulComponentRegistration& operator=( const StatefulComponentRegistration& ) = delete;

        /** @brief 지금 올라 있는 등록입니다(등록 순서). */
        static const vector<StatefulComponentRegistration*>& getRegistrations();

        const utf8* getTypeName() const { return _pTypeName; }
        CaptureFn   getCapture() const { return _pCapture; }
        RestoreFn   getRestore() const { return _pRestore; }

    private:
        const utf8* _pTypeName;
        CaptureFn   _pCapture;
        RestoreFn   _pRestore;
    };
} // namespace sw

namespace sw
{
    /**
     * @class StatefulComponentRegistrar
     * @brief @p TComponent 의 `writeState` / `restoreState` 를 `ComponentStateStore` 로 잇는 정적 등록입니다.
     * @details 오브젝트 매니저를 include 한 .cpp 에서 만든다(순회 템플릿이 그때 인스턴스화된다).
     */
    template <typename TComponent>
    class StatefulComponentRegistrar final : public StatefulComponentRegistration
    {
    public:
        explicit StatefulComponentRegistrar( const utf8* pTypeName )
            : StatefulComponentRegistration( pTypeName, &StatefulComponentRegistrar::captureOf, &StatefulComponentRegistrar::restoreOf )
        {
        }

    private:
        static void captureOf( ComponentStateStore& store, GameObjectManager& manager ) { (void)store.capture<TComponent>( manager ); }
        static void restoreOf( const ComponentStateStore& store, GameObjectManager& manager )
        {
            (void)store.restore<TComponent>( manager ); // 짝이 없는 컴포넌트는 그대로 둔다
        }
    };
} // namespace sw
