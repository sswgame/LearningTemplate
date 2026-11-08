/**
 * @file DevTools/Diagnostics/GameAutoplaySwitch.cpp
 * @brief 게임 자동 플레이의 개발 쪽 — 콘솔 `autoplay` 와 런타임 칸 `IGameAutoplayQuery` 를 채우는 서브시스템입니다.
 * @details 등록 틀(`SW_GAME_AUTOPLAY` · `GameAutoplay`)은 게임이 쓰므로 Engine(`Engine/Config/GameAutoplay.h`)에 남고, 켜는 장치만 개발 도구다(배포 구성은 짓지 않는다 —
 *          칸이 비어 게임 모드는 씬 데이터만 본다). 언리얼 치트 매니저(배포본에 없음)와 같은 갈림.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Config/GameAutoplay.h"
#include "Engine/Config/IGameAutoplayQuery.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    namespace
    {
        struct GameAutoplaySwitchInternal
        {
            static bool runAutoplay( const vector<string>& listArgument, string& outReply )
            {
                const GameAutoplayRegistration* pActive = GameAutoplay::findActive();
                if ( pActive == nullptr )
                {
                    outReply = "this game has no autoplay (SW_GAME_AUTOPLAY)";
                    return false;
                }
                if ( listArgument.size() > 1 )
                    return false;
                if ( listArgument.size() == 1 )
                {
                    bool bOn{ false };
                    if ( StringUtil::tryParseBool( listArgument[0], bOn ) == false )
                        return false;
                    (void)GameAutoplay::setOn( bOn ); // 위에서 등록을 확인했다
                }
                outReply = string( pActive->_pGameName ) + " autoplay " + ( GameAutoplay::isOn() ? "on" : "off" ) + " (" + pActive->_pVariableName + ")";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( Autoplay, "autoplay", "autoplay [on|off]", "Show or switch the game's autoplay (AI drives the player - SW_GAME_AUTOPLAY)",
                    &GameAutoplaySwitchInternal::runAutoplay );
} // namespace sw

namespace sw
{
    /** @brief 런타임 칸 `IGameAutoplayQuery` 의 구현입니다 — 등록된 개발 스위치를 읽습니다. */
    class GameAutoplayQuery final : public IGameAutoplayQuery
    {
    public:
        [[nodiscard]] bool isGameAutoplayOn() const override { return GameAutoplay::isOn(); }
    };

    /**
     * @class GameAutoplaySubsystem
     * @brief 개발 구성에서 런타임 칸 `IGameAutoplayQuery` 를 채우는 서브시스템입니다. 배포 구성은 이 파일을 짓지 않아 칸이 비고, 게임 모드는 씬 데이터만 본다.
     * @details 창구는 멤버로 들고 `exposeMember` 로 건다(다중 상속을 두지 않는다).
     */
    class GameAutoplaySubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "GameAutoplaySubsystem";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        IGameAutoplayQuery& getQuery() { return _query; }

    private:
        GameAutoplayQuery _query;
    };

    SW_ENGINE_SUBSYSTEM( GameAutoplaySubsystem, Services, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        GameAutoplaySubsystem& subsystem = scope.emplaceSubsystem<GameAutoplaySubsystem>();
        const bool             bExposed  = scope.exposeMember<IGameAutoplayQuery>( subsystem, subsystem.getQuery() );
        SW_ASSERT( bExposed );
        (void)bExposed;
    } );
} // namespace sw
