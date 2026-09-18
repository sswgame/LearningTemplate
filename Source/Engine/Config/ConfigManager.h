/**
 * @file ConfigManager.h
 * @brief Config/ 호스트 JSON (EngineConfig 등). Resource/ 팩 에셋이 아니므로 ResourceManager와 분리합니다.
 *
 * @note **설정의 정체성은 타입이다 — 이름 문자열이 아니다.** 예전에는 `hashed_string( "EngineConfig" )`
 *       를 호출부마다 손으로 적어 넘겼고(`EngineLoop` · `App` · 테스트 하네스, 셋이 같은 글자를 따로
 *       들고 있었다), 한 곳만 철자가 어긋나면 `getConfig` 가 조용히 nullptr 을 돌려줬다. 게다가
 *       표의 열쇠가 그 이름의 **해시**여서, 이름이 달라도 해시가 같으면 같은 칸을 가리켰다 —
 *       `hashed_string` 자신은 intern 인덱스로 비교하는데(그래서 충돌이 없다) 표만 해시를 쓰고
 *       있었던 것이다. 지금은 열쇠를 `T::StaticType()->_fullyQualifiedName` 에서 뽑는다.
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

        /**
         * @brief 설정 하나를 파일에서 읽어 등록합니다. 파일이 없으면 false 입니다.
         * @details 표에 이미 같은 타입이 있으면 덮어씁니다.
         */
        template <typename T>
        bool loadConfig( const string& filePath )
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

            return loadConfigFromJson<T>( jsonStr, resolvedPath.c_str() );
        }

        /** @brief 설정 하나를 JSON 본문에서 읽어 등록합니다. */
        template <typename T>
        bool loadConfigFromJson( const string& jsonStr, [[maybe_unused]] const utf8* pSourceLabel = "json" )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            unique_ptr<T> newConfig = make_unique<T>();
            if ( JsonSerializer::deserialize( newConfig.get(), *T::StaticType(), jsonStr ) == false )
            {
                SW_LOG_ERROR( "Failed to deserialize config from: %#", pSourceLabel );
                return false;
            }

            _mapConfig[getConfigKey<T>()] = std::move( newConfig );
            SW_LOG_INFO( "Config loaded successfully from %#", pSourceLabel );
            return true;
        }

        /**
         * @brief 파일 로드 성공 시 그 값, 실패 시 bakedJson(있으면) 또는 T{} 를 등록하고 포인터 반환.
         * @details missing/깨진 JSON으로 기동을 중단하지 않는다 (Shipping/Dev soft-fail).
         *          **Shipping 은 디스크의 `Config/` 를 아예 보지 않는다** — 베이크된 JSON 만 쓴다.
         */
        template <typename T>
        T* ensureConfig( const string& filePath, const utf8* pBakedJson = nullptr )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            const utf8* const pTypeName = T::StaticType()->_fullyQualifiedName.c_str();

#if defined( SW_SHIPPING )
            (void)filePath;
            if ( StringUtil::isNullOrEmpty( pBakedJson ) == false )
            {
                if ( loadConfigFromJson<T>( string( pBakedJson ), "shipping_host_baked" ) )
                {
                    SW_LOG_TRACE( "%# source=baked", pTypeName );
                    return getConfig<T>();
                }
            }
#else
            if ( loadConfig<T>( filePath ) )
            {
                SW_LOG_TRACE( "%# source=file (%#)", pTypeName, filePath.c_str() );
                return getConfig<T>();
            }
            if ( StringUtil::isNullOrEmpty( pBakedJson ) == false )
            {
                if ( loadConfigFromJson<T>( string( pBakedJson ), "shipping_host_baked_fallback" ) )
                {
                    SW_LOG_WARNING( "%# missing %# — using baked defaults", pTypeName, filePath.c_str() );
                    return getConfig<T>();
                }
            }
#endif

            unique_ptr<T> fallback        = make_unique<T>();
            T* const      pRaw            = fallback.get();
            _mapConfig[getConfigKey<T>()] = std::move( fallback );
            SW_LOG_WARNING( "%# using cpp defaults", pTypeName );
            return pRaw;
        }

        /** @brief 등록된 설정을 반환합니다. 아직 없으면 nullptr 입니다. */
        template <typename T>
        T* getConfig() const
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            const auto iter = _mapConfig.find( getConfigKey<T>() );
            if ( iter != _mapConfig.end() )
                return static_cast<T*>( iter->second.get() );
            return nullptr;
        }

    private:
        /**
         * @brief 설정 타입 하나를 가리키는 표의 열쇠입니다.
         * @details `hashed_string` 의 intern 인덱스라 **충돌이 없다**(해시와 달리). 그리고 T 에서
         *          뽑으므로, 표에 담긴 것이 T 가 아닌 일은 생기지 않는다 — 아래 `static_cast` 가
         *          안전한 이유가 여기 있다.
         */
        template <typename T>
        static uint32 getConfigKey()
        {
            return T::StaticType()->_fullyQualifiedName.getIndex();
        }

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
