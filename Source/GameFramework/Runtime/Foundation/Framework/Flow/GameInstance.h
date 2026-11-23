/**
 * @file GameInstance.h
 * @brief IGame 의 공통 수명주기와 GamePackConfig 배선을 맡고, 상태 스냅샷 봉투(`GameStateSnapshot`)에 수명주기 훅을 끼워 부릅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Serialization/Serializer/SerializePurpose.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IContentConfiguration.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IGame.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/ScreenTransitionManager.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/GameStateSnapshot.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/SaveLock.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

namespace sw
{
    struct TypeInfo;

    class AppearanceDatabase;
    class AppearanceDataWatcher;
    class AutosaveManager;
    class GameFeatureScope;
    class GameLocalStore;
    class IGameDevPart;
    class GameObjectManager;
    class ItemCatalog;
    class NetMessageChannel;

    // ------------------------------------------------------------------------------
    // 1) GameInstance — 수명주기는 여기, 파생은 훅만
    //    파생은 configurePackConfig 과 on* 훅을 오버라이드한다(예: PlaygroundGame)
    // ------------------------------------------------------------------------------
    /**
     * @brief IGame 수명주기를 고정하고 팩 설정 및 런타임 상태 스냅샷 직렬화를 지원합니다.
     * @details **프로세스당 하나만 돈다.** 게임 범위의 지금 범위(`game::findGameScope`)와 서비스 창구는 프로세스에 하나라, 인스턴스 둘이 함께 서면 뒤의 것이 앞의 조회를
     *          덮는다. 호스트는 옛 인스턴스를 내린 뒤 새 것을 세우고(리로드 · 재시작), 이미 하나가 돌 때 `initialize` 는 아무것도 걸지 않고 거절한다(언리얼 `UGameInstance` 하나와 같은 자리).
     *          여러 월드 · 병렬 게임 테스트가 실제로 필요해지면 그때 게임 컨텍스트를 호출 경계로 넘기도록 바꾼다.
     */
    class SW_GF_API GameInstance : public IGame
    {
    public:
        /** @brief 윈도우 · RHI · 팩 설정을 비운 채로 둡니다. 세이브 슬롯은 사용자 데이터 폴더의 로컬 저장소입니다. */
        GameInstance();
        /** @brief 파생 인스턴스가 정리할 수 있게 합니다. */
        virtual ~GameInstance() override;

        /**
         * @brief configurePackConfig 으로 팩 설정을 채우고 gamesettings 를 읽은 뒤 onInitialize 를 부릅니다.
         * @details 팩 루트는 게임 프리셋 `Config/Game/<게임>.json` 의 값이다(그 파일의 유일한 칸). gamesettings 를 못 읽으면 false 다(기본값 폴백 없음).
         *          읽은 `GameSettings` 는 게임 서비스(`game::findSubsystem<GameSettings>()`)로 등록하고, 다국어(`_localizationProject`)와
         *          게임플레이 입력 맵 묶음(`InputMapBundle`)도 여기서 적용한다. 묶음이 깨졌으면 게임을 띄우지 않는다.
         *          커스텀 칸을 읽는 킷 코드는 이 서비스로 읽는다. 서비스로 등록하지 않으면 제품에서 늘 기본값을 쓴다.
         *          사운드 이벤트 라이브러리(`_audioEvents`)도 `onInitialize` 앞에 올리고 `shutdown` 이 내린다. 적었는데 못 읽거나 `onInitialize` 가 false 면 false 다
         *          (필수 데이터 — `loadRequiredData` — 를 못 읽은 게임은 뜨지 않는다).
         *          **실패는 한 길로 되감는다**: 어느 단계에서 멈추든 그때까지 세운 것을 `shutdown` 이 거둔다(게임 범위 · 서브시스템 · 보안 제공자 · 사운드 이벤트 · 외형 · 자동 저장,
         *          `onInitialize` 에 들어갔으면 `onShutdown`). 호스트의 모듈 절차는 실패한 인스턴스에 `shutdown` 을 부르지 않으므로(`ModuleInstanceUtil::createInstance`)
         *          되감기는 인스턴스의 일이다. 이미 다른 인스턴스가 돌면(프로세스당 하나) 아무것도 걸지 않고 false 다.
         */
        [[nodiscard]] bool initialize( GenericWindow* pWindow ) final;
        /**
         * @brief onShutdown 뒤에 `GameSettings` 서비스를 풀고 윈도우 · RHI 포인터를 끊습니다.
         * @details 세운 만큼만 내리고 두 번 불러도 된다 — 실패한 `initialize` 의 되감기와 호스트의 종료가 같은 함수다. 종료 저장은 다 선 인스턴스만 맡긴다.
         */
        void shutdown() final;
        /**
         * @brief 카탈로그 다시 걸기 → 미니게임 → 씬 흐름(`GameSceneFlow::update` — 페이드 · 로딩 화면 · 끝난 씬 로드의 `SceneLoadCompletedEvent`) → 세이브 완료 → 넷 세션 → onUpdate → 개발 부분 → 자동 저장 순으로 한 프레임을 넘깁니다.
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
        /**
         * @brief 미니게임 칸 스택 없이 지금 씬과 게임 상태만 세이브 목적으로 찍습니다 — 미니게임 흐름이 덮을 씬을 세이브에 실을 봉투로 찍는다(칸 스택은 세이브의 머리가 따로 든다).
         * @return 세이브가 막혔거나 찍지 못하면 false 입니다.
         */
        [[nodiscard]] bool captureSceneForSave( vector<uint8>& outBytes );

        /**
         * @brief Shipping/Gameplay: `captureSnapshot` 과 같은 목적으로 찍은 버퍼로부터 씬과 게임 상태를 즉시 복원합니다.
         * @details 검증에서 진 버퍼는 씬을 건드리지 않는다. 교체 중에 졌으면(씬이 일부만 섰다) 세이브 막기를 걸고, 다음 성공한 복원이 푼다(`readStateEnvelope`).
         */
        [[nodiscard]] bool restoreSnapshot( SerializePurpose purpose, const vector<uint8>& inBytes );

        /** @brief 세이브 막기 문입니다 — 씬 전환 · 연출 · 깨진 복원이 범위형 막기를 들고, 들린 동안 세이브 목적 캡처가 거절된다. 게임 범위에도 걸려 있다. */
        SaveLock& getSaveLock() { return _saveLock; }
        /**
         * @brief 지금 도는 인스턴스입니다(없으면 nullptr). 게임 스레드에서만 부른다.
         * @details 기동이 이미 도는 인스턴스가 있는지 보는 자리다. 프로세스당 하나라 뜻이 하나다.
         *          인스턴스는 기동 때 자기 게임 범위에 자신을 건다 — 지금 게임 범위에서 찾는다(정적 칸을 두지 않는다 — 언리얼 `UGameInstance` 도 월드 문맥이 든다).
         */
        static GameInstance* findRunningInstance();

        // --------------------------------------------------------------------------
        // 게임 인스턴스 서브시스템
        // --------------------------------------------------------------------------
        /**
         * @brief 이 인스턴스가 소유한 서브시스템 @p T 입니다(`ScreenTransitionManager` · `LoadingScreenController` · `GameSceneFlow` · `GameLocalStore`). 생성 뒤에는 늘 있다.
         * @details 언리얼 `UGameInstance::GetSubsystem<T>()` 자리다. 게임 범위에 있으므로 키트는 인스턴스 없이 `game::findSubsystem<T>()` 로도 찾는다.
         *          화면 전환은 FadeOut → 실행 → FadeIn 을, 로딩 화면은 씬 로드 동안 gamesettings 의 `_loadingScreen` 문서를, 씬 흐름은 첫 씬 · 타이틀 요청과 로드 알림을 맡는다
         *          (파생 초기화 뒤 `startFirstScene` 이 `GameSceneFlow::requestFirstScene` 으로 첫 씬을 연다).
         *          네트워크 세션(`GameNetSession`)은 엔진 수명이라 여기 없다 — 키트와 게임은 `game::findSubsystem<GameNetSession>()` 의 라우터에 처리기를 단다.
         */
        template <class T>
        T& getSubsystem() const
        {
            return _gameScope.get<T>();
        }

    protected:
        /**
         * @brief 세이브 슬롯 저장소를 넘겨받아 만듭니다 — 이미 연 저장소(메모리 · 임시 폴더)는 기동이 다시 열지 않는다. 테스트가 사용자 데이터 폴더를 쓰지 않게 하는 자리다.
         */
        explicit GameInstance( unique_ptr<GameLocalStore> localStore );

        /** @brief 파생 클래스가 팩 루트 · 팩 설정을 설정합니다. */
        virtual void configurePackConfig( GamePackConfig& outConfig ) { (void)outConfig; }
        /** @brief 팩의 gamesettings 를 `_packConfig` 으로 읽습니다. 실패하면 게임 초기화가 실패합니다(기본값으로 서지 않는다). 팩 없이 만드는 테스트만 XML 문자열(`loadFromXMLText`)로 오버라이드합니다. */
        [[nodiscard]] virtual bool loadGameSettings();
        /**
         * @brief 콘텐츠 구성(켠 모드 등)을 정하는 파생 훅입니다. 네트워크 세션을 열기 전 · 첫 씬과 리소스를 읽기 전에 한 번 부릅니다. 실패하면 게임을 띄우지 않는다.
         * @details 구성을 정했으면 게임 범위에 `IContentConfiguration` 으로 건다 — 기동이 그 해시를 넷 와이어 버전에 섞고(구성이 다른 상대는 핸드셰이크에서 갈린다)
         *          세이브 봉투 머리에 적는다(`GameStateSnapshot`). 걸지 않으면 구성 없음이다. 구성을 바꾸는 입구는 @p token 을 받는다 — 이 훅 밖에서는 부를 수 없다.
         */
        [[nodiscard]] virtual bool onPrepareContent( const ContentPreparationToken& token )
        {
            (void)token;
            return true;
        }
        /**
         * @brief 이 게임이 복제하는 타입(`PROPERTY( Replicated )` 칸의 스냅샷 구조체)을 엔티티 타입 번호로 등록하는 파생 훅입니다. 네트워크 세션을 열기 전에 한 번 부릅니다.
         * @details 등록한 스키마 해시가 와이어 버전에 섞여(`GameNetSession::combineWireContent`) 칸 순서 · 범위가 다른 빌드는 핸드셰이크에서 갈린다. 실패하면 게임을 띄우지 않는다.
         */
        [[nodiscard]] virtual bool onRegisterReplicatedTypes( NetMessageChannel& outChannel )
        {
            (void)outChannel;
            return true;
        }
        /** @brief 팩 설정을 읽은 뒤 부르는 파생 초기화 훅입니다. */
        virtual bool onInitialize() { return true; }
        /**
         * @brief 파생 초기화(`onInitialize`)가 통과한 뒤 첫 씬을 엽니다. 기본은 gamesettings 의 첫 씬입니다(`GameSceneFlow::requestFirstScene` — `-gv_firstScene` > 타이틀 씬 > 시작 맵).
         * @details 언리얼 `UGameInstance::StartGameInstance` 가 설정의 기본 맵을 여는 자리다 — 게임마다 `onInitialize` 끝에 같은 요청을 적지 않는다. 시작 맵은 gamesettings 의 필수 칸이라
         *          없으면 읽기에서 멈춘다(폴백 없음). 요청 실패는 씬 흐름이 알리고, 살아 있는 월드 위에 다시 선 인스턴스(핫 리로드)는 요청하지 않는다. 에디터가 자기 시작 씬을 열면
         *          그 요청이 뒤에 와서 이긴다. 첫 씬 대신 자기 장면을 세우는 게임(벤치)만 오버라이드한다.
         */
        virtual void startFirstScene();
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
         * @details 부르기 전에 봉투의 컴포넌트 상태 저장소(`getStateSnapshot().getComponentStateStore()`)를 비우고, `register*` 로 올린 타입의 상태를 싣고 세운 것을 걷는다.
         *          그 밖의 상태는 여기서 `getStateSnapshot().getComponentStateStore().capture<T>( key, manager )`(열쇠는 저장 틀이 넘긴다 — `StatefulComponentRegistrar` 로 등록하는 길이 먼저다) 로 싣는다.
         *          한 번의 캡처에 두 번 불릴 수 있다(크기 묻기와 채우기).
         */
        virtual void onBeforeStateSerialize() {}
        /**
         * @brief 상태 스냅샷 역직렬화 직후에 부르는 복원 훅입니다.
         * @details 씬 오브젝트가 다시 서고 `register*` 로 올린 타입이 상태를 돌려받은 뒤에 불린다.
         *          그 밖에 실어 둔 상태는 `getStateSnapshot().getComponentStateStore().restore<T>( key, manager )` 로 넘긴다. 이 훅이 끝나면 저장소를 비운다.
         */
        virtual void onAfterStateDeserialize() {}

        /**
         * @brief 상태 스냅샷 봉투입니다 — 파생 생성자가 PROPERTY 밖의 상태를 가진 컴포넌트 타입을 올린다(`registerStatefulComponent` · `registerViewOwner` · `registerGameMode`).
         * @details 훅(`onBeforeStateSerialize` · `onAfterStateDeserialize`)은 그 봉투의 컴포넌트 상태 저장소(`getComponentStateStore`)로 그 밖의 상태를 싣고 넘긴다.
         */
        GameStateSnapshot& getStateSnapshot() { return _stateSnapshot; }
        /**
         * @brief 이 인스턴스의 게임 범위에 객체(카탈로그 · 데이터베이스 — 보통 파생의 멤버)를 겁니다. `game::findSubsystem<T>()` 가 찾는다.
         * @details 범위는 이 인스턴스가 소유하므로 인스턴스가 내려가면 함께 풀린다 — 짝으로 풀 일이 없다. 같은 타입이 이미 걸려 있으면
         *          알리고 걸지 않는다(키트 둘이 같은 카탈로그 타입을 서비스로 쓰는 조립 — 나중 것이 앞 것을 소리 없이 덮지 않는다).
         * @return 걸었으면 true 입니다. 충돌이면 false 이고 부르는 쪽(`onInitialize`)은 그것을 초기화 실패로 돌려준다 — 키트가 다른 객체를 보며 도는 게임을 띄우지 않는다.
         */
        template <class T>
        [[nodiscard]] bool bindScopedService( T* pService )
        {
            if ( _gameScope.bind( pService ) )
                return true;
            reportScopedServiceConflict( getSubsystemTypeHash<T>() );
            return false;
        }
        /** @brief 이 인스턴스의 게임 범위입니다(기동 뒤 `game::findGameScope()` 와 같다). */
        SubsystemScope& getGameScope() { return _gameScope; }

        /**
         * @brief 필수 데이터 하나를 읽습니다. 경로는 데이터 종류 등록 줄(@p kindLabel)과 팩 루트로 만든다(`GamePackDataUtil::tryMakeKindPath`) — 팩은 이 게임 팩이고, gamesettings 의 `<RequiredData kind pack>` 이 적은 종류만 그 팩(공용 `common` 등)이다.
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
        /**
         * @brief 카탈로그(종류 `TCatalog::kDataKind`)를 공유 캐시에서 받아 @p outCatalog 에 쥐고 게임 범위에 겁니다. 실패는 `loadRequiredData` 와 같습니다.
         * @details 사본을 읽지 않는다 — 같은 파일을 쥔 컴포넌트 · 에디터와 한 벌을 나눈다. 받은 때의 판(스냅숏)을 쥔다 — 번호를 지어 두어 일부러 옛 판을 쥐는 종류(떼어 내는 카탈로그)만 쓴다.
         *          다시 읽힌 판을 따라가야 하면 `loadRequiredCatalog`(`SharedAssetRef`)를 쓴다.
         */
        template <class TCatalog>
        [[nodiscard]] bool loadRequiredCatalogSnapshot( shared_ptr<const TCatalog>& outCatalog )
        {
            if ( loadRequiredData( TCatalog::kDataKind, [&outCatalog]( const string& path )
            {
                outCatalog = SharedAssetCache<TCatalog>::acquire( path ).snapshot();
                return outCatalog != nullptr;
            } ) == false )
                return false;
            return bindScopedService( outCatalog.get() ); // const 칸이다 — 찾는 쪽은 `game::findSubsystem<const TCatalog>()` 로 찾는다
        }
        /**
         * @brief 멤버로 쥐는 카탈로그(@p outCatalog)를 받아 게임 범위에 겁니다. 다시 읽히면 `update` 가 범위의 칸도 새 판으로 바꿔 건다 — 찾는 쪽이 옛 판을 계속 보지 않게.
         * @details @p outCatalog 는 이 인스턴스의 멤버라 인스턴스와 수명이 같아야 한다(다시 걸기가 그 주소를 쥔다).
         */
        template <class TCatalog>
        [[nodiscard]] bool loadRequiredCatalog( SharedAssetRef<TCatalog>& outCatalog )
        {
            if ( loadRequiredData( TCatalog::kDataKind, [&outCatalog]( const string& path )
            {
                outCatalog = SharedAssetCache<TCatalog>::acquire( path );
                return outCatalog != nullptr;
            } ) == false )
                return false;
            if ( bindScopedService( outCatalog.get() ) == false ) // const 칸이다 — 찾는 쪽은 `game::findSubsystem<const TCatalog>()` 로 찾는다
                return false;
            followScopedService( outCatalog );
            return true;
        }
        /** @brief 게임 범위에 건 @p ref 의 칸을 다시 읽힌 판으로 따라 바꾸게 합니다(`update` 앞머리 — 틱 밖). @p ref 는 인스턴스보다 오래 살아야 한다. */
        template <class TCatalog>
        void followScopedService( const SharedAssetRef<TCatalog>& ref )
        {
            const SharedAssetRef<TCatalog>* const pRef = &ref;
            _listBoundCatalog.push_back( ref.snapshot() );
            _listFollowedService.push_back( SW_DELEGATE_LAMBDA( Delegate<void()>, [this, pRef]()
            {
                const TCatalog* const pLatest = pRef->get();
                if ( pLatest == nullptr || _gameScope.find<const TCatalog>() == pLatest )
                    return;
                _listBoundCatalog.push_back( pRef->snapshot() ); // 앞 판도 쥔 채로 둔다 — 칸에서 찾은 날 포인터를 든 쪽(퀘스트 · 가방 · 평판)이 끊기지 않게
                _gameScope.unbind<const TCatalog>();
                (void)_gameScope.bind( pLatest ); // 방금 푼 칸이라 겹치지 않는다
            } ) );
        }

        /**
         * @brief `captureSnapshot` 이 같은 목적으로 쓴 봉투를 읽고(`GameStateSnapshot::readEnvelope`) 읽기 훅을 부릅니다.
         * @details 목적과 상관없이(세이브 · 핫 리로드) 교체 중에 졌으면 세이브 막기(`GameSaveSlots::notifySceneBroken`)를 걸고, 성공하면 푼다. 검증에서 진 것은 막기를 바꾸지 않는다(씬이 그대로다).
         *          세이브 목적에서 교체가 졌으면 그 씬을 디스크에서 다시 열고, 다시 서면 막기를 푼다.
         */
        /** @brief 봉투 하나를 찍습니다(`captureSnapshot` · `captureSceneForSave` 의 몸통). 세이브 목적이면 막기 문을 먼저 본다. */
        [[nodiscard]] bool                                   captureEnvelope( SerializePurpose purpose, const vector<uint8>& minigameBytes, vector<uint8>& outBytes );
        [[nodiscard]] expected<void, SnapshotRestoreFailure> readStateEnvelope( SerializePurpose purpose, const void* pInBuffer, uint32 size );

    private:
        /**
         * @brief `initialize` 의 단계를 차례로 돌립니다(게임 범위 걸기 → 팩 설정 적용 → 인스턴스 서브시스템 → 넷 세션 → 사운드 · 필수 데이터 → 자동 저장 → `onInitialize`).
         * @details 멈춘 단계에서 바로 false 를 돌려주고, 거두기는 부르는 쪽(`initialize`)이 `shutdown` 한 번으로 한다 — 단계마다 되감기를 따로 적지 않는다.
         */
        [[nodiscard]] bool startInstance();
        /**
         * @brief 기동(`initialize`)이 쓰는 엔진 서비스가 있는지 아무것도 걸기 전에 확인합니다(언리얼 `InitializeDependency`). 들고 있지 않고 쓰는 자리에서 게임 창구로 찾는다.
         * @details 네트워크 세션은 모든 호스트의 필수 서비스다. UI · 사용자 설정은 Client 호스트 서비스라 창이 있는 호스트(클라이언트)에서는 필수이고,
         *          창이 없는 호스트(전용 서버 · 테스트)에서만 없다. 필수 서비스가 없으면 오류를 남기고 false 입니다(인스턴스가 서지 않는다).
         */
        [[nodiscard]] static bool validateEngineServices( const GenericWindow* pWindow );
        /**
         * @brief `GameSettings` 의 다국어 · 입력 맵 칸을 적용합니다. 파일을 못 읽은 것은 알리고 넘어갑니다(게임은 뜬다).
         * @return 입력 맵을 적었는데 입력 서비스(`InputManager`)가 없으면 오류를 남기고 false 입니다 — 호스트의 결함이라 조용히 건너뛰지 않는다.
         */
        [[nodiscard]] bool applyPackConfig();
        /** @brief gamesettings 의 사운드 이벤트 라이브러리(`_audioEvents`)를 올립니다. 적었는데 못 읽으면 오류를 남기고 false 입니다(적지 않았으면 true). */
        [[nodiscard]] bool loadAudioEvents();
        /**
         * @brief gamesettings 의 자동 저장 정책(`_autosaveSettings`)이 있으면 `AutosaveManager` 를 세워 게임 범위에 겁니다. 적었는데 못 읽으면 오류를 남기고 false 입니다(적지 않았으면 true).
         * @details 저장 바이트는 세이브 슬롯과 같은 봉투다(`captureSnapshot` · `restoreSnapshot` 의 `SerializePurpose::SaveGame`). `update` 가 돌리고 `shutdown` 앞머리가 종료 저장을 맡긴다.
         */
        [[nodiscard]] bool startAutosave();
        /**
         * @brief gamesettings `gameFeatures` 의 게임 기능을 게임 범위 하나(`GameFeatureScope`, 카탈로그는 팩 데이터 규칙)에 켭니다 — `onInitialize` 뒤 · 첫 씬 전의 필수 단계다.
         * @details 언리얼 Game Features 의 늘 켜진 기능 자리다. 미니게임(`MinigameFlow`)보다 먼저 켜 미니게임 범위의 해제 검사가 그 몫을 남은 것으로 세지 않게 하고,
         *          서브시스템 종료(미니게임 끄기) 뒤에 끈다. 비었으면 빈 범위다(기능 없는 게임도 같은 길).
         * @return 겹친 이름 · 등록되지 않은 기능 · 켜기가 진 기능이 있으면 알리고 false 입니다(게임 초기화 실패 — 필수 데이터 없이 띄우지 않는다).
         */
        [[nodiscard]] bool activateGameFeatures();
        /**
         * @brief gamesettings 가 `<RequiredData>` 로 적은 아이템(`items`) · 외형(`appearance`) 데이터를 읽어 게임 범위에 겁니다(외형은 아이템 뒤 — 아이템을 참조한다). 적지 않은 종류는 읽지 않는다.
         * @details 캐릭터 외형을 쓰는 게임마다 같은 로드 · 서비스 걸기 · 핫 리로드를 손으로 짜지 않게 데이터가 정한다(언리얼 GameInstance 서브시스템처럼 바탕이 세운다). 못 읽으면 false(게임을 띄우지 않는다).
         */
        [[nodiscard]] bool loadDeclaredCharacterData();
        /** @brief `loadRequiredData` 의 경로를 만듭니다. 못 만들면 이유를 오류로 남기고 false 입니다. */
        [[nodiscard]] bool makeRequiredDataPath( string_view kindLabel, string& outPath ) const;
        /** @brief 필수 데이터 @p path 를 읽지 못했음을 파일과 이유로 알립니다. */
        void reportRequiredDataFailure( string_view kindLabel, const AssetID& assetID ) const;
        /** @brief 이 인스턴스의 게임 범위를 지금 범위 사슬에서 뗍니다(지금 범위면 앞 범위로 되돌리고, 테스트 범위(`ScopedGameScope`)가 이 범위를 상위로 이었으면 그 위를 잇게 한다). */
        void detachGameScope();
        /** @brief 게임 범위에 같은 타입이 이미 걸려 있음을 알립니다(`bindScopedService`). */
        void reportScopedServiceConflict( uint64 typeHash ) const;

    protected:
        GamePackConfig _packConfig; ///< 팩 루트와 gamesettings
        GenericWindow* _pWindow;    ///< 호스트 윈도우 (App 이 소유)

    private:
        GameStateSnapshot                 _stateSnapshot;       ///< 상태 스냅샷 봉투와 그 봉투에 오른 컴포넌트 타입
        SaveLock                          _saveLock;            ///< 세이브 막기 문 — 게임 범위(씬 흐름이 빌린다)보다 먼저 서고 늦게 진다
        SubsystemScope                    _gameScope;           ///< 게임 범위 — 인스턴스 서브시스템(화면 전환 · 로딩 화면)을 소유하고, 기동부터 종료까지 `game::findSubsystem<T>()` 의 지금 범위다
        SharedAssetRef<ItemCatalog>       _pItemCatalog;        ///< `<RequiredData kind="items">` 를 적은 게임의 아이템 카탈로그(공유 캐시의 한 벌, 게임 범위 서비스)
        unique_ptr<AppearanceDatabase>    _pAppearanceDatabase; ///< `<RequiredData kind="appearance">` 를 적은 게임의 외형 데이터(게임 범위 서비스)
        unique_ptr<AppearanceDataWatcher> _pAppearanceWatcher;  ///< 외형 데이터 핫 리로드(`update` 가 돌린다)
        unique_ptr<AutosaveManager>       _pAutosave;           ///< `<autosaveSettings>` 를 적은 게임의 자동 저장(게임 범위 서비스)
        unique_ptr<GameFeatureScope>      _pGameFeatureScope;   ///< gamesettings `gameFeatures` 의 게임 범위 기능(`activateGameFeatures` 가 켜고 서브시스템 종료 뒤에 끈다)
        unique_ptr<IGameDevPart>          _pDevPart;            ///< 게임 `Dev/` 의 개발 전용 부분(`IGameDevPartFactory` — 배포 구성에는 공장이 없어 비었다). `onInitialize` 뒤에 서고 `onShutdown` 앞에 진다
        string                            _loadedAudioEvents;   ///< `loadAudioEvents` 가 올린 사운드 이벤트 파일 — `shutdown` 이 내린다(비면 올린 것 없음)
        vector<shared_ptr<const void>>    _listBoundCatalog;    ///< 범위의 칸에 건 카탈로그의 판들(`followScopedService` — 인스턴스와 함께 놓는다. 다시 읽기는 개발 구성에만 있다)
        vector<Delegate<void()>>          _listFollowedService; ///< 다시 읽힌 카탈로그를 범위의 칸에 다시 거는 일(`followScopedService`)
        uint8                             _bStarted        : 1; ///< `initialize` 가 끝까지 섰다 — 종료 저장 · 세션 명령은 이때만
        uint8                             _bDerivedEntered : 1; ///< `onInitialize` 에 들어갔다 — `onShutdown` 은 이때만(파생은 자기 부분 실패를 스스로 거둔다)
        uint8                             _bInputMapLoaded : 1; ///< 엔진의 통합 입력 맵에 이 인스턴스의 묶음을 실었다 — 종료가 비운다(다음 게임 · 하네스가 앞 게임의 바인딩을 들고 돌지 않는다)
        uint8                             _bSecuritySet    : 1; ///< 넷 세션에 이 인스턴스의 보안 제공자를 넣었다 — 거둘 때 이 인스턴스가 넣은 것만 뺀다
    };
} // namespace sw
