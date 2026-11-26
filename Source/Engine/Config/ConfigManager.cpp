#include "pch.h"

#include "Engine/Config/ConfigManager.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Config/ConfigOverride.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/Serializer/SchemaMigrate.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingGameDefaults.h"
#include "sw/config/ShippingHostDefaults.h"

namespace sw
{
    namespace
    {
        /** @brief @p fileObject 의 키마다 두 쪽 값이 같은 JSON 이면 경로를 담습니다. 파일 쪽이 객체면 안으로 들어간다(구조체 칸). */
        void collectEqualMembers( const JSONValue& fileObject, const JSONValue& left, const JSONValue& right, const string& prefix,
                                  vector<string>& outListKey )
        {
            for ( const string& key : fileObject.getMemberNames() )
            {
                StringBuilder<constant::buffer::kMax256> pathBuilder;
                if ( prefix.empty() == false )
                    pathBuilder.appendFormat( "%#.", prefix );
                pathBuilder.append( key );
                const string    path( pathBuilder.view() );
                const JSONValue fileValue  = fileObject.get( key );
                const JSONValue leftValue  = left.get( key );
                const JSONValue rightValue = right.get( key );
                if ( leftValue.isValid() == false || rightValue.isValid() == false )
                    continue;
                const bool bRecurseIntoMembers = fileValue.isObject() && leftValue.isObject() && rightValue.isObject() && fileValue.getMemberNames().empty() == false;
                if ( bRecurseIntoMembers )
                {
                    collectEqualMembers( fileValue, leftValue, rightValue, path, outListKey );
                    continue;
                }
                // 글로 못 쓰는 값(잘못된 UTF-8)은 같다고 보지 않는다 - 같은 키로 모이면 파일에서 지워진다.
                const expected<string> leftText  = leftValue.dump();
                const expected<string> rightText = rightValue.dump();
                if ( leftText.has_value() && rightText.has_value() && *leftText == *rightText )
                    outListKey.push_back( path );
            }
        }
    } // namespace
} // namespace sw

// 언리얼 스케일러빌리티 그룹(`sg.*`) · 유니티 품질 단계 자리. 기본 단계 high 는 바탕 값 그대로다. 사용자 옵션 `graphics.renderQuality` 가 값을 넣고(명령줄이 이긴다),
// 바뀌면 엔진 설정을 제자리에서 다시 읽는다(`ConfigManager::applyRenderQuality`).
SW_GLOBAL_VARIABLE( sw::GlobalString, gv_renderQuality, "high",
                    "렌더 품질 단계 — Config/Engine/RenderQuality.json 의 단계 이름 low · medium · high · ultra (사용자 설정 graphics.renderQuality, 엔진 설정 _render 위에 합친다, 빈 글이면 단계 없음)" );

namespace sw
{
    SubsystemInitResult ConfigManager::initializeSubsystem()
    {
        // 읽은 값은 호스트 자리(`getHostEngineConfig` · `getHostGameConfig`)에 담긴다 - 서비스 표가 그 주소를 이미 쥐고 있다.
        // 정적 구성(배포 · 웹 · Android 개발 — 기기에 프로젝트 폴더가 없다)은 디스크의 `Config/` 를 보지 않고 베이크한 JSON(엔진 설정 · 베이크한 게임의 프리셋)만 읽는다.
        // 그 밖의 설정은 모든 구성에서 디스크 파일이 필수다.
        if constexpr ( build::kStaticModules )
        {
            EngineConfig probe;
            if ( JSONSerializer::deserialize( &probe, *EngineConfig::StaticType(), shipping_host::kEngineConfigJSON ) == false )
            {
                SW_LOG_ERROR_ID( "ConfigManager.GeneratedEngineConfigJSONDoes", "The generated EngineConfig JSON does not deserialize - a reflection registrar was dropped at link time" );
                return SubsystemInitResult::Failed;
            }
        }
        // 플랫폼 덮어쓰기 층(`PlatformConfigOverride` — 언리얼 플랫폼 ini 자리)이 엔진 설정 위에 선다. 디스크 구성은 이 플랫폼의 파일(없어도 된다), 정적 구성은 구운 글.
        // 그 위에 고른 품질 단계(`RenderQualityOverride`, `gv_renderQuality`)가 선다 — 디스크 구성은 단계 표 파일, 정적 구성은 구운 표.
        const string       qualityTier  = gv_renderQuality.get();
        const ConfigSource engineSource = build::kStaticModules
                                            ? ConfigSource::baked( shipping_host::kEngineConfigJSON, shipping_host::kEngineConfigPlatformOverrideJSON )
                                                  .withRenderQuality( shipping_host::kRenderQualityJSON, qualityTier )
                                            : ConfigSource::disk( config::kFileRuntimeEngineConfig, PlatformConfigOverride::makeEngineConfigPath() )
                                                  .withRenderQuality( config::kFileRuntimeRenderQuality, qualityTier );
        if ( loadEngineConfig( engineSource ) == nullptr )
            return SubsystemInitResult::Failed;
        // 사용자 설정이 기동 때 저장값을 넣거나 메뉴 · 콘솔이 바꾸면 그 자리에서 다시 읽는다.
        GlobalVariableInfo* const pRenderQualityVariable = engine::getSubsystem<GlobalVariableManager>().findVariable( "gv_renderQuality" );
        if ( pRenderQualityVariable == nullptr )
        {
            SW_LOG_ERROR_ID( "ConfigManager.RenderQualityVariableMissing", "gv_renderQuality is not registered - the render quality option cannot reach the engine config" );
            return SubsystemInitResult::Failed;
        }
        _renderQualitySubscription    = pRenderQualityVariable->bindOnValueChanged( SW_DELEGATE_METHOD( GlobalVariableChangedDelegate, &ConfigManager::onRenderQualityChanged, this ) );
        const ConfigSource gameSource = build::kStaticModules ? ConfigSource::baked( shipping_host::kGameConfigJSON ) : ConfigSource::disk( GameSelection::getConfigPath() );
        if ( loadHostConfig<GameConfig>( gameSource ) == nullptr )
            return SubsystemInitResult::Failed;
        return SubsystemInitResult::Succeeded;
    }

    void ConfigManager::shutdownSubsystem()
    {
        _renderQualitySubscription.reset();
    }

    EngineConfig* ConfigManager::loadEngineConfig( const ConfigSource& source )
    {
        EngineConfig* const pConfig = loadHostConfig<EngineConfig>( source );
        if ( pConfig != nullptr )
            _pEngineSource = make_unique<ConfigSource>( source );
        return pConfig;
    }

    bool ConfigManager::applyRenderQuality( const string& tierName )
    {
        if ( _pEngineSource == nullptr )
        {
            SW_LOG_ERROR_ID( "ConfigManager.RenderQualityBeforeLoad", "Render quality '%#' was chosen before the engine config was loaded", tierName.c_str() );
            return false;
        }
        if ( _pEngineSource->getQualityTier() == tierName )
            return true;
        const ConfigSource source = _pEngineSource->withRenderQuality( _pEngineSource->getQualityTiers(), tierName );
        if ( loadHostConfig<EngineConfig>( source ) == nullptr )
            return false; // 까닭은 읽기가 남겼다 — 표는 이전 값 그대로다
        *_pEngineSource = source;
        _onConfigReloaded.broadcast( EngineConfig::StaticType()->_fullyQualifiedName );
        return true;
    }

    void ConfigManager::onRenderQualityChanged( GlobalVariableInfo* pInfo )
    {
        (void)pInfo;
        (void)applyRenderQuality( gv_renderQuality.get() );
    }

    expected<string> ConfigManager::readProjectText( const utf8* pPath )
    {
        string text;
        switch ( FileUtil::readOptionalTextFile( ProcessRoots::get()._project.join( pPath ), text ) )
        {
            case OptionalFileRead::Read:
                return text;
            case OptionalFileRead::Missing:
                return unexpected( Error{ ErrorKind::NotFound, "the config file is missing", pPath } );
            case OptionalFileRead::Failed:
                break;
        }
        return unexpected( Error{ ErrorKind::ExternalFailure, "the config file could not be read", pPath } );
    }

    bool ConfigManager::readConfigJSON( void* pInstance, const TypeInfo& typeInfo, string_view jsonStr, const utf8* pSourceLabel )
    {
        // 키는 대소문자까지 맞아야 한다(직렬화기는 키를 철자 그대로 찾는다 — `_Width` 오타는 모르는 키다).
        SerializeContext context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
        // JSON 객체가 아닌 본문(`SoftReadResult::NotParsed`)만 여기서 멈춘다. 범위 밖 값(`Rejected`)에서 바로 돌아가면
        // 그 값 하나가 모르는 키 · 빠진 필수 칸의 오류를 삼킨다 — 모두 알린다는 계약(헤더)이 깨진다.
        JSONDocument document;
        if ( document.parse( jsonStr ) == false || document.getRoot().isObject() == false )
        {
            SW_LOG_ERROR_ID( "Config.NotJSONObject", "Config %#: not a JSON object for %#", pSourceLabel, typeInfo._fullyQualifiedName.c_str() );
            return false;
        }
        vector<SchemaOrphanValue> listOrphan;
        // 본문은 위에서 객체로 읽혔으므로 `Read` 가 아니면 범위 밖 값이다 — 읽기 공통 끝(`SerializerUtil::isWithinDeclaredRanges`)이 칸마다 이미 알렸다.
        const bool bWithinRange = JSONSerializer::deserializeSoft( pInstance, typeInfo, jsonStr, &listOrphan, nullptr, context ) == SoftReadResult::Read;
        for ( const SchemaOrphanValue& orphan : listOrphan )
        {
            const utf8* const pName = orphan.getDisplayName();
            SW_LOG_ERROR_ID( "Config.KeyUnknown", "Config %#: key '%#' is unknown to %# or its value %# cannot be read", pSourceLabel, pName, typeInfo._fullyQualifiedName.c_str(),
                             orphan._text.c_str() );
        }
        // 필수 칸(`PROPERTY( Required )`)이 빠졌으면 초기값으로 읽지 않고 오류다 — 초기값은 형식이지 기본값이 아니다.
        vector<string> listMissing;
        JSONSerializer::collectMissingRequired( document.getRoot(), typeInfo, listMissing );
        for ( const string& missingPath : listMissing )
        {
            SW_LOG_ERROR_ID( "Config.KeyMissing", "Config %#: required key '%#' of %# is missing", pSourceLabel, missingPath.c_str(), typeInfo._fullyQualifiedName.c_str() );
        }
        return bWithinRange && listOrphan.empty() && listMissing.empty();
    }

    bool ConfigManager::readConfigXMLDocument( void* pInstance, const TypeInfo& typeInfo, string_view documentXML, const utf8* pSourceLabel )
    {
        XMLDocument document;
        if ( document.parse( documentXML, pSourceLabel ) == false || document.getRoot().isValid() == false )
        {
            SW_LOG_ERROR_ID( "Config.NotXMLElement", "Config %#: not an XML element for %#", pSourceLabel, typeInfo._fullyQualifiedName.c_str() );
            return false;
        }
        const XMLNode root = XMLSerializer::findDocumentRoot( document, typeInfo );
        return root.isValid() && readConfigXMLElement( pInstance, typeInfo, root, pSourceLabel );
    }

    bool ConfigManager::readComposedXMLDocument( void* pInstance, const TypeInfo& typeInfo, string_view documentXML, string_view sourceLabel, const ResourceContext& context )
    {
        string flatText;
        bool   bComposed = false; // 펼칠 것이 없으면 flatText 는 빈 채로 남고 글을 그대로 읽는다
        if ( ResourceComposer::compose( documentXML, sourceLabel, context, flatText, bComposed ) == false )
            return false;
        const string label{ sourceLabel };
        return readConfigXMLDocument( pInstance, typeInfo, bComposed ? string_view{ flatText } : documentXML, label.c_str() );
    }

    bool ConfigManager::readConfigXMLElement( void* pInstance, const TypeInfo& typeInfo, const XMLNode& element, const utf8* pSourceLabel )
    {
        // 키는 대소문자까지 맞아야 한다(`readConfigJSON` 과 같다). 범위 밖 값은 칸마다 읽기 공통 끝이 이미 알렸다.
        SerializeContext          context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
        vector<SchemaOrphanValue> listOrphan;
        const bool                bWithinRange = XMLSerializer::deserializeElementSoft( pInstance, typeInfo, element, &listOrphan, nullptr, context ) == SoftReadResult::Read;
        for ( const SchemaOrphanValue& orphan : listOrphan )
        {
            const utf8* const pName = orphan.getDisplayName();
            SW_LOG_ERROR_ID( "Config.KeyUnknown", "Config %#: '%#' is unknown to %# or its value %# cannot be read", pSourceLabel, pName, typeInfo._fullyQualifiedName.c_str(), orphan._text.c_str() );
        }
        // 필수 칸은 속성이나 자식으로 적혀 있어야 한다(구조체 칸 · 목록 원소 안까지) — 초기값은 형식이지 기본값이 아니다.
        vector<string> listMissing;
        XMLSerializer::collectMissingRequired( element, typeInfo, listMissing );
        for ( const string& missingPath : listMissing )
        {
            SW_LOG_ERROR_ID( "Config.KeyMissing", "Config %#: required '%#' of %# is missing", pSourceLabel, missingPath.c_str(), typeInfo._fullyQualifiedName.c_str() );
        }
        return bWithinRange && listOrphan.empty() && listMissing.empty();
    }

    bool ConfigManager::collectEqualKeys( const void* pLeft, const void* pRight, const TypeInfo& typeInfo, string_view jsonStr, vector<string>& outListKey )
    {
        JSONDocument           fileDoc;
        JSONDocument           leftDoc;
        JSONDocument           rightDoc;
        const expected<string> leftText     = JSONSerializer::serialize( pLeft, typeInfo );
        const expected<string> rightText    = JSONSerializer::serialize( pRight, typeInfo );
        const bool             bParseFailed = leftText.has_value() == false || rightText.has_value() == false || fileDoc.parse( jsonStr ) == false ||
                                  leftDoc.parse( *leftText ) == false || rightDoc.parse( *rightText ) == false;
        if ( bParseFailed )
            return false;
        collectEqualMembers( fileDoc.getRoot(), leftDoc.getRoot(), rightDoc.getRoot(), string(), outListKey );
        return true;
    }

    ConfigManager::ConfigManager()
        : _pEngineSlot{ make_unique<EngineConfig>() }
        , _pGameSlot{ make_unique<GameConfig>() }
        , _pHostEngineConfig{ static_cast<EngineConfig*>( _pEngineSlot.get() ) }
        , _pHostGameConfig{ static_cast<GameConfig*>( _pGameSlot.get() ) }
    {
    }

    ConfigManager::~ConfigManager() = default;

    unique_ptr<IConfig>* ConfigManager::findHostSlot( const TypeInfo& type )
    {
        if ( &type == EngineConfig::StaticType() )
            return &_pEngineSlot;
        if ( &type == GameConfig::StaticType() )
            return &_pGameSlot;
        return nullptr;
    }

    ReloadResult ConfigManager::reloadConfigFile( string_view changedPath )
    {
        for ( const auto& [key, source] : _mapSource )
        {
            (void)key;
            const bool bOverrideChanged = ( source._resolvedOverridePath.empty() == false && FileUtil::isSameDiskPath( source._resolvedOverridePath.str(), changedPath ) ) ||
                                          ( source._resolvedQualityPath.empty() == false && FileUtil::isSameDiskPath( source._resolvedQualityPath.str(), changedPath ) );
            if ( source._pReload != nullptr && ( FileUtil::isSameDiskPath( source._resolvedPath.str(), changedPath ) || bOverrideChanged ) )
                return source._pReload( *this, source ) ? ReloadResult::Reloaded : ReloadResult::Failed;
        }
        return ReloadResult::NotTracked;
    }

    OptionalFileRead ConfigManager::readLayeredFile( const AbsolutePath& resolvedPath, const AbsolutePath& resolvedOverridePath, const AbsolutePath& resolvedQualityPath,
                                                     const string& qualityTier, string& outJSON, string& outLabel )
    {
        outLabel                    = resolvedPath.str();
        const OptionalFileRead read = FileUtil::readOptionalTextFile( resolvedPath, outJSON );
        if ( read != OptionalFileRead::Read )
            return read;
        if ( resolvedOverridePath.empty() == false )
        {
            string                 overrideJSON;
            const OptionalFileRead overrideRead = FileUtil::readOptionalTextFile( resolvedOverridePath, overrideJSON );
            if ( overrideRead == OptionalFileRead::Failed )
                return OptionalFileRead::Failed;          // 이유는 읽기가 오류로 남겼다
            if ( overrideRead == OptionalFileRead::Read ) // 덮어쓰기 층은 선택이다 — 없으면 바탕만
            {
                const string base = std::move( outJSON );
                if ( applyOverride( base, overrideJSON.c_str(), resolvedOverridePath.c_str(), outJSON ) == false )
                    return OptionalFileRead::Failed;
                // 합친 글의 모르는 키 · 범위 밖 값은 어느 층에서 왔는지 모른다 — 출처에 층을 모두 적는다.
                outLabel += " + ";
                outLabel += resolvedOverridePath.str();
            }
        }
        if ( qualityTier.empty() )
            return OptionalFileRead::Read;
        // 단계를 골랐으면 표는 필수다 — 없는 표를 "단계 없음" 으로 읽으면 고른 단계가 조용히 무시된다.
        string                 tiersJSON;
        const OptionalFileRead tiersRead = FileUtil::readOptionalTextFile( resolvedQualityPath, tiersJSON );
        if ( tiersRead == OptionalFileRead::Missing )
            SW_LOG_ERROR_ID( "Config.RenderQualityMissing", "Render quality tier '%#' was chosen but the tier table %# is missing", qualityTier.c_str(), resolvedQualityPath.c_str() );
        if ( tiersRead != OptionalFileRead::Read )
            return OptionalFileRead::Failed;
        StringBuilder<constant::buffer::kMax512> qualityLabel;
        qualityLabel.appendFormat( "%#[%#]", resolvedQualityPath.str(), qualityTier );
        const string base = std::move( outJSON );
        if ( applyQualityTier( base, tiersJSON, qualityTier, qualityLabel.c_str(), outJSON ) == false )
            return OptionalFileRead::Failed;
        outLabel += " + ";
        outLabel += qualityLabel.view();
        return OptionalFileRead::Read;
    }

    bool ConfigManager::applyOverride( const string& baseJSON, const utf8* pOverrideJSON, const utf8* pOverrideLabel, string& outJSON )
    {
        return PlatformConfigOverride::apply( baseJSON, pOverrideJSON, pOverrideLabel, outJSON );
    }

    bool ConfigManager::applyQualityTier( const string& baseJSON, const string& tiersJSON, const string& qualityTier, const utf8* pLabel, string& outJSON )
    {
        return RenderQualityOverride::apply( baseJSON, tiersJSON, qualityTier, pLabel, outJSON );
    }
} // namespace sw

namespace sw
{
    // 설정 표는 Config 서브시스템이다 — 엔진 · 게임 설정을 읽어 자기 자리에 둔다(Resource 가 그 뒤에 서서 팩 루트를 안다).
    SW_ENGINE_SUBSYSTEM( ConfigManager, Process, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        ConfigManager& configManager = scope.emplaceSubsystem<ConfigManager>();
        (void)build;
        // 호스트 설정 둘은 Config 가 소유한다 - 읽는 것은 Config 의 초기화이고, 서브시스템 칸(`engine::getSubsystem<EngineConfig>()`)은 이 자리의 주소를 묶는다.
        (void)scope.exposeMember( configManager, configManager.getHostEngineConfig() );                     // 새 범위 - 겹치지 않는다
        (void)scope.exposeMember( configManager, configManager.getHostGameConfig() );                       // 새 범위 - 겹치지 않는다
        (void)scope.exposeMember( configManager, configManager.getHostEngineConfig()._characterMeshCache ); // 게임에 보이는 조각 - 엔진 설정 안의 자리라 다시 읽어도 주소가 같다
        (void)scope.exposeMember( configManager, configManager.getHostEngineConfig()._minigame );           // 게임에 보이는 조각 - 같은 자리
    } );

    // 위 공장이 건 호스트 설정 둘의 서브시스템 칸(읽기 전용). 정적 활성본(`EngineConfig::getActive()` 따위)을 대신한다 — 다시 읽기는 같은 자리에서 일어나 칸의 주소가 바뀌지 않는다.
    // 엔진 설정은 엔진과 에디터가 읽고, 게임은 게임 설정만 본다.
    SW_ENGINE_SUBSYSTEM_SLOT( EngineConfig, HostOnly );
    SW_ENGINE_SUBSYSTEM_SLOT( GameConfig, GameVisible );
    SW_ENGINE_SUBSYSTEM_SLOT( CharacterMeshCacheConfig, GameVisible ); // 게임 모듈의 캐릭터 메시 캐시가 예산을 읽는다(엔진 설정 전체는 호스트 전용)
    SW_ENGINE_SUBSYSTEM_SLOT( MinigameConfig, GameVisible );           // 게임 모듈의 미니게임 흐름이 쌓기 깊이 한도를 읽는다
} // namespace sw
