#include "pch.h"

#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

#include "Engine/Foundation/EngineScope.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    namespace
    {
        /** @brief App 이 넘긴 호스트 서브시스템 범위와 지금 게임 범위입니다. GameFramework.dll 의 정적이라 게임 · 키트 이미지보다 오래 산다. */
        struct GameSubsystemState
        {
            const ISubsystemLocator* _pHostSubsystems{ nullptr };
            SubsystemScope*          _pGameScope{ nullptr };
        };

        GameSubsystemState s_gameSubsystemState{};

        /**
         * @class GameVisibleEngineLocator
         * @brief 호스트 범위를 받지 못한 실행의 게임 창구입니다 — 엔진 범위를 위로 올라가며 찾되 `HostOnly` 로 등록한 칸은 내주지 않는다.
         * @details 묶인 게임 모듈의 범위는 `GameVisible` 칸만 건다(`engine::bindModuleSubsystems`). 폴백이 엔진 범위를 그대로 보이면 묶인 실행에서 nullptr 인 서비스를
         *          테스트 · 체인 단계 사이(앞 단계가 묶기를 푼 뒤)에서만 찾아 결함이 가려진다(작업 풀 `TaskManager` 가 그랬다). 노출 규칙을 두 길에서 같게 둔다.
         */
        class GameVisibleEngineLocator final : public ISubsystemLocator
        {
        public:
            void* findInScope( uint64 typeHash ) const override
            {
                if ( engine::isHostOnlySubsystem( typeHash ) )
                    return nullptr;
                for ( const ISubsystemLocator* pScope = engine::getEngineSubsystems(); pScope != nullptr; pScope = pScope->getParentScope() )
                {
                    void* const pFound = pScope->findInScope( typeHash );
                    if ( pFound != nullptr )
                        return pFound;
                }
                return nullptr;
            }
            const ISubsystemLocator* getParentScope() const override { return nullptr; } // 상위 범위는 `findInScope` 가 이미 걸었다
        };

        const GameVisibleEngineLocator s_gameVisibleEngineLocator{};
    } // namespace

    namespace game
    {
        SW_GAMESERVICE_API void bindGameSubsystems( const ISubsystemLocator& hostServices )
        {
            // 카탈로그의 공유 캐시는 그 카탈로그를 가진 모듈(GameFramework · 키트 · 게임)의 모듈 서브시스템이 서며 에셋 캐시 레지스트리에 스스로 오른다(`SW_MODULE_SHARED_ASSET_CACHE`).
            s_gameSubsystemState._pHostSubsystems = &hostServices;
        }

        SW_GAMESERVICE_API void unbindGameSubsystems()
        {
            s_gameSubsystemState._pHostSubsystems = nullptr;
        }

        SW_GAMESERVICE_API SubsystemScope* findGameScope()
        {
            return s_gameSubsystemState._pGameScope;
        }

        SW_GAMESERVICE_API void setGameScope( SubsystemScope* pScope )
        {
            s_gameSubsystemState._pGameScope = pScope;
        }

        namespace internal
        {
            SW_GAMESERVICE_API const ISubsystemLocator* findHostSubsystems()
            {
                // 호스트가 게임 서비스를 묶지 않은 실행(쿠커 · 테스트 하네스 · 체인 단계 사이)은 엔진 범위를 보되, 묶인 게임 모듈과 같은 노출 규칙으로 거른다.
                if ( s_gameSubsystemState._pHostSubsystems == nullptr )
                    return engine::getEngineSubsystems() != nullptr ? &s_gameVisibleEngineLocator : nullptr;
                return s_gameSubsystemState._pHostSubsystems;
            }
        } // namespace internal
    } // namespace game
} // namespace sw
