/**
 * @file GameStateSnapshot.h
 * @brief 게임 상태 스냅샷 봉투(씬 오브젝트 · 파생의 리플렉션 상태 · 컴포넌트 상태)를 쓰고 읽으며, 스냅샷에 오른 컴포넌트 타입을 듭니다.
 * @details `GameInstance` 가 값으로 들고 수명주기 훅(`onBeforeStateSerialize` · `onAfterStateDeserialize`)을 그 사이에 끼워 부른다.
 *          봉투 형식과 씬 오브젝트 직렬화가 게임 인스턴스의 흐름(팩 설정 · 씬 흐름 · 세션)과 섞이지 않게 따로 섰다(언리얼 `USaveGame` 직렬화 도우미 자리).
 *          게임 범위 서비스가 아닌 까닭: 등록은 게임 인스턴스 생성자가 하고 봉투는 그 인스턴스의 훅과 함께만 뜻이 있다 — 찾아 쓰는 다른 쪽이 없다.
 *          스레드: 게임 스레드에서만 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/SceneIterationKey.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Serialization/Serializer/SerializePurpose.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ComponentStateCodec;
    struct TypeInfo;

    /** @brief 봉투 읽기(`GameStateSnapshot::readEnvelope`)가 진 까닭입니다 — 부르는 쪽은 씬이 그대로인지에 따라 세이브를 막을지 정한다. */
    enum class SnapshotRestoreFailure : uint8
    {
        Rejected,   ///< 검증에서 졌다 — 씬 · 상태 인스턴스는 건드리지 않았다
        SceneBroken ///< 검증을 지난 뒤 교체에서 졌다 — 씬은 비워졌고 일부만 섰다(세이브를 막는다)
    };

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
         * @details 상태를 저장하기 전에 그 타입마다 `serializeState` 로 써서 싣고(`ComponentStateCodec::captureObject`), 복원한 뒤 `restoreState` 로 돌려준다(`restore`).
         *          등록은 함수 포인터라 게임 모듈과 수명이 같다. 핫 리로드가 새 인스턴스를 만들면 새 생성자가 다시 등록한다.
         *          부르는 .cpp 는 `GameObjectManager.h` 와 `ComponentStateCodec.h` 를 include 해야 한다. 템플릿이 그때 인스턴스화된다(이 헤더는 게임마다 읽혀 코덱의 포함을 끌지 않는다).
         */
        template <typename TComponent, typename TCodec = ComponentStateCodec>
        void registerStatefulComponent()
        {
            addCodec( TCodec::template make<TComponent>( nullptr ) );
        }
        /** @brief 디렉터(`GameModeComponent` 파생) 하나를 올립니다. 상태를 싣고 돌려주며, 저장하기 전에 세운 것을 걷습니다. */
        template <typename TDirector>
        void registerGameMode()
        {
            registerStatefulComponent<TDirector>();
            _listDespawn.push_back( &GameStateSnapshot::despawnComponentViews<TDirector> );
        }

        /** @brief 쓰기 1 단계: 코덱이 있는 컴포넌트의 상태를 오브젝트마다 모두 뜬 뒤 세운 것을 모두 걷습니다. 부르는 쪽은 그 뒤 쓰기 훅을 부르고 `writeEnvelope` 로 잇는다. */
        void beginCapture();
        /**
         * @brief 쓰기 2 단계: 봉투(머리 · 씬 오브젝트 · @p pStateInstance 의 리플렉션 상태 · 컴포넌트 상태 · 씬 몫)를 한 번 직렬화해 @p outBytes 에 담습니다.
         * @details 씬 몫은 늘 올라 있는 오브젝트의 파일 id · 스트리밍 셀 상태 · 데이터 레이어 상태다 — 셀 · 레이어 묶음에 든 오브젝트는 씬 섹션에서 빼고 그 상태로 싣는다
         *          (`SceneStreamingSubsystem::writeSaveState` · `SceneDataLayerSubsystem::writeSaveState`).
         * @param playTime 찍을 때까지의 플레이 시간(초)과 저장 시각은 머리에 싣는다(`readSaveInfo`).
         * @param minigameBytes 찍은 미니게임 칸 스택(`MinigameStack::writeSaveState`) — 머리에 실어 불러오기가 씬을 열기 전에 읽는다. 미니게임 밖이거나 세이브가 아니면 빈 바이트다.
         * @details 씬이 없는 것은 실패가 아니다(빈 섹션). 리플렉션 상태를 싣지 못하면 false 입니다(이유는 직렬화기가 알린다).
         *          @p pStateType 이나 @p pStateInstance 가 없으면 리플렉션 섹션은 비었다.
         */
        [[nodiscard]] bool writeEnvelope( SerializePurpose purpose, const TypeInfo* pStateType, const void* pStateInstance, float64 playTime, const vector<uint8>& minigameBytes, vector<uint8>& outBytes );
        /**
         * @brief 읽기: 같은 목적으로 쓴 봉투를 읽어 씬 오브젝트 · 리플렉션 상태 · 컴포넌트 상태를 되살리고, 올린 타입마다 실어 둔 상태를 돌려줍니다.
         * @details 두 단계다. 1) 검증 — 봉투를 끝까지 읽고 씬은 분리된 오브젝트 매니저에, 리플렉션 상태는 새로 만든 사본에 풀어 본다. 지금 씬과 @p pStateInstance 는 건드리지 않는다.
         *          2) 교체 — 검증을 지난 봉투만 활성 씬을 비우고 채운다. 언리얼 `LoadGameFromSlot` 이 별도 `USaveGame` 으로 풀고 월드는 성공 뒤에만 건드리는 나눔이다.
         *          성공하면 부르는 쪽은 읽기 훅을 부르고 `endRestore` 로 저장소를 비운다. 실패하면 훅을 부르지 않고, 까닭이 `Rejected`(아무것도 바꾸지 않았다)인지
         *          `SceneBroken`(교체 중에 졌다 — 세이브를 막아야 한다)인지 돌려준다.
         *          머리의 씬 정체(찍을 때 활성 씬의 리소스 id)가 지금 활성 씬과 다르면 `Rejected` 다 — 그 씬을 연 뒤에 읽는다(`readSceneID` 로 먼저 본다).
         *          리플렉션 상태가 있으면 그 타입은 기본 생성할 수 있어야 한다(검증 사본) — 아니면 `Rejected` 다.
         */
        [[nodiscard]] expected<void, SnapshotRestoreFailure> readEnvelope( SerializePurpose purpose, const TypeInfo* pStateType, void* pStateInstance, const void* pInBuffer,
                                                                           uint32 size );
        /**
         * @brief 옛 판 봉투 @p inoutBytes 를 이 빌드의 판까지 제자리에서 올립니다(형식의 단계 `FormatDef`) — 슬롯에서 읽은 바이트를 풀기 전에 한 번 부른다. 이 판이면 할 일이 없다.
         * @return 빠진 단계 · 단계 실패면 오류를 남기고 false 입니다. 머리가 없거나 새 판인 바이트는 true 다(읽기가 까닭을 남기고 거절한다).
         */
        [[nodiscard]] static bool upgradeEnvelope( vector<uint8>& inoutBytes );
        /**
         * @brief 봉투의 머리만 읽어 찍을 때 활성 씬의 지금 리소스 id 를 돌려줍니다(씬 없이 찍었으면 빈 글). 머리가 없거나 판이 다르거나 잘렸거나 그 씬이 없으면 없음입니다(까닭은 알린다).
         * @details 섹션은 풀지 않는다 — 불러오기가 그 씬을 먼저 열지 정하는 자리다(언리얼 게임이 세이브의 맵 이름을 보고 `OpenLevel` 하는 것과 같다).
         */
        [[nodiscard]] static optional<string> readSceneID( const void* pInBuffer, uint32 size );
        /** @brief 봉투의 머리만 읽어 찍을 때의 미니게임 칸 스택 바이트를 돌려줍니다(미니게임 밖이면 빈 바이트). 머리를 읽지 못하면 없음입니다(`readSceneID` 와 같은 길). */
        [[nodiscard]] static optional<vector<uint8>> readMinigameBytes( const void* pInBuffer, uint32 size );
        /** @brief 봉투 머리에 실린 이 세이브의 정보입니다(불러오기 목록 · 이어 하기의 플레이 시간). */
        struct SaveInfo
        {
            string           _sceneID;         ///< 찍은 씬의 지금 리소스 id(씬 없이 찍었으면 빈 글)
            float64          _playTime{ 0.0 }; ///< 찍을 때까지의 플레이 시간(초)
            UnixMilliseconds _savedTime{ 0 };  ///< 찍은 벽시계 시각(UTC)
        };
        /** @brief 봉투의 머리만 읽어 이 세이브의 정보를 돌려줍니다. 머리를 읽지 못하면 없음입니다(`readSceneID` 와 같은 길). */
        [[nodiscard]] static optional<SaveInfo> readSaveInfo( const void* pInBuffer, uint32 size );

    private:
        using DespawnFunction = void ( * )( SceneIterationKey key, GameObjectManager& manager );

        /** @brief 다시 선 오브젝트 하나에 돌려줄 컴포넌트 상태입니다. */
        struct RestoredState
        {
            uint64        _objectID;
            vector<uint8> _bytes;
        };

        /** @brief `deserializeSceneObjects` 가 읽는 씬 오브젝트 데이터의 형식입니다. */
        enum class SceneObjectFormat : uint8
        {
            WithIdentity,   ///< 오브젝트마다 id + 상태. 다른 실행에서 찍은 것이라 id 는 읽고 버립니다(새 id).
            RestoreIdentity ///< 오브젝트마다 id + 상태. 같은 프로세스에서 찍었으므로 원래 id 를 복원합니다(핫 리로드).
        };

        // 매니저 타입은 템플릿 매개변수로 둔다 — 이 헤더는 오브젝트 매니저를 include 하지 않고, 등록하는 .cpp 에서 만들어진다.
        template <typename TComponent, typename TManager = GameObjectManager>
        static void despawnComponentViews( SceneIterationKey key, TManager& manager )
        {
            // 순회 콜백 안에서는 오브젝트를 지울 수 없다(매니저 잠금 안) — 핸들을 모은 뒤 걷는다.
            vector<TComponentHandle<TComponent>> listComponent;
            manager.template forEachComponentOfType<TComponent>( key, [&listComponent]( TComponent* pComponent )
            { listComponent.push_back( TComponentHandle<TComponent>{ pComponent } ); } );
            for ( const TComponentHandle<TComponent>& handle : listComponent )
            {
                TComponent* pComponent = manager.resolveComponent( handle );
                if ( pComponent != nullptr )
                    pComponent->despawnViews();
            }
        }

        /**
         * @brief 씬 안의 모든 유효 GameObject 를 바이너리로 직렬화합니다. 스트리밍 셀 · 데이터 레이어 묶음에 든 오브젝트는 뺍니다(그 상태로 싣는다 — `isCellObject` · `isGroupObject`).
         * @details 오브젝트마다 상태 앞에 런타임 id(`ObjectIdentity`)를 싣습니다. 같은 프로세스에서 복원하면(핫 리로드)
         *          `GameObjectHandle` · `ComponentHandle` 이 그 너머로도 이어집니다.
         * @param outListObjectID 실은 오브젝트의 id 를 실은 차례로 채웁니다(씬 몫의 파일 id 표가 쓴다).
         */
        [[nodiscard]] bool serializeSceneObjects( SerializePurpose purpose, vector<uint8>& outBytes, vector<uint64>& outListObjectID ) const;
        /**
         * @brief 바이너리 데이터로부터 씬 GameObject 들을 @p manager 에 만듭니다. @p format 은 id 가 실렸는지, 복원할지를 알려 줍니다.
         * @details @p manager 를 비우지 않는다 — 검증은 분리된 매니저에, 교체는 부르는 쪽이 비운 활성 매니저에 푼다. 하나라도 못 읽으면 false 다.
         * @param outListState 오브젝트마다 실린 컴포넌트 상태를 채웁니다 — 모두 선 뒤 부르는 쪽이 돌려준다(`ComponentStateCodec::restoreObject`).
         * @param outMapObjectID 저장된 오브젝트 id → 다시 만든 오브젝트 id 표를 채웁니다(복원하면 같은 값 — 씬 몫의 파일 id 를 다시 붙인다).
         */
        [[nodiscard]] static bool deserializeSceneObjects( SerializePurpose purpose, const uint8* pData, size_t size, SceneObjectFormat format, GameObjectManager& manager,
                                                           vector<RestoredState>& outListState, unordered_map<uint64, uint64>& outMapObjectID );
        /** @brief 코덱을 이 스냅샷이 사는 동안 엔진에 등록합니다(이름은 형식의 것) — 세이브 · 핫 리로드 · 셀 · 레이어 묶음 · 하위 레벨 · 씬 전환이 함께 쓴다. */
        void addCodec( const ComponentStateCodec& codec );

    private:
        vector<DespawnFunction>                 _listDespawn;      ///< 저장 전에 세운 것을 걷는 일(등록 순서)
        vector<unique_ptr<ComponentStateCodec>> _listCodec;        ///< `addCodec` 이 등록한 코덱(주소가 흔들리지 않게 따로 든다 — 소멸자가 뺀다)
        unordered_map<uint64, vector<uint8>>    _mapCapturedState; ///< `beginCapture` 가 뜬 오브젝트 id → 컴포넌트 상태(`writeEnvelope` 가 싣고 비운다)
    };
} // namespace sw
