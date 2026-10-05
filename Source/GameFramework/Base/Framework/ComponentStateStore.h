/**
 * @file ComponentStateStore.h
 * @brief PROPERTY 가 아닌 컴포넌트 런타임 상태(디렉터의 시뮬레이션)를 게임 상태 스냅샷에 실어 핫 리로드 · 세이브를 넘기는 저장소입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ComponentStateStore
     * @brief 컴포넌트마다 바이트 덩어리 하나를 (타입 이름 · 컴포넌트 id) 로 들고, 게임 상태 봉투(`GameInstanceBase::serializeState`)의 한 섹션으로 씁니다.
     * @details 리플렉션 PROPERTY 는 씬 오브젝트 스냅샷이 이미 옮깁니다. 디렉터가 키트의 보통 클래스로 든 시뮬레이션(밭 · 도시 · 전장)은 PROPERTY 가
     *          아니라서 여기에 싣습니다 — 언리얼 `UObject::Serialize` 오버라이드 · 유니티 `ISerializationCallbackReceiver` 의 자리입니다.
     *
     *          컴포넌트 쪽 약속(템플릿이 이름으로 부른다):
     *          - `void writeState( Archive& outArchive ) const` — 지금 상태를 씁니다. 첫 값은 그 컴포넌트의 형식 버전으로 둡니다.
     *          - `void restoreState( vector<uint8>&& bytes )` — 다시 만든 컴포넌트가 받습니다. 아직 `onBeginPlay` 전이면 들고 있다가 시작할 때 적용하고,
     *            읽지 못하면(버전 · 길이가 맞지 않음) 알리고 새 판으로 시작합니다.
     *
     *          짝짓기: 같은 프로세스(핫 리로드)는 컴포넌트 id 가 되살아나므로 id 로, 다른 실행의 세이브는 id 가 새로 나가므로 같은 타입 안의 순서로 찾습니다.
     */
    class SW_GF_API ComponentStateStore
    {
    public:
        /** @brief 컴포넌트 하나의 상태입니다. */
        struct Entry
        {
            hashed_string _typeName{};
            uint64        _componentId{ 0 };
            vector<uint8> _bytes{};
        };

        static constexpr uint32 kFormatVersion = 1;

        ComponentStateStore();

        void clear() { _listEntry.clear(); }
        bool isEmpty() const { return _listEntry.empty(); }
        /** @brief 상태 하나를 더합니다. 같은 (타입 · id) 가 이미 있으면 바꿉니다. */
        void add( const hashed_string& typeName, uint64 componentId, vector<uint8>&& bytes );
        /**
         * @brief 컴포넌트 하나의 상태를 찾습니다. 없으면 nullptr 입니다.
         * @param orderInType 같은 타입 안에서 이 컴포넌트의 차례입니다 — id 가 맞는 것이 없을 때 그 차례의 항목을 씁니다(다른 실행의 세이브).
         */
        const Entry*         findEntry( const hashed_string& typeName, uint64 componentId, uint32 orderInType ) const;
        const vector<Entry>& getEntries() const { return _listEntry; }

        void write( Archive& outArchive ) const;
        /** @brief `write` 로 쓴 것을 읽습니다. 형식이 맞지 않거나 잘렸으면 false 이고 비워 둡니다. */
        [[nodiscard]] bool read( Archive& archive );

        /**
         * @brief @p manager 의 @p TComponent 마다 `writeState` 를 불러 싣습니다. 실은 수입니다.
         * @details 매니저 타입을 템플릿으로 받아 이 헤더가 오브젝트 매니저를 include 하지 않습니다(부르는 쪽은 이미 include 했다).
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
                add( typeName, pComponent->getComponentId(), std::move( bytes ) );
                ++count;
            } );
            return count;
        }

        /**
         * @brief @p manager 의 @p TComponent 마다 짝지은 상태를 `restoreState` 로 넘깁니다. 넘긴 수입니다.
         * @details 컴포넌트를 먼저 모으고 순회 밖에서 넘깁니다 — `restoreState` 가 세운 오브젝트를 걷을 수 있고, 순회 콜백 안(매니저 잠금)에서는 지울 수 없다.
         */
        template <typename TComponent, typename TManager>
        uint32 restore( TManager& manager ) const
        {
            vector<TComponent*> listComponent;
            manager.template forEachComponentOfType<TComponent>( [&listComponent]( TComponent* pComponent )
            { listComponent.push_back( pComponent ); } );
            const hashed_string& typeName = TComponent::StaticType()->_name;
            uint32               count    = 0;
            for ( uint32 order = 0; order < static_cast<uint32>( listComponent.size() ); ++order )
            {
                TComponent*  pComponent = listComponent[order];
                const Entry* pEntry     = findEntry( typeName, pComponent->getComponentId(), order );
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
