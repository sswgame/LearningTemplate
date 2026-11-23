/**
 * @file ComponentStateStore.h
 * @brief PROPERTY 가 아닌 컴포넌트 런타임 상태(디렉터의 시뮬레이션)를 게임 상태 스냅샷에 실어, 핫 리로드와 세이브를 넘기는 저장소를 정의합니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/SceneIterationKey.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"

#include <type_traits>
#include <utility>

namespace sw
{
    struct ComponentStateCodec;
    struct ObjectIdentity;

    class Component;
    class GameObject;

    namespace ComponentStateStoreTraits
    {
        /** @brief @p T 가 시작 전에 받은 상태를 보이는 `getPendingStateBytes()` 를 가졌는지 봅니다(C++17 이라 `requires` 를 쓰지 않는다). */
        template <typename T, typename = void>
        struct HasPendingState : std::false_type
        {
        };
        template <typename T>
        struct HasPendingState<T, std::void_t<decltype( std::declval<const T&>().getPendingStateBytes() )>> : std::true_type
        {
        };
    } // namespace ComponentStateStoreTraits

    /**
     * @class ComponentStateStore
     * @brief 컴포넌트마다 바이트 덩어리 하나를 (타입 이름, 컴포넌트 id) 로 보관하고, 게임 상태 봉투(`GameStateSnapshot::writeEnvelope`)의 한 섹션으로 씁니다.
     * @details 리플렉션 PROPERTY 는 씬 오브젝트 스냅샷이 이미 옮긴다.
     *          디렉터가 키트의 보통 클래스로 든 시뮬레이션(밭, 도시, 전장)은 PROPERTY 가 아니라서 여기에 싣는다.
     *          스트리밍 셀 · 데이터 레이어 묶음 · 하위 레벨에 든 오브젝트의 것은 그 떠 둔 상태에 함께 실린다(`ComponentStateCodec` — 복원할 때 이 저장소가 찾을 오브젝트가 아직 없다).
     *          언리얼의 `UObject::Serialize` 오버라이드와 유니티의 `ISerializationCallbackReceiver` 에 해당한다.
     *
     *          컴포넌트 쪽 약속은 다음과 같다. 템플릿이 이름으로 부른다.
     *          - `void serializeState( StateIO& io )`: 지금 상태를 쓴다(`StateIO::writeObject` 로 부른다 — `SW_DECLARE_STATE_IO` 가 낸다). 첫 값은 그 컴포넌트의 형식 버전으로 둔다.
     *            손으로 짝지은 `writeState( Archive& )` 는 여기에 꽂을 수 없다 — 쓰기와 읽기의 칸 순서는 본문 하나가 정한다.
     *          - `void restoreState( vector<uint8>&& bytes )`: 다시 만든 컴포넌트가 받는다. 아직 `onBeginPlay` 전이면 보관했다가 시작할 때 적용한다.
     *            읽지 못하면(버전이나 길이가 맞지 않음) 알리고 새 판으로 시작한다.
     *          - `const vector<uint8>& getPendingStateBytes() const`: 시작 전에 보관한 바이트다(보관하는 컴포넌트만 둔다 — `DeferredStateRestore` 를 쓰면 그 틀이 대신한다).
     *            보관한 동안은 그 바이트가 곧 상태라 다시 뜰 때 그대로 싣는다 — 지금 값을 쓰면 세이브를 기본값으로 덮는다.
     *
     *          짝짓기는 언제나 컴포넌트 id 다. 같은 프로세스(핫 리로드)에서는 컴포넌트 id 가 복원된다.
     *          다른 실행의 세이브는 id 가 새로 나가므로, 씬 섹션을 읽은 쪽이 "저장된 id → 새 id" 표를 만들어 `remapComponentIDs` 로 바꾼다
     *          (오브젝트마다 저장된 컴포넌트와 다시 만든 컴포넌트를 안정 키(지역 id 키 `#<16진>`)와 타입으로 짝짓는다. 같은 타입 둘의 순서가 바뀌어도 맞는다).
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

        /**
         * @brief 저장 형식 판입니다. 키는 형식 이름이라 컴포넌트 이름을 바꾸면 올린다. 옛 판은 읽지 않는다 — 옛 이름 표는 두지 않는다.
         * @details 2: 게임 규칙 컴포넌트 *DirectorComponent → *GameModeComponent. 3: 플레이어 진행 묶음을 기능마다 컴포넌트(지갑 · 가방 · 퀘스트 일지 · 평판)로 나눴다. 4: 허브 흡수로 게임 디렉터가 키트 디렉터가 됐다(`CityGameModeComponent` 등). 5: 같은 까닭(`AdventureGameModeComponent`).
         */
        static constexpr uint32 kVersion = 5;

        ComponentStateStore();

        void clear()
        {
            _listEntry.clear();
            _mapKeyToIndex.clear();
        }
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
         * @brief 다른 실행의 세이브에서 오브젝트 하나의 저장된 컴포넌트 id(@p identity)를 다시 만든 @p object 의 컴포넌트 id 에 연결하는 매핑을 더합니다.
         * @details 안정 키(`ComponentStableKey`, 지역 id 키 `#<16진>`)와 타입이 같은 것끼리 짝짓는다. 키는 씬 파일의 부착 대상과 에디터 선택 복원이 쓰는 규칙이라, 같은 타입
         *          컴포넌트 둘의 순서가 바뀌어도 맞는다(타입 차례로 이으면 두 상태가 서로 바뀐다). 키가 맞지 않는 저장 항목은 표에 넣지 않아 `remapComponentIDs` 가 지운다.
         */
        static void          appendComponentIDRemap( const ObjectIdentity& identity, const GameObject& object, vector<SavedComponentID>& outListRemap );
        const vector<Entry>& getEntries() const { return _listEntry; }

        void write( Archive& outArchive ) const;
        /** @brief `write` 로 쓴 것을 읽습니다. 형식이 맞지 않거나 잘렸으면 false 를 반환하고 비워 둡니다. */
        [[nodiscard]] bool read( Archive& archive );

        /**
         * @brief @p manager 의 @p TComponent 마다 `serializeState` 로 써서 싣고, 실은 수를 반환합니다.
         * @details 매니저 타입을 템플릿으로 받아서, 이 헤더는 오브젝트 매니저를 include 하지 않는다. 부르는 쪽은 이미 include 했다.
         */
        template <typename TComponent, typename TManager>
        uint32 capture( SceneIterationKey key, TManager& manager )
        {
            const hashed_string& typeName = TComponent::StaticType()->_name;
            uint32               count    = 0;
            manager.template forEachComponentOfType<TComponent>( key, [this, &typeName, &count]( TComponent* pComponent )
            {
                vector<uint8> bytes;
                writeComponent<TComponent>( *pComponent, bytes );
                add( typeName, pComponent->getComponentID(), std::move( bytes ) );
                ++count;
            } );
            return count;
        }

        /**
         * @brief @p manager 의 @p TComponent 마다 짝지은 상태를 `restoreState` 로 넘기고, 넘긴 수를 반환합니다.
         * @details 컴포넌트를 먼저 모으고 순회 밖에서 넘긴다. `restoreState` 가 스폰한 오브젝트를 정리할 수 있는데, 순회 콜백 안(매니저 잠금)에서는 지울 수 없기 때문이다.
         */
        template <typename TComponent, typename TManager>
        uint32 restore( SceneIterationKey key, TManager& manager ) const
        {
            vector<TComponent*> listComponent;
            manager.template forEachComponentOfType<TComponent>( key, [&listComponent]( TComponent* pComponent )
            { listComponent.push_back( pComponent ); } );
            const hashed_string& typeName = TComponent::StaticType()->_name;
            uint32               count    = 0;
            for ( TComponent* pComponent : listComponent )
            {
                const Entry* pEntry = findEntry( typeName, pComponent->getComponentID() );
                if ( pEntry == nullptr )
                    continue;
                vector<uint8> bytes = pEntry->_bytes;
                restoreComponent<TComponent>( *pComponent, std::move( bytes ) );
                ++count;
            }
            return count;
        }

        /** @brief 컴포넌트 하나의 상태를 뜹니다 — 저장소와 엔진 코덱(`ComponentStateCodec`, 스트리밍 셀)이 함께 쓴다. 시작 전에 받아 보관한 바이트(`getPendingStateBytes`)가 있으면 그것입니다. */
        template <typename TComponent>
        static void writeComponent( Component& component, vector<uint8>& outBytes )
        {
            TComponent& typed = static_cast<TComponent&>( component );
            if constexpr ( ComponentStateStoreTraits::HasPendingState<TComponent>::value )
            {
                if ( typed.getPendingStateBytes().empty() == false )
                {
                    outBytes = typed.getPendingStateBytes();
                    return;
                }
            }
            Archive archive;
            StateIO::writeObject( typed, archive );
            archive.writeData( outBytes );
        }
        /** @brief 컴포넌트 하나에 상태를 돌려줍니다(`writeComponent` 의 짝). */
        template <typename TComponent>
        static void restoreComponent( Component& component, vector<uint8>&& bytes )
        {
            static_cast<TComponent&>( component ).restoreState( std::move( bytes ) );
        }

    private:
        /** @brief 항목의 짝짓기 키(타입 이름, 컴포넌트 id)입니다. */
        struct EntryKey
        {
            hashed_string _typeName{};
            uint64        _componentID{ 0 };

            bool   operator==( const EntryKey& other ) const { return _typeName == other._typeName && _componentID == other._componentID; }
            uint64 computeHash() const { return HashUtil::makeHash( _typeName.getHash(), _componentID ); }
        };

        /** @brief `_listEntry` 의 자리를 다시 적습니다. 같은 키가 둘이면 앞 것을 찾는다. */
        void rebuildIndex();

    private:
        vector<Entry>                   _listEntry;     ///< 더한 순서(쓰는 순서)
        unordered_map<EntryKey, uint32> _mapKeyToIndex; ///< 키 → `_listEntry` 자리 — 짝짓기가 컴포넌트마다 목록을 훑지 않는다
    };
} // namespace sw
