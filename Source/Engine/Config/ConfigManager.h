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

        /**
         * @brief 상대 Config 경로의 기준 디렉터리(보통 프로젝트 루트)를 지정합니다.
         * @details `Config/...` 경로는 상대 경로라 예전엔 **현재 작업 디렉터리 기준**으로만 찾았다.
         *          실행 파일은 `build/<preset>/Bin` 에서 도는데 `Config/` 는 프로젝트 루트에 있어서,
         *          EngineConfig/GameConfig/AppConfig 가 매 실행마다 전부 "없음"으로 떨어지고 베이크된
         *          기본값으로 조용히 대체되고 있었다(창 크기·VSync·리소스 우선순위·게임킷 모듈 목록이
         *          전부 무시됨). `Resource/` 는 상위 디렉터리를 거슬러 올라가 루트를 찾는데
         *          (`ResourceUtil`) Config 만 그 혜택을 못 받고 있었다 — 그 루트를 여기에 넣어준다.
         */
        void setRootDirectory( string_view rootDirectory ) { _rootDirectory = string( rootDirectory ); }

        template <typename T>
        bool loadConfig( const hashed_string& name, const string& filePath )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            string resolvedPath;
            if ( resolveConfigPath( filePath, resolvedPath ) == false )
            {
                // 존재하지 않는 설정 파일은 오류가 아니다 — 호출부(ensureConfig)가 베이크된 기본값으로
                // 정상 폴백한다. 여기서 곧바로 readTextFile 을 부르면 FileUtil 이 [Error] 를 남겨서
                // "정상 기동인데 매번 오류 3건"이 되어 진짜 오류를 가린다.
                SW_LOG_WARNING( "Failed to load config from: %#", filePath.c_str() );
                return false;
            }

            string jsonStr;
            if ( FileUtil::readTextFile( resolvedPath, jsonStr ) == false )
            {
                SW_LOG_WARNING( "Failed to load config from: %#", resolvedPath.c_str() );
                return false;
            }

            return loadConfigFromJson<T>( name, jsonStr, resolvedPath.c_str() );
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
        /**
         * @brief 설정 파일의 실제 위치를 찾습니다.
         * @details 절대경로 → 현재 작업 디렉터리 → 루트 디렉터리(프로젝트 루트) → 실행 파일 디렉터리
         *          순으로 **존재 여부만** 확인한다. 읽기 전에 존재를 확인하므로, 없을 때 FileUtil 이
         *          [Error] 를 남기지 않는다.
         * @return 찾으면 true 이고 @p outResolvedPath 에 실제 경로가 담긴다.
         */
        bool resolveConfigPath( const string& filePath, string& outResolvedPath ) const
        {
            if ( filePath.empty() )
                return false;

            if ( FileUtil::isAbsolutePath( filePath ) )
            {
                outResolvedPath = filePath;
                return FileUtil::fileExists( outResolvedPath );
            }

            if ( FileUtil::fileExists( filePath ) )
            {
                outResolvedPath = filePath;
                return true;
            }

            if ( _rootDirectory.empty() == false )
            {
                string candidate = FileUtil::joinPath( _rootDirectory, filePath );
                if ( FileUtil::fileExists( candidate ) )
                {
                    outResolvedPath = std::move( candidate );
                    return true;
                }
            }

            const string exeDir = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
            if ( exeDir.empty() == false )
            {
                string candidate = FileUtil::joinPath( exeDir, filePath );
                if ( FileUtil::fileExists( candidate ) )
                {
                    outResolvedPath = std::move( candidate );
                    return true;
                }
            }
            return false;
        }

        unordered_map<uint32, unique_ptr<IConfig>> _mapConfig;
        /// @brief 상대 Config 경로의 기준 디렉터리. 비어 있으면 작업 디렉터리/실행 파일 위치만 본다.
        string _rootDirectory;
    };

} // namespace sw
