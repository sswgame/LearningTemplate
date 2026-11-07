#include "pch.h"

#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Client/HUD/HUDControllerComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h"

namespace sw
{
    /**
     * @class HUDViewModelBinder
     * @brief 게임이 든 HUD 뷰모델을 오브젝트의 `HUDControllerComponent` 에 거는 클라이언트 쪽 창구입니다(`IHUDViewModelBinder`).
     * @details 상태가 없다 — 부를 때마다 오브젝트에서 HUD 를 찾는다. 이름 해시가 함수 서명 글이라 익명 이름공간이 아닌 `sw` 에 둔다.
     */
    class HUDViewModelBinder final : public ISubsystem, public IHUDViewModelBinder
    {
    public:
        static constexpr const utf8* kSubsystemName = "HUDViewModelBinder";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        bool bindHUDViewModel( GameObject& owner, UIViewModel* pViewModel ) override
        {
            HUDControllerComponent* pHUD = owner.getComponent<HUDControllerComponent>();
            return pHUD != nullptr && pHUD->attachViewModel( pViewModel );
        }
    };

    // 게임 범위에 서고 `IHUDViewModelBinder` 로 찾힌다 — 전용 서버는 이 소스를 짓지 않아 창구가 없다.
    SW_MODULE_SUBSYSTEM( Engine, HUDViewModelBinder, IHUDViewModelBinder );
} // namespace sw
