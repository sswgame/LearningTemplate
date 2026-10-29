/**
 * @file GameStateSnapshot.h
 * @brief 게임 상태 스냅샷 봉투(씬 오브젝트 · 파생의 리플렉션 상태 · 컴포넌트 상태)를 쓰고 읽으며, 스냅샷에 오른 컴포넌트 타입을 듭니다.
 * @details `GameInstance` 가 값으로 들고 수명주기 훅(`onBeforeStateSerialize` · `onAfterStateDeserialize`)을 그 사이에 끼워 부른다.
 *          봉투 형식과 씬 오브젝트 직렬화가 게임 인스턴스의 흐름(팩 설정 · 씬 흐름 · 세션)과 섞이지 않게 따로 섰다(언리얼 `USaveGame` 직렬화 도우미 자리).
 *          게임 범위 서비스가 아닌 까닭: 등록은 게임 인스턴스 생성자가 하고 봉투는 그 인스턴스의 훅과 함께만 뜻이 있다 — 찾아 쓰는 다른 쪽이 없다.
 *          스레드: 게임 스레드에서만 쓴다.
 */
#pragma once
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Serialization/Serializer/SerializePurpose.h"

#include "GameFramework/Base/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct TypeInfo;

    class GameObjectManager;

    /**
     * @class GameStateSnapshot
     * @brief 상태 스냅샷 봉투 하나의 형식과, 그 봉투에 실을 컴포넌트 타입 목록입니다.
     * @details 쓰기: `beginCapture`(저장소를 비우고 올린 타입의 상태를 싣고 세운 것을 걷는다) → 부르는 쪽 훅 → `writeEnvelope`.
     *          읽기: `readEnvelope`(씬 · 리플렉션 상태 · 컴포넌트 상태를 되살리고 올린 타입에 상태를 돌려준다) → 부르는 쪽 훅 → `endRestore`.
     */
    class SW_GF_API GameStateSnapshot
    {
    public:
        /** @brief 엔진 컴포넌트(지형)와 정적으로 등록된 프레임워크 컴포넌트를 올립니다. */
        GameStateSnapshot();
        ~GameStateSnapshot();

        GameStateSnapshot( const GameStateSnapshot& )            = delete;
        GameStateSnapshot& operator=( const GameStateSnapshot& ) = delete;

        /**
         * @brief PROPERTY 가 아닌 상태를 가진 컴포넌트 타입 하나를 상태 스냅샷에 올립니다 — 게임 인스턴스 생성자에서 한 줄로 부릅니다.
         * @details 상태를 저장하기 전에 그 타입마다 `writeState` 를 싣고(`ComponentStateStore::capture`), 복원한 뒤 `restoreState` 로 돌려준다(`restore`).
         *          등록은 함수 포인터라 게임 모듈과 수명이 같다. 핫 리로드가 새 인스턴스를 만들면 새 생성자가 다시 등록한다.
         *          부르는 .cpp 는 `GameObjectManager.h` 를 include 해야 한다. 순회 템플릿이 그때 인스턴스화된다.
         */
        template <typename TComponent>
        void registerStatefulComponent()
        {
            StatefulComponentType type;
            type._pCapture = &GameStateSnapshot::captureStateOf<TComponent>;
            type._pRestore = &GameStateSnapshot::restoreStateOf<TComponent>;
            _listStatefulType.push_back( type );
        }
        /**
         * @brief 상태 저장 전에 세운 런타임 오브젝트를 걷을 컴포넌트 타입 하나를 올립니다(`despawnViews()` 를 부른다).
         * @details 세운 것은 상태에서 파생된 결과일 뿐이라, 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다.
         *          걷어 두면(삭제 대기는 스냅샷이 건너뛴다) 다시 만든 컴포넌트가 시작하면서 세우고, 남은 컴포넌트는 다음 틱에 다시 세운다.
         *          걷기는 모든 타입의 상태를 실은 뒤에 돈다.
         */
        template <typename TComponent>
        void registerViewOwner()
        {
            StatefulComponentType type;
            type._pDespawn = &GameStateSnapshot::despawnViewsOf<TComponent>;
            _listStatefulType.push_back( type );
        }
        /** @brief 디렉터(`GameDirectorComponent` 파생) 하나를 올립니다. 상태를 싣고 돌려주며, 저장하기 전에 세운 것을 걷습니다. */
        template <typename TDirector>
        void registerDirector()
        {
            StatefulComponentType type;
            type._pCapture = &GameStateSnapshot::captureStateOf<TDirector>;
            type._pRestore = &GameStateSnapshot::restoreStateOf<TDirector>;
            type._pDespawn = &GameStateSnapshot::despawnViewsOf<TDirector>;
            _listStatefulType.push_back( type );
        }

        /**
         * @brief 봉투의 컴포넌트 상태 섹션입니다(`ComponentStateStore`).
         * @details 쓰기 훅(`onBeforeStateSerialize`)이 올린 타입 밖의 상태를 `capture<T>( manager )` 로 싣고, 읽기 훅(`onAfterStateDeserialize`)이 `restore<T>( manager )` 로 넘긴다.
         */
        ComponentStateStore& getComponentStateStore() { return *_pComponentStateStore; }

        /** @brief 쓰기 1 단계: 저장소를 비우고, 올린 타입의 상태를 모두 실은 뒤 세운 것을 모두 걷습니다. 부르는 쪽은 그 뒤 쓰기 훅을 부르고 `writeEnvelope` 로 잇는다. */
        void beginCapture();
        /**
         * @brief 쓰기 2 단계: 봉투(머리 · 씬 오브젝트 · @p pStateInstance 의 리플렉션 상태 · 컴포넌트 상태)를 한 번 직렬화해 @p outBytes 에 담고 저장소를 비웁니다.
         * @details 씬이 없는 것은 실패가 아니다(빈 섹션). 리플렉션 상태를 싣지 못하면 저장소를 비우고 false 입니다(이유는 직렬화기가 알린다).
         *          @p pStateType 이나 @p pStateInstance 가 없으면 리플렉션 섹션은 비었다.
         */
        [[nodiscard]] bool writeEnvelope( SerializePurpose purpose, const TypeInfo* pStateType, const void* pStateInstance, vector<uint8>& outBytes );
        /**
         * @brief 읽기: 같은 목적으로 쓴 봉투를 읽어 씬 오브젝트 · 리플렉션 상태 · 컴포넌트 상태를 되살리고, 올린 타입마다 실어 둔 상태를 돌려줍니다.
         * @details 성공하면 부르는 쪽은 읽기 훅을 부르고 `endRestore` 로 저장소를 비운다. 실패하면 false 이고 훅을 부르지 않는다.
         */
        [[nodiscard]] bool readEnvelope( SerializePurpose purpose, const TypeInfo* pStateType, void* pStateInstance, const void* pInBuffer, uint32 size );
        /** @brief 읽기 끝: 읽기 훅이 넘겨받은 뒤 저장소를 비웁니다. */
        void endRestore();

    private:
        /** @brief 상태 스냅샷에 오른 컴포넌트 타입 하나의 일입니다(`register*`). 없는 일은 nullptr 입니다. */
        struct StatefulComponentType
        {
            void ( *_pCapture )( ComponentStateStore& store, GameObjectManager& manager ){ nullptr };
            void ( *_pRestore )( const ComponentStateStore& store, GameObjectManager& manager ){ nullptr };
            void ( *_pDespawn )( GameObjectManager& manager ){ nullptr };
        };

        /** @brief `deserializeSceneObjects` 가 읽는 씬 오브젝트 데이터의 형식입니다. */
        enum class SceneObjectFormat : uint8
        {
            WithIdentity,   ///< 오브젝트마다 id + 상태. 다른 실행에서 찍은 것이라 id 는 읽고 버립니다(새 id).
            RestoreIdentity ///< 오브젝트마다 id + 상태. 같은 프로세스에서 찍었으므로 원래 id 를 복원합니다(핫 리로드).
        };

        /**
         * @brief 씬 안의 모든 유효 GameObject 를 바이너리로 직렬화합니다.
         * @details 오브젝트마다 상태 앞에 런타임 id(`ObjectIdentity`)를 싣습니다. 같은 프로세스에서 복원하면(핫 리로드)
         *          `GameObjectHandle` · `ComponentHandle` 이 그 너머로도 이어집니다.
         */
        [[nodiscard]] static bool serializeSceneObjects( SerializePurpose purpose, vector<uint8>& outBytes );
        /**
         * @brief 바이너리 데이터로부터 씬 GameObject 들을 복원합니다. @p format 은 id 가 실렸는지, 복원할지를 알려 줍니다.
         * @param outListRemap id 를 복원하지 않을 때(`WithIdentity`) 저장된 컴포넌트 id → 다시 만든 컴포넌트 id 표를 채웁니다(`ComponentStateStore::remapComponentIDs`).
         */
        [[nodiscard]] static bool deserializeSceneObjects( SerializePurpose purpose, const uint8* pData, size_t size, SceneObjectFormat format,
                                                           vector<ComponentStateStore::SavedComponentID>& outListRemap );
        /** @brief 올린 타입마다 실어 둔 상태를 돌려줍니다(읽기 훅 앞). */
        void restoreStatefulComponents();

        // 매니저 타입은 템플릿 매개변수로 둔다 — 이 헤더는 오브젝트 매니저를 include 하지 않고, 등록하는 .cpp 에서 만들어진다.
        template <typename TComponent, typename TManager = GameObjectManager>
        static void captureStateOf( ComponentStateStore& store, TManager& manager )
        {
            (void)store.capture<TComponent>( manager );
        }
        template <typename TComponent, typename TManager = GameObjectManager>
        static void restoreStateOf( const ComponentStateStore& store, TManager& manager )
        {
            (void)store.restore<TComponent>( manager ); // 넘긴 수는 쓰지 않는다 — 짝이 없는 컴포넌트는 그대로 둔다
        }
        template <typename TComponent, typename TManager = GameObjectManager>
        static void despawnViewsOf( TManager& manager )
        {
            // 순회 콜백 안에서는 오브젝트를 지울 수 없다(매니저 잠금 안) — 핸들을 모은 뒤 걷는다.
            vector<TComponentHandle<TComponent>> listComponent;
            manager.template forEachComponentOfType<TComponent>( [&listComponent]( TComponent* pComponent )
            { listComponent.push_back( TComponentHandle<TComponent>{ pComponent } ); } );
            for ( const TComponentHandle<TComponent>& handle : listComponent )
            {
                TComponent* pComponent = manager.resolveComponent( handle );
                if ( pComponent != nullptr )
                    pComponent->despawnViews();
            }
        }

        vector<StatefulComponentType>   _listStatefulType;     ///< 상태 스냅샷에 오른 컴포넌트 타입(등록 순서대로 싣고 걷고 돌려준다)
        unique_ptr<ComponentStateStore> _pComponentStateStore; ///< 스냅샷 봉투의 컴포넌트 상태 섹션
    };
} // namespace sw
