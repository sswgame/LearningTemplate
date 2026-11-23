#include "pch.h"

#include "Engine/Physics/Scene/PhysicsNameValidation.h"

#include "Core/Container/string.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    namespace
    {
        struct PhysicsNameValidationInternal
        {
            /**
             * @brief 물리 설정 파일을 읽습니다. 못 읽으면 단언하고 빈 표입니다 — 기본 표를 지어내지 않으므로 모든 이름이 "표에 없음" 오류로 드러납니다.
             */
            static PhysicsSettings loadSettingsFromFile()
            {
                PhysicsSettings settings;
                const bool      bLoaded = settings.loadFromAsset( AssetID{ PhysicsSystem::kSettingsPath } );
                SW_LOG_ASSERT( bLoaded, "Physics settings '%#' could not be loaded for name validation", PhysicsSystem::kSettingsPath );
                if ( bLoaded == false )
                    settings = PhysicsSettings{};
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
    void PhysicsNameValidation::validateLayer( ValidationContext& context, const utf8* pFieldName, const hashed_string& name )
    {
        if ( name.empty() == false && PhysicsNameValidationInternal::getSettings().findLayerIndex( name ).has_value() == false )
        {
            fixed_string<constant::buffer::kMax512> message;
            formatstring( message.data(), message.capacity(), "%# names '%#', which is not in the physics layers (physicssettings.xml) - layer 0 is used", pFieldName, name.c_str() );
            context.addError( message );
        }
    }

    void PhysicsNameValidation::validateMaterial( ValidationContext& context, const utf8* pFieldName, const hashed_string& name )
    {
        if ( name.empty() == false && PhysicsNameValidationInternal::getSettings().findMaterial( name ) == nullptr )
        {
            fixed_string<constant::buffer::kMax512> message;
            formatstring( message.data(), message.capacity(), "%# names '%#', which is not in the physics materials (physicssettings.xml) - the first material is used", pFieldName,
                          name.c_str() );
            context.addError( message );
        }
    }

    void PhysicsNameValidation::collectProfileNames( vector<hashed_string>& outListName )
    {
        outListName.clear();
        for ( const CollisionProfileDef& profile : PhysicsNameValidationInternal::getSettings()._listProfile )
        {
            outListName.push_back( profile._name );
        }
    }

    void PhysicsNameValidation::collectChannelNames( vector<hashed_string>& outListName )
    {
        outListName.clear();
        for ( const CollisionChannelDef& channel : PhysicsNameValidationInternal::getSettings()._listChannel )
        {
            outListName.push_back( channel._name );
        }
    }

    void PhysicsNameValidation::collectMaterialNames( vector<hashed_string>& outListName )
    {
        outListName.clear();
        for ( const PhysicsMaterialDef& material : PhysicsNameValidationInternal::getSettings()._listMaterial )
        {
            outListName.push_back( material._name );
        }
    }
} // namespace sw
