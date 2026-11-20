/**
 * @file GameFeatureScope.h
 * @brief 켜진 게임 기능(`SW_GAME_FEATURE`) 묶음 하나가 게임 범위에 건 것(서비스 · 카탈로그 · 전역 변수 · 되돌릴 일)의 범위입니다 — 끄면(소멸하면) 건 역순으로 모두 거둡니다.
 * @details 범위는 둘 쓰인다. 게임 인스턴스가 gamesettings `gameFeatures` 로 `onInitialize` 뒤 · 첫 씬 전에 켜는 게임 범위 하나(인스턴스 내내), 미니게임(`MinigameFlow`)이
 *          정의의 `features` 로 들어갈 때 켜고 나올 때 끄는 것. 키트 23 개 이상이 한 게임 인스턴스에 등록되면 서비스 · 전역 값이 미니게임 사이로 새므로
 *          기능의 켜기 함수는 게임 범위가 아니라 이 범위만 받는다 — 거는 길이 모두 되돌릴 일을 함께 적어, 끄기를 빠뜨린 등록을 쓸 수 없다.
 *          언리얼 Game Features(`UGameFeatureAction::OnGameFeatureActivating` · `OnGameFeatureDeactivating`)와 같은 자리다(액션마다 끄기를 손으로 적지 않는 점만 다르다).
 *          서비스는 게임 범위(`game::findSubsystem<T>()`)에 그대로 걸린다 — 찾는 쪽은 기능 묶음을 몰라도 된다.
 *          카탈로그 선언: 미니게임 범위는 정의의 `catalogs` 에 적힌 종류만 읽는다(`CatalogRule::Declared`). 게임 범위는 gamesettings 데이터 항목에 경로가 있는 종류면
 *          선언된 것으로 본다(`CatalogRule::PackData` — 게임 인스턴스의 필수 데이터와 같은 규칙, 따로 적을 목록이 없다).
 *          해제 검사: 범위 밖으로 건 것(기능이 게임 범위 · 입력 맵 · 전역 변수를 직접 만진 것)은 되돌릴 일이 없으므로, 켜기 전 · 켠 뒤 · 끈 뒤의 사본을 견줘
 *          켜기가 바꾸고 끈 뒤에도 남은 칸 · 입력 레이어 · 전역 값을 오류로 알린다(시나리오 · 테스트의 로그 판정이 잡는다). 플레이 중 게임 · 시나리오가 바꾼 것은 기능의
 *          등록이 아니므로 보지 않는다. 언리얼은 기능을 내린 뒤 남은 것을 보지 않는다 — 형식이 막지 못하는 틈(직접 등록)을 여기서 잡는다.
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

#include "GameFramework/GameFrameworkExports.h"

#include <type_traits>

namespace sw
{
    class GlobalVariableManager;
    class InputMap;
    struct GamePackConfig;

    /**
     * @class GameFeatureScope
     * @brief 켜진 게임 기능 묶음 하나의 범위입니다. 만들면 빈 채로 켜진 상태이고, `activateFeatures` 가 기능을 겁니다. `deactivate`(또는 소멸)가 건 것을 역순으로 거둡니다.
     *        복사 · 이동은 막혀 있습니다(되돌릴 일이 이 주소를 쥔다).
     */
    class SW_GF_API GameFeatureScope
    {
    public:
        /** @brief 기능이 읽는 카탈로그(`loadCatalog`)를 선언된 것으로 보는 규칙입니다. */
        enum class CatalogRule : uint8
        {
            Declared, ///< 만들 때 넘긴 목록에 있는 종류만(미니게임 정의의 `catalogs`) — 켜기 끝에 적은 것을 모두 읽었는지도 본다(`findUnloadedCatalog`)
            PackData, ///< 팩 데이터 경로가 있는 종류면 모두(게임 범위 — gamesettings 데이터 항목, `GamePackConfig::makeRequiredDataPath`)
        };

        /**
         * @param gameScope 서비스를 걸 게임 범위(이 범위보다 오래 산다)
         * @param variables 전역 변수 덮어쓰기의 매니저(이 범위보다 오래 산다)
         * @param packConfig 카탈로그 경로를 만드는 팩 설정(이 범위보다 오래 산다)
         * @param pInputMap 입력 레이어를 거는 입력 맵(이 범위보다 오래 산다). 입력 서비스가 없는 호스트(전용 서버 · 테스트)는 nullptr 이다
         * @param name 로그에 적는 범위 이름(미니게임 이름 · `gameFeatures`)
         * @param catalogRule 카탈로그 선언 규칙
         * @param listDeclaredCatalog `CatalogRule::Declared` 의 목록(사본을 든다). `PackData` 면 비어 있어야 한다
         * @details 만들 때 해제 검사의 "켜기 전" 사본을 찍는다 — 기능 켜기보다 먼저 만든다.
         */
        GameFeatureScope( SubsystemScope& gameScope, GlobalVariableManager& variables, const GamePackConfig& packConfig, InputMap* pInputMap, const hashed_string& name, CatalogRule catalogRule,
                          vector<hashed_string> listDeclaredCatalog );
        /** @brief 아직 켜져 있으면 `deactivate` 합니다. */
        ~GameFeatureScope();

        GameFeatureScope( const GameFeatureScope& )            = delete;
        GameFeatureScope& operator=( const GameFeatureScope& ) = delete;

        /** @brief 범위 이름입니다(로그용). */
        const hashed_string& getName() const { return _name; }
        /** @brief 입력 레이어를 거는 입력 맵입니다(없으면 nullptr). */
        InputMap* findInputMap() const { return _pInputMap; }

        /**
         * @brief 등록된 기능(`GameFeatureRegistry`)을 @p listFeature 의 순서로 켭니다.
         * @return 겹친 이름 · 등록되지 않은 이름 · 켜기 함수가 진 기능이 있으면 알리고 false 입니다 — 그때까지 건 것은 범위에 남아 끌 때 거둔다(부르는 쪽이 범위를 버린다).
         */
        [[nodiscard]] bool activateFeatures( const vector<hashed_string>& listFeature );

        /**
         * @brief @p pService 를 게임 범위에 타입 @p T 로 겁니다(`game::findSubsystem<T>()` 가 찾는다). 끌 때 풉니다. @p pService 는 끌 때까지 살아야 한다.
         * @return 같은 타입(const 든 아니든)이 게임 범위나 그 위(엔진)에 이미 있으면 알리고 false 입니다 — 기능 둘이 같은 서비스를 걸어 뒤의 것이 앞의 것을 소리 없이 가리지 않게.
         */
        template <class T>
        [[nodiscard]] bool bindService( T* pService )
        {
            using TPlain = std::remove_const_t<T>;
            if ( pService == nullptr || locateSubsystem<const TPlain>( &_gameScope ) != nullptr || _gameScope.bind( pService ) == false )
            {
                reportServiceConflict( getSubsystemTypeHash<T>() );
                return false;
            }
            addCleanup( Delegate<void()>::create( [this]()
            { _gameScope.unbind<T>(); } ) );
            return true;
        }

        /**
         * @brief 카탈로그 @p TCatalog(종류 `TCatalog::kDataKind`)를 공유 캐시에서 받아 게임 범위에 const 로 겁니다(`game::findSubsystem<const TCatalog>()`). 끌 때 풀고 놓습니다.
         * @details 종류는 선언되어 있어야 한다(`CatalogRule`). 경로는 게임 인스턴스의 필수 데이터와 같은 규칙이다(`GamePackConfig::makeRequiredDataPath`).
         *          공유 캐시의 `SharedAssetRef` 를 쥐고 다시 읽히면 `rebindReloadedCatalogs` 가 칸을 새 판으로 바꿔 건다 — 게임 인스턴스의 필수 카탈로그
         *          (`GameInstance::loadRequiredCatalog` · `followScopedService`)와 같은 길이다. 스냅숏 사본은 두지 않는다(찾는 쪽이 옛 판을 계속 보지 않게).
         * @return 선언되지 않은 종류이거나 경로를 못 만들거나 못 읽거나 이미 걸려 있으면 알리고 false 입니다.
         */
        template <class TCatalog>
        [[nodiscard]] bool loadCatalog()
        {
            string path;
            if ( resolveCatalogPath( TCatalog::kDataKind, path ) == false )
                return false;
            // 참조는 범위가 쥔 블록에 둔다 — 따라가기 일이 그 주소를 쥐고, 블록은 `deactivate` 가 따라가기를 지운 뒤에 놓는다.
            shared_ptr<SharedAssetRef<TCatalog>> pRef     = make_shared<SharedAssetRef<TCatalog>>( SharedAssetCache<TCatalog>::acquire( path ) );
            const TCatalog* const                pCatalog = pRef->get();
            if ( pCatalog == nullptr )
            {
                reportCatalogFailure( TCatalog::kDataKind, path );
                return false;
            }
            if ( bindService( pCatalog ) == false ) // const 칸이다 — 찾는 쪽은 `game::findSubsystem<const TCatalog>()`
                return false;
            const SharedAssetRef<TCatalog>* const pFollowed = pRef.get();
            _listFollow.push_back( Delegate<void()>::create( [this, pFollowed]()
            {
                const TCatalog* const pLatest = pFollowed->get();
                if ( pLatest == nullptr || _gameScope.find<const TCatalog>() == pLatest )
                    return;
                _gameScope.unbind<const TCatalog>();
                (void)_gameScope.bind( pLatest ); // 방금 푼 칸이라 겹치지 않는다 — 끌 때의 풀기(위 `bindService` 의 되돌릴 일)는 타입으로 푼다
            } ) );
            _listHeldObject.push_back( std::move( pRef ) ); // 풀기(되돌릴 일)는 위에 이미 적혔다 — 놓기는 `deactivate` 끝이다
            _listLoadedCatalog.push_back( hashed_string( TCatalog::kDataKind ) );
            return true;
        }

        /** @brief 다시 읽힌 카탈로그(`loadCatalog`)의 칸을 새 판으로 바꿔 겁니다 — 게임 프레임 앞(틱 밖)에서 범위의 주인이 부른다. 끈 범위는 할 일이 없다. */
        void rebindReloadedCatalogs();

        /**
         * @brief 서비스 @p T 를 기본 생성해 이 범위가 쥐고 게임 범위에 겁니다(`game::findSubsystem<T>()`). 끌 때 풀고 지운다.
         * @details 게임 코드가 멤버로 쥐던 레지스트리(어빌리티 클래스 등)를 기능 수명으로 옮기는 자리다 — 언리얼 Game Feature 액션 `AddComponents` 가 켤 때 만들고 끌 때 지우는 것과 같다.
         * @return 같은 타입이 이미 걸려 있으면 알리고 false 입니다(`bindService`).
         */
        template <class T>
        [[nodiscard]] bool emplaceService()
        {
            shared_ptr<T> pService = make_shared<T>();
            if ( bindService( pService.get() ) == false )
                return false;
            _listHeldObject.push_back( std::move( pService ) ); // 풀기는 위에 적혔다 — 지우기는 `deactivate` 끝이다
            return true;
        }

        /**
         * @brief 전역 변수 @p name 을 @p value 로 바꾸고, 끌 때 지금 값으로 되돌립니다.
         * @return 변수가 없거나 값을 받지 않으면 알리고 false 입니다(바꾸지 않는다).
         */
        [[nodiscard]] bool overrideGlobalVariable( string_view name, string_view value );

        /** @brief 끌 때 부를 일을 더합니다(입력 레이어 빼기 · 화면 닫기처럼 거는 길이 위에 없는 것). 건 역순으로 부른다. */
        void addCleanup( Delegate<void()> cleanup );

        /** @brief 기능들이 읽은 카탈로그 종류입니다(`loadCatalog` 의 차례). */
        const vector<hashed_string>& getLoadedCatalogs() const { return _listLoadedCatalog; }
        /** @brief `CatalogRule::Declared` 에서 적었지만 아무 기능도 읽지 않은 첫 종류입니다. 없거나 `PackData` 면 nullptr 입니다 — 데이터와 기능이 어긋났다. */
        const hashed_string* findUnloadedCatalog() const;

        /** @brief 켜기가 끝났습니다 — 해제 검사의 "켠 뒤" 사본을 찍습니다. 부르지 않고 끄면(켜기 도중 실패) 끌 때 찍는다. */
        void finishActivation();

        /**
         * @brief 건 것을 역순으로 거두고 해제 검사를 합니다. 두 번 불러도 된다(검사는 한 번).
         * @return 끈 뒤에 남은 것(칸 · 입력 레이어 · 전역 값)의 수입니다. 0 이 아니면 하나씩 오류로 알렸다.
         */
        uint32 deactivate();

    private:
        /** @brief `loadCatalog` 의 경로입니다 — 선언되지 않았거나 경로를 못 만들면 알리고 false 입니다. */
        [[nodiscard]] bool resolveCatalogPath( const utf8* pKind, string& outPath ) const;
        /** @brief 같은 타입이 이미 걸려 있음을 알립니다. */
        void reportServiceConflict( uint64 typeHash ) const;
        /** @brief 카탈로그를 읽지 못했음을 알립니다. */
        void reportCatalogFailure( const utf8* pKind, const string& path ) const;

        /** @brief 전역 변수 하나의 이름과 값(글)입니다. */
        struct GlobalValue
        {
            string _name;
            string _value;
        };
        /** @brief 범위 밖 상태의 한 시점 사본입니다 — 게임 범위의 칸 · 입력 레이어 스택 · 켜진 입력 레이어 · 전역 변수 값(이름 순). */
        struct StateSnapshot
        {
            vector<uint64>        _listServiceType{};
            vector<hashed_string> _listInputLayer{};
            vector<hashed_string> _listEnabledInputLayer{}; ///< 스택 밖에서 켠 레이어도 빈 스택에서는 살아난다 — 켬도 따로 견준다
            vector<GlobalValue>   _listGlobalValue{};
        };
        /** @brief 지금 상태를 찍습니다. */
        StateSnapshot captureState() const;
        /** @brief 켜기가 바꾸고(`_before` ≠ `_activated`) 끈 뒤(@p after)에도 남은 것을 알리고 그 수를 돌려줍니다. */
        uint32 reportLeaks( const StateSnapshot& after ) const;

    private:
        SubsystemScope&                _gameScope;           ///< 서비스를 거는 게임 범위
        GlobalVariableManager&         _variables;           ///< 전역 변수 덮어쓰기의 매니저
        const GamePackConfig&          _packConfig;          ///< 카탈로그 경로의 팩 설정
        InputMap*                      _pInputMap;           ///< 입력 레이어를 거는 입력 맵(없으면 nullptr)
        hashed_string                  _name;                ///< 로그에 적는 범위 이름
        vector<hashed_string>          _listDeclaredCatalog; ///< `CatalogRule::Declared` 의 목록
        vector<Delegate<void()>>       _listCleanup;         ///< 끌 때 역순으로 부를 일
        vector<Delegate<void()>>       _listFollow;          ///< 다시 읽힌 카탈로그를 칸에 다시 거는 일(`loadCatalog`)
        vector<shared_ptr<const void>> _listHeldObject;      ///< 걸어 둔 카탈로그 · 쥔 서비스(끌 때 놓는다)
        vector<hashed_string>          _listLoadedCatalog;   ///< 읽은 카탈로그 종류
        StateSnapshot                  _before;              ///< 켜기 전 사본(만들 때)
        StateSnapshot                  _activated;           ///< 켠 뒤 사본(`finishActivation`, 아니면 끌 때)
        CatalogRule                    _catalogRule;         ///< 카탈로그 선언 규칙
        uint8                          _bActivated   : 1;    ///< `_activated` 를 찍었으면 1
        uint8                          _bDeactivated : 1;    ///< 끄고 검사했으면 1
    };
} // namespace sw
