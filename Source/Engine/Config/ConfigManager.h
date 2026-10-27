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
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Base/ReloadOutcome.h"

namespace sw
{
    /** @brief 설정 하나를 파일에서 다시 읽었다는 알림입니다. 인자는 설정 타입의 이름(`T::StaticType()->_fullyQualifiedName`)입니다. */
    using ConfigReloadedDelegate = MulticastDelegate<void( const hashed_string& configTypeName )>;

    /** @brief 설정 파일 하나를 읽은 결과입니다. 없는 파일을 기본값으로 대신하지 않습니다. 필수 설정이면 오류이고, 없어도 되는 설정만 부르는 쪽이 없음으로 다룹니다. */
    enum class ConfigReadResult : uint8
    {
        Loaded = 0, ///< 읽었다
        Missing,    ///< 파일이 없다. 필수 설정(`loadHostConfig`)이면 오류이고, 선택 설정(`loadOptionalConfig`)만 없음으로 다룬다
        Invalid,    ///< 파일이 있는데 틀렸다. 키 이름과 함께 오류를 남겼다
    };

    /**
     * @brief 호스트 설정 하나를 어디서 읽는지입니다. `disk`(파일 경로)와 `baked`(configure 때 베이크한 JSON) 두 값뿐입니다.
     * @details 출처를 호출 서명에 둔다. "디스크에 없으면 베이크한 JSON" 같은 연결은 만들 수 없다 — 부르는 쪽(빌드 구성)이 하나를 고른다.
     */
    class ConfigSource
    {
    public:
        [[nodiscard]] static ConfigSource disk( string path ) { return ConfigSource( std::move( path ), nullptr ); }
        [[nodiscard]] static ConfigSource baked( const utf8* pJSON ) { return ConfigSource( string(), pJSON ); }

        [[nodiscard]] bool          isBaked() const { return _pBakedJSON != nullptr; }
        [[nodiscard]] const string& getPath() const { return _path; }
        [[nodiscard]] const utf8*   getBakedJSON() const { return _pBakedJSON; }

    private:
        ConfigSource( string path, const utf8* pBakedJSON )
            : _path{ std::move( path ) }
            , _pBakedJSON{ pBakedJSON }
        {
        }

        string      _path;
        const utf8* _pBakedJSON;
    };
} // namespace sw

namespace sw
{
    struct EngineConfig;
    struct GameConfig;

    /**
     * @class ConfigManager
     * @brief 호스트 설정(EngineConfig, GameConfig 등)의 테이블입니다. 파일에서 읽은 설정은 그 경로를 기억해 실행 중에 다시 읽을 수 있습니다
     *        (`reloadConfigFile`. 에디터의 설정 파일 감시가 부릅니다).
     * @details 호스트 설정 둘(`EngineConfig` · `GameConfig`)의 **소유자**는 이 표다. 서비스 표(`engine::getSubsystem<EngineConfig>()`)가 기동 초기화 전에 주소를
     *          묶어야 해서, 값이 읽히기 전부터 자리(`getHostEngineConfig` · `getHostGameConfig`)가 있고 읽으면 그 자리에 담긴다 — 다시 읽기도 제자리라
     *          주소는 이 표가 사는 동안 바뀌지 않는다. 정적 사본(`setActive`)을 따로 두지 않으므로 표가 둘이면 설정도 둘이다.
     */
    class SW_API ConfigManager : public ISubsystem
    {
    public:
        ConfigManager();
        ~ConfigManager() override;
        ConfigManager( const ConfigManager& )            = delete;
        ConfigManager& operator=( const ConfigManager& ) = delete;

        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Config";
        /**
         * @brief 호스트가 서비스 표에 걸 엔진 설정의 자리입니다(`engine::getSubsystem<EngineConfig>()` 가 이것을 돌려준다).
         * @details 값은 `initializeSubsystem` 이 읽은 뒤에야 쓸 수 있다 — 그 전에는 빈 형식(0)이다. 주소는 이 표가 사는 동안 같다.
         */
        [[nodiscard]] EngineConfig& getHostEngineConfig() const { return *_pHostEngineConfig; }
        /** @brief 호스트가 서비스 표에 걸 게임 프리셋의 자리입니다. 읽기 전에 쓰면 Debug 어서션(`DataPath` 가 비어 있다). 나머지는 `getHostEngineConfig` 와 같다. */
        [[nodiscard]] GameConfig& getHostGameConfig() const { return *_pHostGameConfig; }
        /**
         * @brief 기동 순서의 자리 — 프로젝트 루트를 기준으로 엔진 · 게임 설정(EngineConfig · GameConfig)을 읽어 호스트 자리에 둡니다.
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
            _mapSource[getConfigKey<T>()] = ReloadSource{ resolvedPath, &ConfigManager::reloadFromSource<T> };
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
         * @return 다시 읽었으면 `Reloaded`, 파일에서 읽은 설정이 아닌 경로면 `NotTracked`, 깨진 파일이면(오류를 남겼다) `Failed`
         */
        [[nodiscard]] ReloadOutcome reloadConfigFile( string_view changedPath );
        /** @brief 설정을 다시 읽을 때마다 호출되는 델리게이트입니다. */
        ConfigReloadedDelegate& onConfigReloaded() { return _onConfigReloaded; }
        /** @brief 다시 읽을 수 있는(파일에서 읽은) 설정 수입니다. */
        uint32 getReloadableCount() const { return static_cast<uint32>( _mapSource.size() ); }

        // 호스트가 기동 때 만든 테이블은 엔진 기동 범위의 서브시스템이다 — 엔진은 `engine::findSubsystem<ConfigManager>()`, 에디터는 모듈 호스트가 에디터 서비스 범위에
        // 건 것을 `editor::findSubsystem<ConfigManager>()` 로 찾는다(정적 창구를 두지 않는다). 서비스 목록에 올리지 않은 것은 게임 모듈이 볼 이유가 없기 때문이다.

        /**
         * @brief 설정 하나를 JSON 본문에서 엄격하게(`readConfigJSON`) 읽어 등록합니다. 틀리면 테이블을 바꾸지 않습니다.
         * @details 이미 같은 타입이 있으면(호스트 자리 포함) 그 객체에 값으로 덮어쓴다 — 서비스 표가 쥔 주소가 바뀌지 않는다.
         */
        template <typename T>
        [[nodiscard]] bool loadConfigFromJSON( const string& jsonStr, const utf8* pSourceLabel = "json" )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            unique_ptr<T> newConfig = make_unique<T>();
            if ( readConfigJSON( *newConfig, jsonStr, pSourceLabel ) == false )
                return false;
            const uint32 key  = getConfigKey<T>();
            auto         iter = _mapConfig.find( key );
            if ( iter != _mapConfig.end() )
            {
                *static_cast<T*>( iter->second.get() ) = std::move( *newConfig );
            }
            else if ( unique_ptr<IConfig>* pHostSlot = findHostSlot( *T::StaticType() ); pHostSlot != nullptr )
            {
                *static_cast<T*>( pHostSlot->get() ) = std::move( *newConfig ); // 서비스 표가 이 주소를 쥐고 있다 — 새 객체로 바꾸지 않고 자리에 담는다
                _mapConfig[key]                      = std::move( *pHostSlot );
            }
            else
            {
                _mapConfig[key] = std::move( newConfig );
            }
            SW_LOG_INFO( "Config loaded successfully from %#", pSourceLabel );
            return true;
        }

        /**
         * @brief 선택 설정 하나를 @p source 에서 읽어 등록합니다. 없는 것을 `Missing` 으로 돌려주고 로그는 남기지 않습니다.
         * @details `disk` 는 `loadConfigFile` 과 같다. `baked` 는 베이크한 JSON 을 읽으며 없음이 없다(읽으면 `Loaded`, 틀리면 `Invalid`).
         *          없어도 되는 설정만 쓴다 — 없을 때 무엇을 할지는 부르는 쪽이 정하되 `T{}` 를 대신 만들어 쓰지 않는다.
         */
        template <typename T>
        [[nodiscard]] ConfigReadResult loadOptionalConfig( const ConfigSource& source )
        {
            if ( source.isBaked() == false )
                return loadConfigFile<T>( source.getPath() );
            return loadConfigFromJSON<T>( string( source.getBakedJSON() ), "baked_config" ) ? ConfigReadResult::Loaded : ConfigReadResult::Invalid;
        }

        /**
         * @brief 필수 호스트 설정 하나를 @p source 에서 읽어 등록하고 포인터를 반환합니다. 없거나 틀리면 nullptr 이고, 부르는 쪽이 기동을 멈춥니다.
         * @details 파일이 없는 것도 오류다(경로와 함께 남긴다). 베이크한 JSON · `T{}` 로 조용히 대신하지 않는다 — 고친 값이 무시되거나 틀린 상태로 서는 것을 아무도 모른다.
         *          틀린 파일(모르는 키, 읽지 못한 값, 범위 밖)의 오류는 키 이름과 함께 이미 남았다. 출처는 빌드 구성이 고른다(Shipping 은 `baked`, 그 밖은 `disk`).
         */
        template <typename T>
        [[nodiscard]] T* loadHostConfig( const ConfigSource& source )
        {
            static_assert( std::is_base_of_v<IConfig, T>, "T must inherit from IConfig" );

            const utf8* const      pTypeName = T::StaticType()->_fullyQualifiedName.c_str();
            const ConfigReadResult result    = loadOptionalConfig<T>( source );
            if ( result == ConfigReadResult::Missing )
            {
                SW_LOG_ERROR( "Required config %# was not found at '%#' (cwd, project root, or exe directory)", pTypeName, source.getPath().c_str() );
                return nullptr;
            }
            if ( result == ConfigReadResult::Invalid )
                return nullptr;
            SW_LOG_TRACE( "%# source=%#", pTypeName, source.isBaked() ? "baked" : source.getPath().c_str() );
            return getConfig<T>();
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
         * @brief 설정 하나를 파일에서 읽어 @p outConfig 에 담습니다. **테이블에 등록하지 않습니다.**
         * @details 모듈이 정의한 설정 타입은 이쪽으로 읽어 그 모듈의 객체(서비스 · 키트)가 소유한다. 이 매니저는 엔진 서비스라 모듈보다 오래 사는데, 모듈이 내려간 뒤 그 타입의
         *          객체를 소멸시키면 가상 소멸자가 사라진 이미지로 들어가 죽는다. 등록하지 않으면 그럴 객체도 `_mapSource` 의 다시 읽는 함수(모듈 코드)도 남지 않는다.
         * @return 없으면 `Missing`(로그 없음), 틀렸으면 `Invalid`(키 이름과 함께 오류를 남겼다). `loadConfigFile` 과 같은 경로 규칙이다.
         */
        template <typename T>
        [[nodiscard]] ConfigReadResult readConfigFileInto( const string& filePath, T& outConfig ) const
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
            if ( readConfigJSON( outConfig, jsonStr, resolvedPath.c_str() ) == false )
                return ConfigReadResult::Invalid;
            SW_LOG_INFO( "Config loaded successfully from %#", resolvedPath.c_str() );
            return ConfigReadResult::Loaded;
        }

        /**
         * @brief 설정 JSON 하나를 @p pInstance 에 엄격하게 읽습니다.
         * @details 모르는 키, 대소문자만 다른 키, 읽지 못한 값, 빠진 필수 칸(`PROPERTY( Required )` — 구조체 칸 · 구조체 목록 원소 안까지), `PROPERTY( Min/Max )` 밖 숫자를 **모두** 키 이름과 @p pSourceLabel 로 오류 로그에 남기고
         *          false 를 반환한다(첫 오류에서 멈추지 않는다. 한 번에 다 고치게 하려는 것이다). 설정이 아닌 리플렉션 타입(`EditorToolDefaults`)도 이 함수로 읽는다.
         *          구조체 필드 안의 모르는 키는 바깥 필드 이름과 본문으로 표시된다.
         */
        [[nodiscard]] static bool readConfigJSON( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr, const utf8* pSourceLabel );
        /**
         * @brief 설정 XML 요소 하나(`<Engine _windowTitle="…"/>` — 속성과 자식이 칸)를 @p pInstance 에 엄격하게 읽습니다.
         * @details 모르는 속성 · 자식, 읽지 못한 값, 빠진 필수 칸(`PROPERTY( Required )`, 맨 위 칸만), 범위 밖 숫자를 **모두** 오류 로그에 남기고 false 를 반환한다.
         *          요소의 이름은 보지 않는다 — 한 파일 안의 요소 하나를 소유하는 설정(`PackEngineSettings`)과 프로젝트 설정 창이 같은 규칙으로 읽는다.
         */
        [[nodiscard]] static bool readConfigXMLElement( void* pInstance, const TypeInfo& typeInfo, string_view elementXML, const utf8* pSourceLabel );

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
        struct ReloadSource
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
        [[nodiscard]] bool resolveConfigPath( const string& filePath, string& outResolvedPath ) const
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

        /**
         * @brief 아직 표에 올리지 않은 호스트 설정(`EngineConfig` · `GameConfig`)의 자리입니다. 그 타입이 아니면 nullptr 입니다.
         * @details 첫 읽기가 끝나면 자리의 객체가 표(`_mapConfig`)로 옮겨가 자리는 비지만, 객체의 주소는 그대로다.
         */
        unique_ptr<IConfig>* findHostSlot( const TypeInfo& type );

        unordered_map<uint32, unique_ptr<IConfig>> _mapConfig;
        unique_ptr<IConfig>                        _pEngineSlot;       ///< 호스트 엔진 설정의 객체 - 읽기 전까지 여기 있고 읽은 뒤 `_mapConfig` 가 쥔다
        unique_ptr<IConfig>                        _pGameSlot;         ///< 호스트 게임 프리셋의 객체 - `_pEngineSlot` 과 같다
        EngineConfig*                              _pHostEngineConfig; ///< `_pEngineSlot` 의 객체 주소 - 표를 옮겨도 같다
        GameConfig*                                _pHostGameConfig;   ///< `_pGameSlot` 의 객체 주소
        unordered_map<uint32, ReloadSource>        _mapSource;         ///< 파일에서 읽은 설정에서 경로와 다시 읽는 함수로 가는 맵이다
        ConfigReloadedDelegate                     _onConfigReloaded;
        /// @brief 상대 Config 경로의 기준 디렉터리입니다. 비어 있으면 작업 디렉터리와 실행 파일 위치만 봅니다.
        string _rootDirectory;
    };

} // namespace sw
