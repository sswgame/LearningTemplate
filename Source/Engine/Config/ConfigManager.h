/**
 * @file ConfigManager.h
 * @brief Config/ 호스트 JSON (EngineConfig 등). Resource/ 팩 에셋이 아니므로 ResourceManager와 분리합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Serialization/Format/JsonSerializer.h"

namespace sw
{
    class SW_API ConfigManager
    {
    public:
        ConfigManager()                                  = default;
        ~ConfigManager()                                 = default;
        ConfigManager( const ConfigManager& )            = delete;
        ConfigManager& operator=( const ConfigManager& ) = delete;

        template <typename T>
        bool loadConfig( const hashed_string& name, const string& filePath )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            string jsonStr;
            if ( FileUtil::readTextFile( filePath, jsonStr ) == false )
            {
                SW_LOG_WARNING( "Failed to load config from: %#", filePath.c_str() );
                return false;
            }

            return loadConfigFromJson<T>( name, jsonStr, filePath.c_str() );
        }

        template <typename T>
        bool loadConfigFromJson( const hashed_string& name, const string& jsonStr, [[maybe_unused]] const utf8* pSourceLabel = "json" )
        {

            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            unique_ptr<T> newConfig = make_unique<T>();
            if ( JsonSerializer::deserialize( newConfig.get(), *T::StaticType(), jsonStr ) == false )
            {
                SW_LOG_ERROR( "Failed to deserialize config from: %#", pSourceLabel );
                return false;
            }

            _mapConfig[name.getHash()] = std::move( newConfig );
            SW_LOG_INFO( "Config loaded successfully from %#", pSourceLabel );
            return true;
        }

        /**
         * @brief 파일 로드 성공 시 그 값, 실패 시 bakedJson(있으면) 또는 T{} 를 등록하고 포인터 반환.
         * @details missing/깨진 JSON으로 기동을 중단하지 않는다 (Shipping/Dev soft-fail).
         */
        template <typename T>
        T* ensureConfig( const hashed_string& name, const string& filePath, const utf8* pBakedJson = nullptr )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

#if defined( SW_SHIPPING )
            (void)filePath;
            if ( StringUtil::isNullOrEmpty( pBakedJson ) == false )
            {
                if ( loadConfigFromJson<T>( name, string( pBakedJson ), "shipping_host_baked" ) )
                {
                    SW_LOG_TRACE( "%# source=baked", name.c_str() );
                    return getConfig<T>( name );
                }
            }
#else
            if ( loadConfig<T>( name, filePath ) )
            {
                SW_LOG_TRACE( "%# source=file (%#)", name.c_str(), filePath.c_str() );
                return getConfig<T>( name );
            }
            if ( StringUtil::isNullOrEmpty( pBakedJson ) == false )
            {
                if ( loadConfigFromJson<T>( name, string( pBakedJson ), "shipping_host_baked_fallback" ) )
                {
                    SW_LOG_WARNING( "%# missing %# — using baked defaults", name.c_str(), filePath.c_str() );
                    return getConfig<T>( name );
                }
            }
#endif

            unique_ptr<T> fallback     = make_unique<T>();
            T*            pRaw         = fallback.get();
            _mapConfig[name.getHash()] = std::move( fallback );
            SW_LOG_WARNING( "%# using cpp defaults", name.c_str() );
            return pRaw;
        }

        template <typename T>
        T* getConfig( const hashed_string& name ) const
        {
            const auto iter = _mapConfig.find( name.getHash() );
            if ( iter != _mapConfig.end() )
                return static_cast<T*>( iter->second.get() );
            return nullptr;
        }

    private:
        unordered_map<uint32, unique_ptr<IConfig>> _mapConfig;
    };

} // namespace sw
