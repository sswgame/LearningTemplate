/**
 * @file EmptyGame.h
 * @brief 최소 SWGame 템플릿 (키트 없음)
 */
#pragma once
#include "GameFramework/Base/GameInstanceBase.h"

namespace sw
{
    /** @brief 부트스트랩만 지정하는 빈 게임 팩 */
    class EmptyGame : public GameInstanceBase
    {
    public:
        EmptyGame()           = default;
        ~EmptyGame() override = default;

    protected:
        void configureBootstrap( BootstrapConfig& outConfig ) override;
    };
} // namespace sw
