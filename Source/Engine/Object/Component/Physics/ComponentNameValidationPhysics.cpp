#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Resource/ResourceIO.h"

// ComponentNameValidation 의 물리 표 몫 — 컴포넌트 모델 코어(Object/Component)는 Physics 를 include 하지 않으므로 기능 폴더에 둔다.

namespace sw
{
    namespace
    {
        struct ComponentNameValidationPhysicsInternal
        {
            /** @brief 물리 설정 파일을 읽습니다. 못 읽으면 기본 표(`Default` 레이어 · 재질 하나)입니다. */
            static PhysicsSettings loadSettingsFromFile()
            {
                PhysicsSettings settings;
                if ( ResourceIO::hasResource( PhysicsSystem::kSettingsPath ) == false || settings.loadFromResource( PhysicsSystem::kSettingsPath ) == false )
                    settings = PhysicsSettings{};
                settings.ensureDefaults();
                return settings;
            }

            /** @brief 런타임이 쓰는 물리 표 — 초기화된 `PhysicsSystem` 이 있으면 그것, 없으면 파일을 한 번(스레드 안전하게) 읽은 것입니다. */
            static const PhysicsSettings& getSettings()
            {
                const PhysicsSystem* pSystem = engine::areEngineServicesBound() ? engine::findSubsystem<PhysicsSystem>() : nullptr;
                if ( pSystem != nullptr && pSystem->isInitialized() )
                    return pSystem->getSettings();
                static const PhysicsSettings s_settingsFromFile = loadSettingsFromFile();
                return s_settingsFromFile;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void ComponentNameValidation::validatePhysicsLayer( ValidationContext& context, const utf8* pFieldName, const hashed_string& name )
    {
        uint8 layerIndex = 0;
        if ( name.empty() == false && ComponentNameValidationPhysicsInternal::getSettings().findLayerIndex( name, layerIndex ) == false )
            context.addError( string( pFieldName ) + " names '" + name.c_str() + "', which is not in the physics layers (physicssettings.xml) - layer 0 is used" );
    }

    void ComponentNameValidation::validatePhysicsMaterial( ValidationContext& context, const utf8* pFieldName, const hashed_string& name )
    {
        if ( name.empty() == false && ComponentNameValidationPhysicsInternal::getSettings().findMaterial( name ) == nullptr )
            context.addError( string( pFieldName ) + " names '" + name.c_str() + "', which is not in the physics materials (physicssettings.xml) - the first material is used" );
    }
} // namespace sw
