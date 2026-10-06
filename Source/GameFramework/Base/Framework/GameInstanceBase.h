/**
 * @file GameInstanceBase.h
 * @brief IGame 공통 수명주기 + BootstrapConfig 배선과 인스턴스 상태 스냅샷 직렬화입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskFuture.h"

#include "GameFramework/Base/Data/GameSettings.h"
#include "GameFramework/Base/Framework/ComponentStateStore.h"
#include "GameFramework/Base/Framework/IGame.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct TypeInfo;

    class GameObjectManager;
    class Scene;

    // ------------------------------------------------------------------------------
    // 1) GameInstanceBase — 수명주기는 여기, 파생은 훅만
    //    파생은 configureBootstrap 과 on* 훅을 오버라이드한다(예: EmptyGame)
    // ------------------------------------------------------------------------------
    /** @brief IGame 수명주기를 고정하고 부트스트랩 및 런타임 상태 스냅샷 직렬화를 지원합니다. */
    class SW_GF_API GameInstanceBase : public IGame
    {
    public:
        /** @brief 윈도우 · RHI · 부트스트랩을 비운 채로 둡니다. */
        GameInstanceBase();
        /** @brief 파생 인스턴스가 정리할 수 있게 합니다. */
        virtual ~GameInstanceBase() override;

        /**
         * @brief configureBootstrap 으로 부트스트랩을 채우고(게임 프리셋 `Config/Game/<게임>.json` 의 팩 루트가 있으면 그것이 우선) gamesettings 를 읽은 뒤 onInitialize 를 부릅니다.
         * @details 읽은 `GameSettings` 를 게임 서비스로 묶고(`game::getService<GameSettings>()`), 다국어(`_localizationProject`)와
         *          게임플레이 입력 맵(`_inputMap`)을 여기서 적용합니다. 커스텀 칸을 읽는 킷 코드는
         *          이 서비스로 읽습니다 — 서비스로 묶지 않으면 제품에서 늘 기본값을 씁니다.
         */
        bool initialize( IWindow* pWindow, IRHIDevice* pRhiDevice ) final;
        /** @brief onShutdown 뒤에 `GameSettings` 서비스를 풀고 윈도우 · RHI 포인터를 끊습니다. */
        void shutdown() final;
        /** @brief 끝난 씬 로드마다 `SceneLoadCompletedEvent` 를 낸 뒤 onUpdate 로 한 프레임을 넘깁니다. */
        void update( float32 deltaTime ) final;

        // --------------------------------------------------------------------------
        // 런타임 상태 직렬화 / 스냅샷 (Dev LiveReload & Shipping 체크포인트/세이브)
        // --------------------------------------------------------------------------
        /** @brief C-ABI: 활성 씬과 파생 클래스의 커스텀 리플렉션 상태를 통합 바이너리로 직렬화합니다. */
        [[nodiscard]] bool serializeState( void* pOutBuffer, uint32* pInOutSize ) override;

        /** @brief C-ABI: 바이너리 버퍼에서 씬과 커스텀 리플렉션 상태를 복원합니다. */
        [[nodiscard]] bool deserializeState( const void* pInBuffer, uint32 size ) override;

        /** @brief Shipping/Gameplay: 현재 씬과 게임 상태를 인메모리 스냅샷 버퍼에 캡처합니다 (체크포인트/타임리와인드용). */
        bool captureSnapshot( vector<uint8>& outBytes );

        /** @brief Shipping/Gameplay: 인메모리 스냅샷 버퍼로부터 씬과 게임 상태를 즉시 복원합니다. */
        [[nodiscard]] bool restoreSnapshot( const vector<uint8>& inBytes );

        /**
         * @brief Shipping/Gameplay: 씬과 게임 상태 전체를 바이너리 파일로 저장합니다. 경로를 비우면 기본 세이브 경로(`GameSettings::_defaultSavePath`)입니다.
         * @details 경로가 정해졌으면 성공 · 실패와 함께 `SaveGameSavedEvent` 를 "game" 채널에 냅니다. 경로가 없으면 알리고 아무것도 내지 않습니다.
         */
        [[nodiscard]] bool saveStateToFile( string_view filePath = {} );

        /**
         * @brief Shipping/Gameplay: 바이너리 파일로부터 씬과 게임 상태 전체를 복원합니다. 경로를 비우면 기본 세이브 경로입니다.
         * @details 경로가 정해졌으면 성공 · 실패와 함께 `SaveGameLoadedEvent` 를 "game" 채널에 냅니다.
         */
        [[nodiscard]] bool loadStateFromFile( string_view filePath = {} );

        // --------------------------------------------------------------------------
        // 부트스트랩 씬 흐름 (GameSettings 의 씬 칸을 읽는 자리)
        // --------------------------------------------------------------------------
        /** @brief 게임이 처음 여는 씬입니다 — `-gv_firstScene` > 타이틀 씬 > 시작 맵(gamesettings). 모두 비었으면 빈 문자열입니다. */
        const string& getFirstScene() const;
        /** @brief 타이틀 다음에 여는 씬입니다 — 입구 씬 > 시작 맵. */
        const string& getEntranceScene() const;
        /**
         * @brief `getFirstScene()` 의 로드를 요청합니다. 열 씬이 없거나 요청이 실패하면 false 입니다(실패는 알립니다).
         * @details 요청이 들어가면 `SceneLoadRequestedEvent` 를 내고, 그 로드가 끝난(활성 씬이 됐거나 실패한) 뒤의 첫 `update` 가
         *          `SceneLoadCompletedEvent` 를 냅니다.
         *          인스턴스가 살아 있는 월드 위에 다시 섰으면(게임 모듈 핫 리로드 · 백엔드 교체 — `initialize` 때 이미 활성 씬이 있었다) 요청하지 않고
         *          false 입니다. 그 씬은 곧 스냅샷으로 되살아나거나(핫 리로드) 에디터가 연 씬이다 — 언리얼 핫 리로드도 `GameInstance::Init` 을
         *          다시 부르지 않는다.
         */
        [[nodiscard]] bool requestFirstScene();
        /** @brief `getEntranceScene()` 의 로드를 요청합니다. 타이틀 화면이 "시작" 에서 부릅니다. 레벨 이벤트는 `requestFirstScene` 과 같습니다. */
        [[nodiscard]] bool requestEntranceScene();

    protected:
        /** @brief 파생 클래스가 팩 루트 · 부트스트랩을 설정합니다. */
        virtual void configureBootstrap( BootstrapConfig& outConfig ) { (void)outConfig; }
        /** @brief 부트스트랩을 읽은 뒤 부르는 파생 초기화 훅입니다. */
        virtual bool onInitialize() { return true; }
        /** @brief 파생 종료 훅입니다. */
        virtual void onShutdown() {}
        /** @brief 파생 프레임 훅입니다. */
        virtual void onUpdate( float32 deltaTime ) { (void)deltaTime; }

        // --------------------------------------------------------------------------
        // 파생 클래스 전용 상태 스냅샷 리플렉션 훅
        // --------------------------------------------------------------------------
        /** @brief 파생 클래스의 리플렉션 상태 TypeInfo 를 반환합니다(없으면 씬만 직렬화합니다). */
        virtual const TypeInfo* getStateTypeInfo() const { return nullptr; }
        /** @brief 파생 클래스의 상태 구조체 인스턴스 포인터를 반환합니다. */
        virtual void* getStateInstance() { return nullptr; }
        /** @brief 파생 클래스의 상태 구조체 const 인스턴스 포인터를 반환합니다. */
        virtual const void* getStateInstance() const { return nullptr; }

        /**
         * @brief 상태 스냅샷 직렬화 직전에 부르는 준비 훅입니다.
         * @details 부르기 전에 `getComponentStateStore()` 를 비우고, `register*` 로 올린 타입의 상태를 싣고 세운 것을 걷습니다. 그 밖의 상태는 여기서
         *          `getComponentStateStore().capture<T>( manager )` 로 싣습니다. 한 번의 캡처에 두 번 불릴 수 있습니다(크기 묻기 + 채우기).
         */
        virtual void onBeforeStateSerialize() {}
        /**
         * @brief 상태 스냅샷 역직렬화 직후에 부르는 복원 훅입니다.
         * @details 씬 오브젝트가 다시 서고 `register*` 로 올린 타입이 상태를 돌려받은 뒤입니다. 그 밖에 실어 둔 상태는
         *          `getComponentStateStore().restore<T>( manager )` 로 넘깁니다(이 훅이 끝나면 저장소를 비웁니다).
         */
        virtual void onAfterStateDeserialize() {}

        /** @brief 상태 스냅샷 봉투의 컴포넌트 상태 섹션입니다(`ComponentStateStore`). */
        ComponentStateStore& getComponentStateStore() { return *_pComponentStateStore; }

        /**
         * @brief PROPERTY 가 아닌 상태를 가진 컴포넌트 타입 하나를 상태 스냅샷에 올립니다 — 파생 생성자에서 한 줄로 부릅니다.
         * @details 상태 저장 전에 그 타입마다 `writeState` 를 싣고(`ComponentStateStore::capture`), 복원 뒤 `restoreState` 로 돌려줍니다(`restore`).
         *          등록은 함수 포인터라 게임 모듈과 수명이 같다 — 핫 리로드가 새 인스턴스를 만들면 새 생성자가 다시 등록한다.
         *          부르는 .cpp 는 `GameObjectManager.h` 를 include 합니다(순회 템플릿이 그때 만들어진다).
         */
        template <typename TComponent>
        void registerStatefulComponent()
        {
            StatefulComponentType type;
            type._pCapture = &GameInstanceBase::captureStateOf<TComponent>;
            type._pRestore = &GameInstanceBase::restoreStateOf<TComponent>;
            _listStatefulType.push_back( type );
        }
        /**
         * @brief 상태 저장 전에 세운 런타임 오브젝트를 걷을 컴포넌트 타입 하나를 올립니다(`despawnViews()` 를 부른다).
         * @details 세운 것은 상태의 모습일 뿐이다 — 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다. 걷어 두면(삭제 대기는 스냅샷이 건너뛴다)
         *          다시 만든 컴포넌트가 시작하며 세우고, 남은 컴포넌트는 다음 틱에 다시 세운다. 걷기는 모든 타입을 실은 뒤에 돈다.
         */
        template <typename TComponent>
        void registerViewOwner()
        {
            StatefulComponentType type;
            type._pDespawn = &GameInstanceBase::despawnViewsOf<TComponent>;
            _listStatefulType.push_back( type );
        }
        /** @brief 디렉터(`GameDirectorComponent` 파생) 하나를 올립니다 — 상태를 싣고 돌려주며, 저장 전에 세운 것을 걷습니다. */
        template <typename TDirector>
        void registerDirector()
        {
            StatefulComponentType type;
            type._pCapture = &GameInstanceBase::captureStateOf<TDirector>;
            type._pRestore = &GameInstanceBase::restoreStateOf<TDirector>;
            type._pDespawn = &GameInstanceBase::despawnViewsOf<TDirector>;
            _listStatefulType.push_back( type );
        }
        /** @brief 활성 씬의 오브젝트 매니저입니다. 게임 서비스가 묶이지 않았거나 활성 씬이 없으면 nullptr 입니다. */
        static GameObjectManager* findActiveObjectManager();

        /** @brief `deserializeSceneObjects` 가 읽는 씬 오브젝트 데이터의 형식입니다. */
        enum class SceneObjectFormat : uint8
        {
            WithIdentity,   ///< 오브젝트마다 id + 상태. 다른 실행에서 찍은 것이라 id 는 읽고 버립니다(새 id).
            RestoreIdentity ///< 오브젝트마다 id + 상태. 같은 프로세스에서 찍었으므로 원래 id 를 되살립니다(핫 리로드).
        };

        /**
         * @brief 씬 안의 모든 유효 GameObject 를 바이너리로 직렬화합니다.
         * @details 오브젝트마다 상태 앞에 런타임 id(`ObjectIdentity`)를 싣습니다. 같은 프로세스에서 되살리면(핫 리로드)
         *          `GameObjectHandle` · `ComponentHandle` 이 그 너머로도 이어집니다.
         */
        [[nodiscard]] bool serializeSceneObjects( vector<uint8>& outBytes );
        /** @brief 바이너리 데이터로부터 씬 GameObject 들을 복원합니다. @p format 은 id 가 실렸는지, 되살릴지를 알려 줍니다. */
        [[nodiscard]] bool deserializeSceneObjects( const uint8* pData, size_t size, SceneObjectFormat format );

        BootstrapConfig _bootstrap;  ///< 팩 루트와 gamesettings
        IWindow*        _pWindow;    ///< 호스트 윈도우 (App 이 소유)
        IRHIDevice*     _pRhiDevice; ///< 활성 RHI 디바이스

    private:
        /** @brief 상태 스냅샷에 오른 컴포넌트 타입 하나의 일입니다(`register*`). 없는 일은 nullptr 입니다. */
        struct StatefulComponentType
        {
            void ( *_pCapture )( ComponentStateStore& store, GameObjectManager& manager ){ nullptr };
            void ( *_pRestore )( const ComponentStateStore& store, GameObjectManager& manager ){ nullptr };
            void ( *_pDespawn )( GameObjectManager& manager ){ nullptr };
        };

        /** @brief 맡긴 씬 로드 하나입니다. 끝나면 `update` 가 `SceneLoadCompletedEvent` 를 내고 목록에서 뺍니다. */
        struct PendingSceneLoad
        {
            string             _scenePath;
            TaskFuture<Scene*> _future;
        };

        /** @brief `GameSettings` 의 다국어 · 입력 맵 칸을 적용합니다. 못 읽은 것은 알리고 넘어갑니다(게임은 뜬다). */
        void applyBootstrap();
        /**
         * @brief @p scenePath 의 로드를 `SceneManager` 에 맡기고 `SceneLoadRequestedEvent` 를 냅니다.
         * @return 경로가 비었으면 조용히 false, 맡기지 못했으면 알리고 false 입니다(그때는 이벤트도 없습니다).
         */
        [[nodiscard]] bool requestSceneLoad( const string& scenePath, const utf8* pWhich );
        /** @brief 끝난 씬 로드마다 `SceneLoadCompletedEvent` 를 냅니다(요청 순서). */
        void publishFinishedSceneLoads();
        /** @brief 올린 타입의 상태를 모두 싣고, 그다음 세운 것을 모두 걷습니다(`onBeforeStateSerialize` 앞). */
        void captureStatefulComponents();
        /** @brief 올린 타입마다 실어 둔 상태를 돌려줍니다(`onAfterStateDeserialize` 앞). */
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
            (void)store.restore<TComponent>( manager );
        }
        template <typename TComponent, typename TManager = GameObjectManager>
        static void despawnViewsOf( TManager& manager )
        {
            // 순회 콜백 안에서는 오브젝트를 지울 수 없다(매니저 잠금 안) — 핸들을 모은 뒤 걷는다.
            vector<ComponentHandle> listComponent;
            manager.template forEachComponentOfType<TComponent>( [&listComponent]( TComponent* pComponent )
            { listComponent.push_back( pComponent->getHandle() ); } );
            for ( const ComponentHandle& handle : listComponent )
            {
                TComponent* pComponent = static_cast<TComponent*>( manager.resolveComponent( handle ) );
                if ( pComponent != nullptr )
                    pComponent->despawnViews();
            }
        }

        vector<PendingSceneLoad>        _listPendingSceneLoad; ///< 맡겼지만 아직 끝을 알리지 않은 씬 로드
        vector<StatefulComponentType>   _listStatefulType;     ///< 상태 스냅샷에 오른 컴포넌트 타입(등록 순서대로 싣고 걷고 돌려준다)
        unique_ptr<ComponentStateStore> _pComponentStateStore; ///< 스냅샷 봉투의 컴포넌트 상태 섹션
        uint8                           _bResumingWorld;       ///< `initialize` 때 이미 활성 씬이 있었다(다시 선 인스턴스) — 첫 씬을 요청하지 않는다
    };
} // namespace sw
