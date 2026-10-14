/**
 * @file ConfigManager.h
 * @brief Config/ 의 호스트 JSON(EngineConfig 등)을 관리합니다. Resource/ 팩 에셋이 아니므로 AssetManager 와 분리합니다.
 *
 * @note **설정을 구분하는 것은 타입입니다. 이름 문자열이 아닙니다.** 키는 `T::StaticType()->_fullyQualifiedName` 에서
 *       만듭니다. 호출부마다 이름을 손으로 적으면 한 곳만 철자가 어긋나도 `getConfig` 가 조용히 nullptr 을 반환하고,
 *       이름의 해시를 키로 쓰면 이름이 달라도 해시가 같을 때 같은 항목을 가리킵니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Config/IConfig.h"

namespace sw
{
    /** @brief 설정 하나를 파일에서 다시 읽었다는 알림입니다. 인자는 설정 타입의 이름(`T::StaticType()->_fullyQualifiedName`)입니다. */
    using ConfigReloadedDelegate = MulticastDelegate<void( const hashed_string& configTypeName )>;

    /** @brief 설정 파일 하나를 읽은 결과입니다. 없는 파일만 기본값으로 대체하고, 틀린 파일은 기동을 멈춥니다. */
    enum class ConfigReadResult : uint8
    {
        Loaded = 0, ///< 읽었다
        Missing,    ///< 파일이 없다. 부르는 쪽이 기본값을 쓴다
        Invalid,    ///< 파일이 있는데 틀렸다. 키 이름과 함께 오류를 남겼다
    };

    /**
     * @class ConfigManager
     * @brief 호스트 설정(EngineConfig, GameConfig 등)의 테이블입니다. 파일에서 읽은 설정은 그 경로를 기억해 실행 중에 다시 읽을 수 있습니다
     *        (`reloadConfigFile`. 에디터의 설정 파일 감시가 부릅니다).
     */
    class SW_API ConfigManager : public ISubsystem
    {
    public:
        ConfigManager() = default;
        /** @brief 이 표가 주 표(`setPrimary`)면 알림을 거둡니다 — 사라진 표를 게임 모듈이 찾지 않는다. */
        ~ConfigManager() override;
        ConfigManager( const ConfigManager& )            = delete;
        ConfigManager& operator=( const ConfigManager& ) = delete;

        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Config";
        /**
         * @brief 기동 순서의 자리 — 프로젝트 루트를 기준으로 엔진 · 게임 설정(EngineConfig · GameConfig)을 읽어 활성으로 두고, 이 표를 주 표로 알립니다.
         * @details 틀린 설정 파일은 실패다(키 이름은 이미 오류로 남았다) — 기본값으로 뜨면 고친 값이 무시된 것을 아무도 모른다. Dev 는 고른 게임(`-game=`)의
         *          파일만 읽는다. 배포 구성은 생성된 JSON 을 읽으며, 그것이 역직렬화되지 않으면(정적 링크에서 리플렉션 등록기가 빠졌다) 실패로 드러낸다.
         */
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 상대 Config 경로의 기준 디렉터리(보통 프로젝트 루트)를 지정합니다.
         * @details `Config/...` 는 상대 경로다. 실행 파일은 `build/<preset>/Bin` 에서 도는데 `Config/` 는 프로젝트 루트에 있어,
         *          **현재 작업 디렉터리 기준**으로 찾으면 EngineConfig, GameConfig, EditorConfig 가 모두 "없음"으로 처리되고 생성된
         *          기본값으로 조용히 대체된다(창 크기, VSync, 리소스 우선순위, 게임 키트 모듈 목록이 모두 무시된다).
         *          `Resource/` 처럼 거슬러 올라가 찾은 루트(`ResourcePaths`)를 여기에 넣어 준다.
         */
        void setRootDirectory( string_view rootDirectory ) { _rootDirectory = string( rootDirectory ); }

        /**
         * @brief 설정 하나를 파일에서 읽어 등록합니다. 테이블에 이미 같은 타입이 있으면 덮어씁니다.
         * @return 없으면 `Missing`(로그 없음, 부르는 쪽이 기본값을 쓴다), 틀렸으면 `Invalid`(키 이름과 함께 오류를 남겼다)
         * @note 없는 파일을 곧바로 `readTextFile` 로 읽으면 FileUtil 이 [Error] 를 남겨 정상 기동의 진짜 오류를 가린다. 존재부터 확인한다.
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
            if ( loadConfigFromJSON<T>( jsonStr, resolvedPath.c_str() ) == false )
                return ConfigReadResult::Invalid;
            _mapSource[getConfigKey<T>()] = ConfigSource{ resolvedPath, &ConfigManager::reloadFromSource<T> };
            return ConfigReadResult::Loaded;
        }

        /** @brief `loadConfigFile` 이 `Loaded` 이면 true 입니다. */
        template <typename T>
        [[nodiscard]] bool loadConfig( const string& filePath )
        {
            return loadConfigFile<T>( filePath ) == ConfigReadResult::Loaded;
        }

        /**
         * @brief 파일에서 읽은 설정 중 경로가 @p changedPath 인 것을 **제자리에서** 다시 읽고 `onConfigReloaded` 를 알립니다.
         * @details 설정 객체는 바꾸지 않고 값만 덮어쓴다. 기동 때 받아 둔 포인터(`Engine::_pEngineConfig`)가 그대로 유효하다. JSON 이
         *          깨졌으면 경고를 남기고 이전 값을 그대로 둔다. 경로는 `FileUtil::pathsEqualNormalized` 로 비교한다.
         * @return 아는 설정이고 다시 읽었으면 true. 모르는 경로나 깨진 파일이면 false
         */
        [[nodiscard]] bool reloadConfigFile( string_view changedPath );
        /** @brief 설정을 다시 읽을 때마다 호출되는 델리게이트입니다. */
        ConfigReloadedDelegate& onConfigReloaded() { return _onConfigReloaded; }
        /** @brief 다시 읽을 수 있는(파일에서 읽은) 설정 수입니다. */
        uint32 getReloadableCount() const { return static_cast<uint32>( _mapSource.size() ); }

        /**
         * @brief 호스트가 기동 때 만든 테이블입니다(에디터가 설정 파일 감시에 씁니다). 없으면 nullptr 입니다.
         * @details 테이블은 `Engine` 가 보관하고, 만든 직후 `setPrimary` 로 알리고 해제하기 전에 회수한다. 서비스 목록에 올리지 않은 것은 게임 모듈이 볼
         *          이유가 없기 때문이다.
         */
        static ConfigManager* findPrimary();
        /** @brief 호스트의 테이블을 알립니다. 해제할 때 nullptr 를 넘깁니다. */
        static void setPrimary( ConfigManager* pManager );

        /** @brief 설정 하나를 JSON 본문에서 엄격하게(`readConfigJSON`) 읽어 등록합니다. 틀리면 테이블을 바꾸지 않습니다. */
        template <typename T>
        [[nodiscard]] bool loadConfigFromJSON( const string& jsonStr, const utf8* pSourceLabel = "json" )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            unique_ptr<T> newConfig = make_unique<T>();
            if ( readConfigJSON( *newConfig, jsonStr, pSourceLabel ) == false )
                return false;
            _mapConfig[getConfigKey<T>()] = std::move( newConfig );
            SW_LOG_INFO( "Config loaded successfully from %#", pSourceLabel );
            return true;
        }

        /**
         * @brief 설정 하나를 등록하고 포인터를 반환합니다. **파일이 없을 때만** 생성 JSON(있으면), 그다음 `T{}` 로 대체합니다.
         * @details 파일이 있는데 틀리면(모르는 키, 읽지 못한 값, 범위 밖) nullptr 입니다. 오류는 키 이름과 함께 이미 남겼고, 부르는 쪽이 기동을 멈춘다.
         *          생성된 사본으로 조용히 대체하면 고친 값이 무시된 것을 아무도 모른다.
         *          **Shipping 은 디스크의 `Config/` 를 보지 않습니다.** 생성 JSON 만 쓰고, 그것이 틀려도 nullptr 입니다.
         */
        template <typename T>
        T* ensureConfig( const string& filePath, const utf8* pGeneratedJSON = nullptr )
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
            if ( StringUtil::isNullOrEmpty( pGeneratedJSON ) == false )
            {
                if ( loadConfigFromJSON<T>( string( pGeneratedJSON ), "shipping_host_generated" ) == false )
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
         * @details 모르는 키, 대소문자만 다른 키, 읽지 못한 값, `PROPERTY( Min/Max )` 밖 숫자를 **모두** 키 이름과 @p pSourceLabel 로 오류 로그에 남기고
         *          false 를 반환한다(첫 오류에서 멈추지 않는다. 한 번에 다 고치게 하려는 것이다). 설정이 아닌 리플렉션 타입(`EditorToolDefaults`)도 이 함수로 읽는다.
         *          구조체 필드 안의 모르는 키는 바깥 필드 이름과 본문으로 표시된다.
         */
        [[nodiscard]] static bool readConfigJSON( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr, const utf8* pSourceLabel );

        /** @brief `readConfigJSON` 의 타입 지정 버전입니다. */
        template <typename T>
        [[nodiscard]] static bool readConfigJSON( T& outConfig, string_view jsonStr, const utf8* pSourceLabel )
        {
            return readConfigJSON( &outConfig, *T::StaticType(), jsonStr, pSourceLabel );
        }

        /**
         * @brief 설정 JSON 의 키 가운데 값이 타입의 기본값과 같은 것을 모읍니다(구조체 필드는 안으로 들어가 `_window._width` 처럼 표기). 읽지 못하는 JSON 이면 false 입니다.
         * @details 설정 파일에는 기본값과 다른 값만 적는다(언리얼 `Default*.ini` 와 같다). 기본값을 다시 적은 줄은 코드의 기본값을 바꿔도
         *          따라가지 않는 옛 값이 된다. 두 값은 같은 직렬화기로 다시 써서 비교하므로 숫자와 색 표기 차이는 같다고 본다.
         */
        template <typename T>
        [[nodiscard]] static bool collectDefaultEchoKeys( string_view jsonStr, vector<string>& outListKey )
        {
            T loaded{};
            if ( readConfigJSON( loaded, jsonStr, "default echo check" ) == false )
                return false;
            const T defaultValue{};
            return collectEqualKeys( &loaded, &defaultValue, *T::StaticType(), jsonStr, outListKey );
        }

        /** @brief @p jsonStr 의 키마다 두 인스턴스의 그 필드가 같은 JSON 으로 쓰이면 @p outListKey 에 담습니다(`collectDefaultEchoKeys` 의 본체). */
        [[nodiscard]] static bool collectEqualKeys( const void* pLeft, const void* pRight, const TypeInfo& typeInfo, string_view jsonStr, vector<string>& outListKey );

        /** @brief 파일을 `readConfigJSON` 으로 읽습니다. 없으면 `Missing`(로그 없음)입니다. @p absolutePath 는 그대로 씁니다. */
        template <typename T>
        [[nodiscard]] static ConfigReadResult readConfigFile( T& outConfig, string_view absolutePath )
        {
            if ( FileUtil::isRegularFile( absolutePath ) == false )
                return ConfigReadResult::Missing;
            string jsonStr;
            if ( FileUtil::readTextFile( absolutePath, jsonStr ) == false )
                return ConfigReadResult::Invalid;
            const string label( absolutePath );
            return readConfigJSON( outConfig, jsonStr, label.c_str() ) ? ConfigReadResult::Loaded : ConfigReadResult::Invalid;
        }

    private:
        /** @brief 파일에서 읽은 설정 하나의 경로와 다시 읽는 함수입니다. */
        struct ConfigSource
        {
            using ReloadFunc = bool ( * )( ConfigManager& manager, const string& resolvedPath );

            string     _resolvedPath;
            ReloadFunc _pReload{ nullptr };
        };

        /** @brief T 를 @p resolvedPath 에서 새로 읽어 테이블의 객체에 값으로 덮어씁니다. */
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
            if ( readConfigJSON( *reloaded, jsonStr, resolvedPath.c_str() ) == false )
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
         * @brief 설정 타입 하나를 가리키는 테이블의 키입니다.
         * @details `hashed_string` 의 intern 인덱스라 **충돌이 없다**(해시와 달리). 그리고 T 에서
         *          만드므로, 테이블에 담긴 것이 T 가 아닌 일은 생기지 않는다. 아래 `static_cast` 가
         *          안전한 이유가 여기 있다.
         */
        template <typename T>
        static uint32 getConfigKey()
        {
            return T::StaticType()->_fullyQualifiedName.getIndex();
        }

        /**
         * @brief 설정 파일의 실제 위치를 찾습니다.
         * @details 절대 경로 → 현재 작업 디렉터리 → 루트 디렉터리(프로젝트 루트) → 실행 파일 디렉터리
         *          순으로 **존재 여부만** 확인한다. 읽기 전에 존재를 확인하므로, 없을 때 FileUtil 이
         *          [Error] 를 남기지 않는다.
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

            const string exeDir = FileUtil::getBinaryDirectory();
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
        unordered_map<uint32, ConfigSource>        _mapSource; ///< 파일에서 읽은 설정에서 경로와 다시 읽는 함수로 가는 맵이다
        ConfigReloadedDelegate                     _onConfigReloaded;
        /// @brief 상대 Config 경로의 기준 디렉터리입니다. 비어 있으면 작업 디렉터리와 실행 파일 위치만 봅니다.
        string _rootDirectory;
    };

} // namespace sw
