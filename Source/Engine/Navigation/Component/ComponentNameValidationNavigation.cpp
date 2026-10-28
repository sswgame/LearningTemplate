#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Asset/AssetIO.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Reflection/ReflectionValidation.h"

// ComponentNameValidation 의 내비 영역 몫 — 컴포넌트 모델 코어(Object/Component)는 Navigation 을 include 하지 않으므로 기능 폴더에 둔다.

namespace sw
{
    namespace
    {
        struct ComponentNameValidationNavigationInternal
        {
            /** @brief `SceneNavigation::getSettings` 와 같은 파일을 읽습니다. 못 읽으면 기본 표입니다. */
            static NavMeshSettings loadSettingsFromFile()
            {
                NavMeshSettings settings;
                const AssetID   settingsID{ engine::getSubsystem<EngineDefaultAssets>()._navMeshSettings.get() };
                if ( AssetIO::hasAsset( settingsID ) == false || settings.loadFromAsset( settingsID ) == false )
                    settings = NavMeshSettings{}; // 코드가 표를 채우지 않는다 - 빈 표면 모든 영역 이름이 오류로 보인다
                return settings;
            }

            /** @brief 내비 표 — 파일을 한 번(스레드 안전하게) 읽은 것입니다. */
            static const NavMeshSettings& getSettings()
            {
                static const NavMeshSettings s_settings = loadSettingsFromFile();
                return s_settings;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void ComponentNameValidation::validateNavArea( ValidationContext& context, const utf8* pFieldName, const hashed_string& name )
    {
        uint8 areaIndex = 0;
        if ( name.empty() == false && ComponentNameValidationNavigationInternal::getSettings().findAreaIndex( name, areaIndex ) == false )
            context.addError( string( pFieldName ) + " names '" + name.c_str() + "', which is not in the navigation areas (navmeshsettings.xml) - it bakes as plain ground" );
    }
} // namespace sw
