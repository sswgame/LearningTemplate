/**
 * @file ExperienceScope.h
 * @brief 켜진 Experience 하나가 게임 범위에 건 것(서비스 · 카탈로그 · 전역 변수 · 되돌릴 일)의 묶음입니다 — 끄면(소멸하면) 건 역순으로 모두 거둡니다.
 * @details 키트 23 개 이상이 한 게임 인스턴스에 등록되면 서비스 · 전역 값이 Experience 사이로 샌다(지금은 시나리오 체인이 단계마다 인스턴스를 다시 세워 감춰짐).
 *          그래서 키트의 켜기 함수(`SW_EXPERIENCE_KIT`)는 게임 범위가 아니라 이 범위만 받는다 — 거는 길이 모두 되돌릴 일을 함께 적어, 끄기를 빠뜨린 등록을 쓸 수 없다.
 *          언리얼 Lyra `ULyraExperienceManagerComponent` 가 Experience 를 내릴 때 Game Feature 액션을 역순으로 끄는 것과 같은 자리다(액션마다 끄기를 손으로 적지 않는 점만 다르다).
 *          서비스는 게임 범위(`game::findSubsystem<T>()`)에 그대로 걸린다 — 찾는 쪽은 Experience 를 몰라도 된다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceDefinition.h"

#include <type_traits>

namespace sw
{
    class GlobalVariableManager;
    struct GamePackConfig;

    /**
     * @class ExperienceScope
     * @brief 켜진 Experience 하나의 범위입니다. 만들면 켜진 상태이고, `deactivate`(또는 소멸)가 건 것을 역순으로 거둡니다. 복사 · 이동은 막혀 있습니다(되돌릴 일이 이 주소를 쥔다).
     */
    class SW_GF_API ExperienceScope
    {
    public:
        /**
         * @param gameScope 서비스를 걸 게임 범위(이 범위보다 오래 산다)
         * @param variables 전역 변수 덮어쓰기의 매니저(이 범위보다 오래 산다)
         * @param packConfig 카탈로그 경로를 만드는 팩 설정(이 범위보다 오래 산다)
         * @param definition 켠 Experience(사본을 든다)
         */
        ExperienceScope( SubsystemScope& gameScope, GlobalVariableManager& variables, const GamePackConfig& packConfig, const ExperienceDefinition& definition );
        /** @brief 아직 켜져 있으면 `deactivate` 합니다. */
        ~ExperienceScope();

        ExperienceScope( const ExperienceScope& )            = delete;
        ExperienceScope& operator=( const ExperienceScope& ) = delete;

        /** @brief 켠 Experience 입니다. */
        const ExperienceDefinition& getDefinition() const { return _definition; }

        /**
         * @brief @p pService 를 게임 범위에 타입 @p T 로 겁니다(`game::findSubsystem<T>()` 가 찾는다). 끌 때 풉니다. @p pService 는 끌 때까지 살아야 한다.
         * @return 같은 타입(const 든 아니든)이 게임 범위나 그 위(엔진)에 이미 있으면 알리고 false 입니다 — 키트 둘이 같은 서비스를 걸어 뒤의 것이 앞의 것을 소리 없이 가리지 않게.
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
         * @details 종류는 Experience 의 `catalogs` 에 적혀 있어야 한다 — 데이터가 무엇을 읽는지 정의 한 곳에 보이게. 경로는 게임 인스턴스의 필수 데이터와 같은 규칙이다
         *          (`GamePackConfig::makeRequiredDataPath`). shortcut: 받은 때의 판(스냅숏)을 쥔다 — 핫 리로드를 따라가야 하는 카탈로그가 생기면 `SharedAssetRef` 를 쥐고 다시 건다.
         * @return 적지 않은 종류이거나 경로를 못 만들거나 못 읽거나 이미 걸려 있으면 알리고 false 입니다.
         */
        template <class TCatalog>
        [[nodiscard]] bool loadCatalog()
        {
            string path;
            if ( resolveCatalogPath( TCatalog::kDataKind, path ) == false )
                return false;
            shared_ptr<const TCatalog> pCatalog = SharedAssetCache<TCatalog>::acquire( path ).snapshot();
            if ( pCatalog == nullptr )
            {
                reportCatalogFailure( TCatalog::kDataKind, path );
                return false;
            }
            if ( bindService( pCatalog.get() ) == false )
                return false;
            _listHeldCatalog.push_back( std::move( pCatalog ) ); // 풀기(되돌릴 일)는 위에 이미 적혔다 — 놓기는 `deactivate` 끝이다
            _listLoadedCatalog.push_back( hashed_string( TCatalog::kDataKind ) );
            return true;
        }

        /**
         * @brief 전역 변수 @p name 을 @p value 로 바꾸고, 끌 때 지금 값으로 되돌립니다.
         * @return 변수가 없거나 값을 받지 않으면 알리고 false 입니다(바꾸지 않는다).
         */
        [[nodiscard]] bool overrideGlobalVariable( string_view name, string_view value );

        /** @brief 끌 때 부를 일을 더합니다(입력 레이어 빼기 · 화면 닫기처럼 거는 길이 위에 없는 것). 건 역순으로 부른다. */
        void addCleanup( Delegate<void()> cleanup );

        /** @brief 키트들이 읽은 카탈로그 종류입니다(`loadCatalog` 의 차례). 켜기 끝에 `catalogs` 와 맞춰 본다. */
        const vector<hashed_string>& getLoadedCatalogs() const { return _listLoadedCatalog; }

        /** @brief 건 것을 역순으로 거둡니다. 두 번 불러도 된다. */
        void deactivate();

    private:
        /** @brief `loadCatalog` 의 경로입니다 — `catalogs` 에 없거나 경로를 못 만들면 알리고 false 입니다. */
        [[nodiscard]] bool resolveCatalogPath( const utf8* pKind, string& outPath ) const;
        /** @brief 같은 타입이 이미 걸려 있음을 알립니다. */
        void reportServiceConflict( uint64 typeHash ) const;
        /** @brief 카탈로그를 읽지 못했음을 알립니다. */
        void reportCatalogFailure( const utf8* pKind, const string& path ) const;

    private:
        SubsystemScope&                _gameScope;         ///< 서비스를 거는 게임 범위
        GlobalVariableManager&         _variables;         ///< 전역 변수 덮어쓰기의 매니저
        const GamePackConfig&          _packConfig;        ///< 카탈로그 경로의 팩 설정
        ExperienceDefinition           _definition;        ///< 켠 Experience
        vector<Delegate<void()>>       _listCleanup;       ///< 끌 때 역순으로 부를 일
        vector<shared_ptr<const void>> _listHeldCatalog;   ///< 걸어 둔 카탈로그(끌 때 놓는다)
        vector<hashed_string>          _listLoadedCatalog; ///< 읽은 카탈로그 종류
    };
} // namespace sw
