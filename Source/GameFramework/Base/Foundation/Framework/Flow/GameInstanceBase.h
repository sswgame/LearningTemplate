/**
 * @file GameInstanceBase.h
 * @brief IGame 의 공통 수명주기와 GamePackConfig 배선, 인스턴스 상태 스냅샷 직렬화를 담당합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Task/TaskFuture.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Serialization/Serializer/SerializePurpose.h"

#include "GameFramework/Base/Foundation/Framework/Flow/IGame.h"
#include "GameFramework/Base/Foundation/Framework/Flow/ScreenTransitionManager.h"
#include "GameFramework/Base/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/Base/Foundation/GameData/GameSettings.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct TypeInfo;

    class GameObjectManager;
    class InputManager;
    class LoadingScreenController;
    class Scene;
    class UISystem;
    class UserSettingsManager;
    class Widget;

    // ------------------------------------------------------------------------------
    // 1) GameInstanceBase — 수명주기는 여기, 파생은 훅만
    //    파생은 configurePackConfig 과 on* 훅을 오버라이드한다(예: EmptyGame)
    // ------------------------------------------------------------------------------
    /** @brief IGame 수명주기를 고정하고 팩 설정 및 런타임 상태 스냅샷 직렬화를 지원합니다. */
    class SW_GF_API GameInstanceBase : public IGame
    {
    public:
        /** @brief 윈도우 · RHI · 팩 설정을 비운 채로 둡니다. */
        GameInstanceBase();
        /** @brief 파생 인스턴스가 정리할 수 있게 합니다. */
        virtual ~GameInstanceBase() override;

        /**
         * @brief configurePackConfig 으로 팩 설정을 채우고 gamesettings 를 읽은 뒤 onInitialize 를 부릅니다.
         * @details 팩 루트는 게임 프리셋 `Config/Game/<게임>.json` 의 값이다(그 파일의 유일한 칸). gamesettings 를 못 읽으면 false 다(기본값 폴백 없음).
         *          읽은 `GameSettings` 는 게임 서비스(`game::findSubsystem<GameSettings>()`)로 등록하고, 다국어(`_localizationProject`)와
         *          게임플레이 입력 맵(`_inputMap`)도 여기서 적용한다.
         *          커스텀 칸을 읽는 킷 코드는 이 서비스로 읽는다. 서비스로 등록하지 않으면 제품에서 늘 기본값을 쓴다.
         *          사운드 이벤트 라이브러리(`_audioEvents`)도 `onInitialize` 앞에 올리고 `shutdown` 이 내린다. 적었는데 못 읽거나 `onInitialize` 가 false 면 false 다
         *          (필수 데이터 — `loadRequiredData` — 를 못 읽은 게임은 뜨지 않는다).
         */
        [[nodiscard]] bool initialize( IWindow* pWindow, IRHIDevice* pRHIDevice ) final;
        /** @brief onShutdown 뒤에 `GameSettings` 서비스를 풀고 윈도우 · RHI 포인터를 끊습니다. */
        void shutdown() final;
        /**
         * @brief 끝난 씬 로드마다 `SceneLoadCompletedEvent` 를 낸 뒤 onUpdate 로 한 프레임을 넘깁니다.
         * @details 그 앞에 화면 전환(페이드)을 진행하고 로딩 화면을 갱신한다. 씬 로드 요청이 남았거나 씬 매니저가 전환 중이면 로딩 중으로 본다.
         */
        void update( float32 deltaTime ) final;

        // --------------------------------------------------------------------------
        // 런타임 상태 직렬화 / 스냅샷 (Dev LiveReload & Shipping 체크포인트/세이브)
        // --------------------------------------------------------------------------
        /** @brief C-ABI(모듈 핫 리로드): 활성 씬과 파생 클래스의 커스텀 리플렉션 상태를 스냅샷 목적(`SerializePurpose::Snapshot`)의 통합 바이너리로 직렬화합니다. */
        [[nodiscard]] bool serializeState( vector<uint8>& outBytes ) override;

        /** @brief C-ABI(모듈 핫 리로드): 스냅샷 목적의 바이너리 버퍼에서 씬과 커스텀 리플렉션 상태를 복원합니다. */
        [[nodiscard]] bool deserializeState( const void* pInBuffer, uint32 size ) override;

        /**
         * @brief Shipping/Gameplay: 현재 씬과 게임 상태를 인메모리 버퍼에 캡처합니다(체크포인트 · 타임 리와인드는 `Snapshot`, 세이브 파일은 `SaveGame`).
         * @details 같은 봉투(씬 오브젝트, 커스텀 상태, 컴포넌트 상태)를 두 목적이 함께 쓰므로 목적은 부르는 쪽이 정한다. 읽는 쪽(`restoreSnapshot`)도 같은 목적이어야 한다.
         *          봉투는 한 번 직렬화해 @p outBytes 에 담는다. C-ABI 의 `serializeState` 도 이 함수를 한 번 부른다.
         */
        bool captureSnapshot( SerializePurpose purpose, vector<uint8>& outBytes );

        /** @brief Shipping/Gameplay: `captureSnapshot` 과 같은 목적으로 찍은 버퍼로부터 씬과 게임 상태를 즉시 복원합니다. */
        [[nodiscard]] bool restoreSnapshot( SerializePurpose purpose, const vector<uint8>& inBytes );

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
        // 팩 설정 씬 흐름 (GameSettings 의 씬 칸을 읽는 자리)
        // --------------------------------------------------------------------------
        /** @brief 게임이 처음 여는 씬입니다 — `-gv_firstScene` > 타이틀 씬 > 시작 맵(gamesettings). 모두 비었으면 빈 문자열입니다. */
        const string& getFirstScene() const;
        /** @brief 타이틀 다음에 여는 씬입니다 — 입구 씬 > 시작 맵. */
        const string& getEntranceScene() const;
        /**
         * @brief `getFirstScene()` 의 로드를 요청합니다. 열 씬이 없거나 요청이 실패하면 false 입니다(실패는 알립니다).
         * @details 요청이 들어가면 `SceneLoadRequestedEvent` 를 내고, 그 로드가 끝난(활성 씬이 됐거나 실패한) 뒤의 첫 `update` 가
         *          `SceneLoadCompletedEvent` 를 냅니다.
         *          인스턴스가 살아 있는 월드 위에 다시 섰으면 요청하지 않고 false 를 반환한다.
         *          게임 모듈 핫 리로드나 백엔드 교체가 그 경우이며, `initialize` 때 이미 활성 씬이 있었다는 뜻이다.
         *          그 씬은 곧 스냅샷으로 되살아나거나 에디터가 연 씬이다. 언리얼의 핫 리로드도 `GameInstance::Init` 을 다시 부르지 않는다.
         */
        [[nodiscard]] bool requestFirstScene();
        /** @brief `getEntranceScene()` 의 로드를 요청합니다. 타이틀 화면이 "시작" 에서 부릅니다. 레벨 이벤트는 `requestFirstScene` 과 같습니다. */
        [[nodiscard]] bool requestEntranceScene();
        /**
         * @brief gamesettings 의 타이틀 씬(`titleScene`) 로드를 요청합니다. 타이틀 씬이 없거나 요청이 실패하면 false 입니다. 레벨 이벤트는 `requestFirstScene` 과 같습니다.
         * @details 타이틀 씬이 있는 게임이면 `initialize` 가 UI 전역 명령 `ReturnToTitle` 로 이 함수를 등록해 일시정지 메뉴에 "타이틀로" 가 보인다
         *          (언리얼 Lyra 의 "Quit to Main Menu", 유니티 템플릿의 메뉴 씬 로드). 없는 게임은 등록하지 않아 단추가 접힌 채다.
         */
        [[nodiscard]] bool requestTitleScene();

        // --------------------------------------------------------------------------
        // 게임 인스턴스 서브시스템
        // --------------------------------------------------------------------------
        /**
         * @brief 이 인스턴스가 소유한 서브시스템 @p T 입니다(`ScreenTransitionManager` · `LoadingScreenController`). 생성 뒤에는 늘 있다.
         * @details 언리얼 `UGameInstance::GetSubsystem<T>()` 자리다. 게임 범위에 있으므로 키트는 인스턴스 없이 `game::findSubsystem<T>()` 로도 찾는다.
         *          화면 전환은 FadeOut → 실행 → FadeIn 을, 로딩 화면은 씬 로드 동안 gamesettings 의 `_loadingScreen` 문서를 맡는다.
         *          네트워크 세션(`GameNetSession`)은 엔진 수명이라 여기 없다 — 키트와 게임은 `game::findSubsystem<GameNetSession>()` 의 라우터에 처리기를 단다.
         */
        template <class T>
        T& getSubsystem() const
        {
            return _gameScope.get<T>();
        }

        /** @brief 활성 씬의 오브젝트 매니저입니다. 게임 서비스가 묶이지 않았거나 활성 씬이 없으면 nullptr 입니다(개발 명령 · 치트가 대상을 찾는 데도 쓴다). */
        static GameObjectManager* findActiveObjectManager();

    protected:
        /**
         * @brief 타이틀 씬이 있으면 @p ui 의 전역 명령 `ReturnToTitle` 을 등록합니다(`initialize` 가 UI 가 있을 때 부른다). `unregisterSessionCommands` 가 거둡니다.
         * @details UI 를 인자로 받는 까닭: 창 없이 게임 인스턴스를 세우는 테스트가 UI 를 직접 줘서 "타이틀로" 흐름(등록 · 누르면 타이틀 씬 로드)을 본다.
         */
        void registerSessionCommands( UISystem& ui );
        void unregisterSessionCommands( UISystem& ui );
        /** @brief 파생 클래스가 팩 루트 · 팩 설정을 설정합니다. */
        virtual void configurePackConfig( GamePackConfig& outConfig ) { (void)outConfig; }
        /** @brief 팩의 gamesettings 를 `_packConfig` 으로 읽습니다. 실패하면 게임 초기화가 실패합니다(기본값으로 서지 않는다). 팩 없이 만드는 테스트만 XML 문자열(`loadFromXMLText`)로 오버라이드합니다. */
        [[nodiscard]] virtual bool loadGameSettings();
        /** @brief 팩 설정을 읽은 뒤 부르는 파생 초기화 훅입니다. */
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
         * @details 부르기 전에 `getComponentStateStore()` 를 비우고, `register*` 로 올린 타입의 상태를 싣고 세운 것을 걷는다.
         *          그 밖의 상태는 여기서 `getComponentStateStore().capture<T>( manager )` 로 싣는다.
         *          한 번의 캡처에 두 번 불릴 수 있다(크기 묻기와 채우기).
         */
        virtual void onBeforeStateSerialize() {}
        /**
         * @brief 상태 스냅샷 역직렬화 직후에 부르는 복원 훅입니다.
         * @details 씬 오브젝트가 다시 서고 `register*` 로 올린 타입이 상태를 돌려받은 뒤에 불린다.
         *          그 밖에 실어 둔 상태는 `getComponentStateStore().restore<T>( manager )` 로 넘긴다. 이 훅이 끝나면 저장소를 비운다.
         */
        virtual void onAfterStateDeserialize() {}

        /** @brief 상태 스냅샷 봉투의 컴포넌트 상태 섹션입니다(`ComponentStateStore`). */
        ComponentStateStore& getComponentStateStore() { return *_pComponentStateStore; }

        /**
         * @brief PROPERTY 가 아닌 상태를 가진 컴포넌트 타입 하나를 상태 스냅샷에 올립니다 — 파생 생성자에서 한 줄로 부릅니다.
         * @details 상태를 저장하기 전에 그 타입마다 `writeState` 를 싣고(`ComponentStateStore::capture`), 복원한 뒤 `restoreState` 로 돌려준다(`restore`).
         *          등록은 함수 포인터라 게임 모듈과 수명이 같다. 핫 리로드가 새 인스턴스를 만들면 새 생성자가 다시 등록한다.
         *          부르는 .cpp 는 `GameObjectManager.h` 를 include 해야 한다. 순회 템플릿이 그때 인스턴스화된다.
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
         * @details 세운 것은 상태에서 파생된 결과일 뿐이라, 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다.
         *          걷어 두면(삭제 대기는 스냅샷이 건너뛴다) 다시 만든 컴포넌트가 시작하면서 세우고, 남은 컴포넌트는 다음 틱에 다시 세운다.
         *          걷기는 모든 타입의 상태를 실은 뒤에 돈다.
         */
        template <typename TComponent>
        void registerViewOwner()
        {
            StatefulComponentType type;
            type._pDespawn = &GameInstanceBase::despawnViewsOf<TComponent>;
            _listStatefulType.push_back( type );
        }
        /** @brief 디렉터(`GameDirectorComponent` 파생) 하나를 올립니다. 상태를 싣고 돌려주며, 저장하기 전에 세운 것을 걷습니다. */
        template <typename TDirector>
        void registerDirector()
        {
            StatefulComponentType type;
            type._pCapture = &GameInstanceBase::captureStateOf<TDirector>;
            type._pRestore = &GameInstanceBase::restoreStateOf<TDirector>;
            type._pDespawn = &GameInstanceBase::despawnViewsOf<TDirector>;
            _listStatefulType.push_back( type );
        }
        /**
         * @brief 이 인스턴스의 게임 범위에 객체(카탈로그 · 데이터베이스 — 보통 파생의 멤버)를 겁니다. `game::findSubsystem<T>()` 가 찾는다.
         * @details 범위는 이 인스턴스가 소유하므로 인스턴스가 내려가면 함께 풀린다 — 짝으로 풀 일이 없다. 같은 타입이 이미 걸려 있으면
         *          알리고 걸지 않는다(키트 둘이 같은 카탈로그 타입을 서비스로 쓰는 조립 — 나중 것이 앞 것을 소리 없이 덮지 않는다).
         */
        template <class T>
        void bindScopedService( T* pService )
        {
            if ( _gameScope.bind( pService ) == false )
                reportScopedServiceConflict( getSubsystemTypeHash<T>() );
        }
        /** @brief 이 인스턴스의 게임 범위입니다(기동 뒤 `game::findGameScope()` 와 같다). */
        SubsystemScope& getGameScope() { return _gameScope; }

        /**
         * @brief 필수 데이터 하나를 이 게임 팩에서 읽습니다. 경로는 데이터 종류 등록 줄(@p kindLabel)과 팩 루트로 만든다(`GamePackDataUtil::tryMakeKindPath`).
         * @details 게임 코드는 경로를 적지 않는다 — 카탈로그 경로의 기준은 등록 줄 하나다.
         *          경로를 만들 수 없거나 @p load( path ) 가 false 면 종류 · 파일 · 이유를 오류로 남기고 false 를 돌려준다.
         *          `onInitialize` 는 그 false 를 그대로 돌려준다. 필수 데이터 없이 게임을 띄우지 않는다(모듈 초기화 실패로 기동이 멈춘다).
         */
        template <typename TLoad>
        [[nodiscard]] bool loadRequiredData( string_view kindLabel, TLoad&& load )
        {
            string path;
            if ( makeRequiredDataPath( kindLabel, path ) == false )
                return false;
            if ( load( static_cast<const string&>( path ) ) )
                return true;
            reportRequiredDataFailure( kindLabel, AssetID{ path } );
            return false;
        }
        /** @brief 카탈로그(종류 `TCatalog::kDataKind`)를 `loadFromAsset` 로 읽고 게임 범위에 겁니다. 실패는 `loadRequiredData` 와 같습니다. */
        template <class TCatalog>
        [[nodiscard]] bool loadRequiredCatalog( TCatalog& catalog )
        {
            if ( loadRequiredData( TCatalog::kDataKind, [&catalog]( const string& path )
            { return catalog.loadFromAsset( AssetID{ path } ); } ) == false )
                return false;
            bindScopedService( &catalog );
            return true;
        }

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
        [[nodiscard]] bool serializeSceneObjects( SerializePurpose purpose, vector<uint8>& outBytes );
        /**
         * @brief 바이너리 데이터로부터 씬 GameObject 들을 복원합니다. @p format 은 id 가 실렸는지, 복원할지를 알려 줍니다.
         * @param outListRemap id 를 복원하지 않을 때(`WithIdentity`) 저장된 컴포넌트 id → 다시 만든 컴포넌트 id 표를 채웁니다(`ComponentStateStore::remapComponentIDs`).
         */
        [[nodiscard]] bool deserializeSceneObjects( SerializePurpose purpose, const uint8* pData, size_t size, SceneObjectFormat format,
                                                    vector<ComponentStateStore::SavedComponentID>& outListRemap );
        /** @brief `captureSnapshot` 이 같은 목적으로 쓴 봉투를 읽습니다. */
        [[nodiscard]] bool readStateEnvelope( SerializePurpose purpose, const void* pInBuffer, uint32 size );

        GamePackConfig _packConfig; ///< 팩 루트와 gamesettings
        IWindow*       _pWindow;    ///< 호스트 윈도우 (App 이 소유)
        IRHIDevice*    _pRHIDevice; ///< 활성 RHI 디바이스

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

        /**
         * @brief 기동(`initialize`)이 쓰는 엔진 서비스를 한 번 받아 듭니다(언리얼 `InitializeDependency`). 쓰는 곳은 다시 찾지 않습니다.
         * @details 네트워크 세션은 모든 호스트의 필수 서비스다. UI · 사용자 설정은 Client 호스트 서비스라 창이 있는 호스트(클라이언트)에서는 필수이고,
         *          창이 없는 호스트(전용 서버 · 테스트)에서만 없다. 필수 서비스가 없으면 오류를 남기고 false 입니다(인스턴스가 서지 않는다).
         */
        [[nodiscard]] bool bindEngineServices( const IWindow* pWindow );
        /**
         * @brief `GameSettings` 의 다국어 · 입력 맵 칸을 적용합니다. 파일을 못 읽은 것은 알리고 넘어갑니다(게임은 뜬다).
         * @return 입력 맵을 적었는데 입력 서비스(`InputManager`)가 없으면 오류를 남기고 false 입니다 — 호스트의 결함이라 조용히 건너뛰지 않는다.
         */
        [[nodiscard]] bool applyPackConfig();
        /** @brief gamesettings 의 사운드 이벤트 라이브러리(`_audioEvents`)를 올립니다. 적었는데 못 읽으면 오류를 남기고 false 입니다(적지 않았으면 true). */
        [[nodiscard]] bool loadAudioEvents();
        /** @brief `loadRequiredData` 의 경로를 만듭니다. 못 만들면 이유를 오류로 남기고 false 입니다. */
        [[nodiscard]] bool makeRequiredDataPath( string_view kindLabel, string& outPath ) const;
        /** @brief 필수 데이터 @p path 를 읽지 못했음을 파일과 이유로 알립니다. */
        void reportRequiredDataFailure( string_view kindLabel, const AssetID& assetID ) const;
        /** @brief 이 인스턴스의 게임 범위를 지금 범위 사슬에서 뗍니다(지금 범위면 앞 범위로 되돌리고, 다른 범위가 이 범위를 상위로 이었으면 그 위를 잇게 한다). */
        void detachGameScope();
        /** @brief 게임 범위에 같은 타입이 이미 걸려 있음을 알립니다(`bindScopedService`). */
        void reportScopedServiceConflict( uint64 typeHash ) const;
        /**
         * @brief @p scenePath 의 로드를 `SceneManager` 에 맡기고 `SceneLoadRequestedEvent` 를 냅니다.
         * @return 경로가 비었으면 조용히 false, 맡기지 못했으면 알리고 false 입니다(그때는 이벤트도 없습니다).
         */
        [[nodiscard]] bool requestLoadScene( const string& scenePath, const utf8* pWhich );
        /** @brief 끝난 씬 로드마다 `SceneLoadCompletedEvent` 를 냅니다(요청 순서). */
        void publishFinishedSceneLoads();
        /** @brief UI 전역 명령 `ReturnToTitle` 의 처리 함수입니다(일시정지 메뉴의 "타이틀로"). */
        void onReturnToTitleCommand( const hashed_string& command, Widget& source );
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

        vector<PendingSceneLoad>        _listPendingSceneLoad;      ///< 맡겼지만 아직 끝을 알리지 않은 씬 로드
        vector<StatefulComponentType>   _listStatefulType;          ///< 상태 스냅샷에 오른 컴포넌트 타입(등록 순서대로 싣고 걷고 돌려준다)
        unique_ptr<ComponentStateStore> _pComponentStateStore;      ///< 스냅샷 봉투의 컴포넌트 상태 섹션
        SubsystemScope                  _gameScope;                 ///< 게임 범위 — 인스턴스 서브시스템(화면 전환 · 로딩 화면)을 소유하고, 기동부터 종료까지 `game::findSubsystem<T>()` 의 지금 범위다
        string                          _loadedAudioEvents;         ///< `loadAudioEvents` 가 올린 사운드 이벤트 파일 — `shutdown` 이 내린다(비면 올린 것 없음)
        UISystem*                       _pUISystem;                 ///< `bindEngineServices` 가 받은 UI(빌림) — 창이 없는 호스트에서만 nullptr
        Subscription                    _returnToTitleRegistration; ///< UI 전역 명령 ReturnToTitle 의 등록 구독(`registerSessionCommands` 가 받는다)
        UserSettingsManager*            _pUserSettings;             ///< `bindEngineServices` 가 받은 사용자 설정(빌림) — 창이 없는 호스트에서만 nullptr
        InputManager*                   _pInput;                    ///< `bindEngineServices` 가 받은 입력(빌림) — 없으면 입력 맵을 적은 게임이 서지 않는다(`applyPackConfig`)
        uint8                           _bResumingWorld;            ///< `initialize` 때 이미 활성 씬이 있었다(다시 선 인스턴스) — 첫 씬을 요청하지 않는다
    };
} // namespace sw
