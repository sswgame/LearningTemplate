/**
 * @file ConfigManager.h
 * @brief Config/ 의 호스트 JSON(EngineConfig 등)을 관리합니다. Resource/ 팩 에셋이 아니므로 AssetManager 와 분리합니다.
 *
 * @note **설정을 구분하는 것은 타입입니다. 이름 문자열이 아닙니다.** 열쇠는 `T::StaticType()->_fullyQualifiedName` 에서
 *       뽑습니다 — 호출부마다 이름을 손으로 적으면 한 곳만 철자가 어긋나도 `getConfig` 가 조용히 nullptr 을 반환하고,
 *       이름의 해시를 열쇠로 쓰면 이름이 달라도 해시가 같을 때 같은 칸을 가리킵니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Config/IConfig.h"

namespace sw
{
    /** @brief 설정 하나를 파일에서 다시 읽었다는 알림입니다. 인자는 설정 타입의 이름(`T::StaticType()->_fullyQualifiedName`)입니다. */
    using ConfigReloadedDelegate = MulticastDelegate<void( const hashed_string& configTypeName )>;

    /** @brief 설정 파일 하나를 읽은 결과입니다. 없는 파일만 기본값으로 떨어지고, 틀린 파일은 기동을 멈춥니다. */
    enum class ConfigReadResult : uint8
    {
        Loaded = 0, ///< 읽었다
        Missing,    ///< 파일이 없다 — 부르는 쪽이 기본값으로 간다
        Invalid,    ///< 파일이 있는데 틀렸다 — 키 이름과 함께 오류를 남겼다
    };

    /**
     * @class ConfigManager
     * @brief 호스트 설정(EngineConfig · GameConfig …)의 표입니다. 파일에서 읽은 설정은 그 경로를 기억해 실행 중에 다시 읽을 수 있습니다
     *        (`reloadConfigFile` — 에디터의 설정 파일 감시가 부릅니다).
     */
    class SW_API ConfigManager
    {
    public:
        ConfigManager()                                  = default;
        ~ConfigManager()                                 = default;
        ConfigManager( const ConfigManager& )            = delete;
        ConfigManager& operator=( const ConfigManager& ) = delete;

        /**
         * @brief 상대 Config 경로의 기준 디렉터리(보통 프로젝트 루트)를 지정합니다.
         * @details `Config/...` 는 상대 경로입니다. 실행 파일은 `build/<preset>/Bin` 에서 도는데 `Config/` 는 프로젝트 루트에 있어,
         *          **현재 작업 디렉터리 기준**으로 찾으면 EngineConfig/GameConfig/EditorConfig 가 모두 "없음" 으로 떨어지고 생성된
         *          기본값으로 조용히 대체됩니다(창 크기 · VSync · 리소스 우선순위 · 게임 키트 모듈 목록이 모두 무시됨).
         *          `Resource/` 처럼 거슬러 올라가 찾은 루트(`ResourceUtil`)를 여기에 넣어 줍니다.
         */
        void setRootDirectory( string_view rootDirectory ) { _rootDirectory = string( rootDirectory ); }

        /**
         * @brief 설정 하나를 파일에서 읽어 등록합니다. 표에 이미 같은 타입이 있으면 덮어씁니다.
         * @return 없으면 `Missing`(로그 없음 — 부르는 쪽이 기본값으로 간다), 틀렸으면 `Invalid`(키 이름과 함께 오류를 남겼다)
         * @note 없는 파일을 곧바로 `readTextFile` 로 읽으면 FileUtil 이 [Error] 를 남겨 정상 기동의 진짜 오류를 가린다 — 존재부터 본다.
         */
        template <typename T>
        [[nodiscard]] ConfigReadResult loadConfigFile( const string& filePath )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            string resolvedPath;
            if ( resolveConfigPath( filePath, resolvedPath ) == false )
                return ConfigReadResult::Missing;

            string jsonStr;
            if ( FileUtil::readTextFile( resolvedPath, jsonStr ) == false )
            {
                SW_LOG_ERROR( "Cannot read config file %#", resolvedPath.c_str() );
                return ConfigReadResult::Invalid;
            }
            if ( loadConfigFromJson<T>( jsonStr, resolvedPath.c_str() ) == false )
                return ConfigReadResult::Invalid;
            _mapSource[getConfigKey<T>()] = ConfigSource{ resolvedPath, &ConfigManager::reloadFromSource<T> };
            return ConfigReadResult::Loaded;
        }

        /** @brief `loadConfigFile` 이 `Loaded` 인지입니다. */
        template <typename T>
        [[nodiscard]] bool loadConfig( const string& filePath )
        {
            return loadConfigFile<T>( filePath ) == ConfigReadResult::Loaded;
        }

        /**
         * @brief 파일에서 읽은 설정 중 경로가 @p changedPath 인 것을 **제자리에서** 다시 읽고 `onConfigReloaded` 를 알립니다.
         * @details 설정 객체는 바꾸지 않고 값만 덮어씁니다 — 기동 때 받아 둔 포인터(`EngineLoop::_pEngineConfig`)가 그대로 유효합니다. JSON 이
         *          깨졌으면 경고를 남기고 이전 값을 그대로 둡니다. 경로는 `FileUtil::pathsEqualNormalized` 로 비교합니다.
         * @return 아는 설정이고 다시 읽었으면 true. 모르는 경로 · 깨진 파일이면 false
         */
        [[nodiscard]] bool reloadConfigFile( string_view changedPath );
        /** @brief 설정을 다시 읽을 때마다 부를 대상입니다. */
        ConfigReloadedDelegate& onConfigReloaded() { return _onConfigReloaded; }
        /** @brief 다시 읽을 수 있는(파일에서 읽은) 설정 수입니다. */
        uint32 getReloadableCount() const { return static_cast<uint32>( _mapSource.size() ); }

        /**
         * @brief 호스트가 기동 때 만든 표입니다(에디터가 설정 파일 감시에 씁니다). 없으면 nullptr 입니다.
         * @details 표는 `EngineLoop` 가 들고, 만든 직후 `setPrimary` 로 알리고 놓기 전에 거둡니다. 서비스 목록에 올리지 않은 것은 게임 모듈이 볼
         *          이유가 없어서입니다.
         */
        static ConfigManager* findPrimary();
        /** @brief 호스트의 표를 알립니다. 놓을 때 nullptr 를 넘깁니다. */
        static void setPrimary( ConfigManager* pManager );

        /** @brief 설정 하나를 JSON 본문에서 엄격하게(`readConfigJson`) 읽어 등록합니다. 틀리면 표를 바꾸지 않습니다. */
        template <typename T>
        [[nodiscard]] bool loadConfigFromJson( const string& jsonStr, const utf8* pSourceLabel = "json" )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            unique_ptr<T> newConfig = make_unique<T>();
            if ( readConfigJson( *newConfig, jsonStr, pSourceLabel ) == false )
                return false;
            _mapConfig[getConfigKey<T>()] = std::move( newConfig );
            SW_LOG_INFO( "Config loaded successfully from %#", pSourceLabel );
            return true;
        }

        /**
         * @brief 설정 하나를 등록하고 포인터를 반환합니다. **파일이 없을 때만** 생성 JSON(있으면) → `T{}` 로 떨어집니다.
         * @details 파일이 있는데 틀리면(모르는 키 · 읽지 못한 값 · 범위 밖) nullptr 입니다 — 오류는 키 이름과 함께 이미 남겼고, 부르는 쪽이 기동을 멈춥니다.
         *          구운 사본으로 조용히 떨어지면 고친 값이 무시된 것을 아무도 모른다.
         *          **Shipping 은 디스크의 `Config/` 를 보지 않습니다.** 생성 JSON 만 쓰고, 그것이 틀려도 nullptr 입니다.
         */
        template <typename T>
        T* ensureConfig( const string& filePath, const utf8* pGeneratedJson = nullptr )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            const utf8* const pTypeName = T::StaticType()->_fullyQualifiedName.c_str();

#if defined( SW_SHIPPING )
            (void)filePath;
#else
            const ConfigReadResult result = loadConfigFile<T>( filePath );
            if ( result == ConfigReadResult::Loaded )
            {
                SW_LOG_TRACE( "%# source=file (%#)", pTypeName, filePath.c_str() );
                return getConfig<T>();
            }
            if ( result == ConfigReadResult::Invalid )
                return nullptr;
#endif
            if ( StringUtil::isNullOrEmpty( pGeneratedJson ) == false )
            {
                if ( loadConfigFromJson<T>( string( pGeneratedJson ), "shipping_host_generated" ) == false )
                    return nullptr;
#if !defined( SW_SHIPPING )
                SW_LOG_WARNING( "%# file %# is missing - using the generated defaults", pTypeName, filePath.c_str() );
#endif
                return getConfig<T>();
            }

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

        /**
         * @brief 설정 JSON 하나를 @p pInstance 에 엄격하게 읽습니다.
         * @details 모르는 키 · 대소문자만 다른 키 · 읽지 못한 값 · `PROPERTY( Min/Max )` 밖 숫자를 **모두** 키 이름과 @p pSourceLabel 로 오류 로그에 남기고
         *          false 입니다(첫 오류에서 멈추지 않는다 — 한 번에 다 고치게). 설정 아닌 리플렉션 타입(`EditorToolDefaults`)도 이것으로 읽습니다.
         *          구조체 칸 안의 모르는 키는 바깥 칸 이름과 본문으로 나옵니다.
         */
        [[nodiscard]] static bool readConfigJson( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr, const utf8* pSourceLabel );

        /** @brief `readConfigJson` 의 타입 판입니다. */
        template <typename T>
        [[nodiscard]] static bool readConfigJson( T& outConfig, string_view jsonStr, const utf8* pSourceLabel )
        {
            return readConfigJson( &outConfig, *T::StaticType(), jsonStr, pSourceLabel );
        }

        /**
         * @brief 설정 JSON 의 키 가운데 값이 타입의 기본값과 같은 것을 모읍니다(구조체 칸은 안으로 들어가 `_window._width` 처럼). 읽지 못하는 JSON 이면 false 입니다.
         * @details 설정 파일에는 기본값과 다른 값만 적는다(언리얼 `Default*.ini` 와 같다) — 기본값을 다시 적은 줄은 코드의 기본값을 바꿔도
         *          따라가지 않는 옛 값이 된다. 두 값은 같은 직렬화기로 다시 써서 비교하므로 숫자 · 색 표기 차이는 같다고 본다.
         */
        template <typename T>
        [[nodiscard]] static bool collectDefaultEchoKeys( string_view jsonStr, vector<string>& outListKey )
        {
            T loaded{};
            if ( readConfigJson( loaded, jsonStr, "default echo check" ) == false )
                return false;
            const T defaultValue{};
            return collectEqualKeys( &loaded, &defaultValue, *T::StaticType(), jsonStr, outListKey );
        }

        /** @brief @p jsonStr 의 키마다 두 인스턴스의 그 칸이 같은 JSON 으로 쓰이면 @p outListKey 에 담습니다(`collectDefaultEchoKeys` 의 몸). */
        [[nodiscard]] static bool collectEqualKeys( const void* pLeft, const void* pRight, const TypeInfo& typeInfo, string_view jsonStr, vector<string>& outListKey );

        /** @brief 파일을 `readConfigJson` 으로 읽습니다. 없으면 `Missing`(로그 없음)입니다. @p absolutePath 는 그대로 씁니다. */
        template <typename T>
        [[nodiscard]] static ConfigReadResult readConfigFile( T& outConfig, string_view absolutePath )
        {
            if ( FileUtil::isRegularFile( absolutePath ) == false )
                return ConfigReadResult::Missing;
            string jsonStr;
            if ( FileUtil::readTextFile( absolutePath, jsonStr ) == false )
                return ConfigReadResult::Invalid;
            const string label( absolutePath );
            return readConfigJson( outConfig, jsonStr, label.c_str() ) ? ConfigReadResult::Loaded : ConfigReadResult::Invalid;
        }

    private:
        /** @brief 파일에서 읽은 설정 하나의 경로와 다시 읽는 함수입니다. */
        struct ConfigSource
        {
            using ReloadFunc = bool ( * )( ConfigManager& manager, const string& resolvedPath );

            string     _resolvedPath;
            ReloadFunc _pReload{ nullptr };
        };

        /** @brief T 를 @p resolvedPath 에서 새로 읽어 표의 객체에 값으로 덮어씁니다. */
        template <typename T>
        [[nodiscard]] static bool reloadFromSource( ConfigManager& manager, const string& resolvedPath )
        {
            T* pExisting = manager.getConfig<T>();
            if ( pExisting == nullptr )
                return false;
            string jsonStr;
            if ( FileUtil::readTextFile( resolvedPath, jsonStr ) == false )
            {
                SW_LOG_WARNING( "Config reload: could not read %#", resolvedPath.c_str() );
                return false;
            }
            unique_ptr<T> reloaded = make_unique<T>();
            if ( readConfigJson( *reloaded, jsonStr, resolvedPath.c_str() ) == false )
            {
                SW_LOG_WARNING( "Config reload: %# is not valid - keeping the previous values", resolvedPath.c_str() );
                return false;
            }
            *pExisting = std::move( *reloaded );
            SW_LOG_INFO( "Config reloaded from %#", resolvedPath.c_str() );
            manager._onConfigReloaded.broadcast( T::StaticType()->_fullyQualifiedName );
            return true;
        }

        /**
         * @brief 설정 타입 하나를 가리키는 표의 열쇠입니다.
         * @details `hashed_string` 의 intern 인덱스라 **충돌이 없습니다**(해시와 달리). 그리고 T 에서
         *          뽑으므로, 표에 담긴 것이 T 가 아닌 일은 생기지 않습니다. 아래 `static_cast` 가
         *          안전한 이유가 여기 있습니다.
         */
        template <typename T>
        static uint32 getConfigKey()
        {
            return T::StaticType()->_fullyQualifiedName.getIndex();
        }

        /**
         * @brief 설정 파일의 실제 위치를 찾습니다.
         * @details 절대 경로 → 현재 작업 디렉터리 → 루트 디렉터리(프로젝트 루트) → 실행 파일 디렉터리
         *          순으로 **존재 여부만** 확인합니다. 읽기 전에 존재를 확인하므로, 없을 때 FileUtil 이
         *          [Error] 를 남기지 않습니다.
         * @return 찾으면 true 이고 @p outResolvedPath 에 실제 경로가 담깁니다.
         */
        bool resolveConfigPath( const string& filePath, string& outResolvedPath ) const
        {
            if ( filePath.empty() )
                return false;

            if ( FileUtil::isAbsolutePath( filePath ) )
            {
                outResolvedPath = filePath;
                return FileUtil::isRegularFile( outResolvedPath );
            }

            if ( FileUtil::isRegularFile( filePath ) )
            {
                outResolvedPath = filePath;
                return true;
            }

            if ( _rootDirectory.empty() == false )
            {
                string candidate = FileUtil::joinPath( _rootDirectory, filePath );
                if ( FileUtil::isRegularFile( candidate ) )
                {
                    outResolvedPath = std::move( candidate );
                    return true;
                }
            }

            const string exeDir = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
            if ( exeDir.empty() == false )
            {
                string candidate = FileUtil::joinPath( exeDir, filePath );
                if ( FileUtil::isRegularFile( candidate ) )
                {
                    outResolvedPath = std::move( candidate );
                    return true;
                }
            }
            return false;
        }

        unordered_map<uint32, unique_ptr<IConfig>> _mapConfig;
        unordered_map<uint32, ConfigSource>        _mapSource; ///< 파일에서 읽은 설정 → 경로 · 다시 읽는 함수
        ConfigReloadedDelegate                     _onConfigReloaded;
        /// @brief 상대 Config 경로의 기준 디렉터리입니다. 비어 있으면 작업 디렉터리 · 실행 파일 위치만 봅니다.
        string _rootDirectory;
    };

} // namespace sw
