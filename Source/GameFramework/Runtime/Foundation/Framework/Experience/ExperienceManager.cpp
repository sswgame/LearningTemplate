#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceManager.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceKitRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceScope.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

namespace sw
{
    /**
     * @brief `"-gv_firstExperience=<이름>"` 인자입니다. 첫 씬(`gv_firstScene` > 타이틀 > 시작 맵) 대신 이 Experience(`<팩>/experiences/<이름>.experience.xml`)로 들어갑니다.
     * @details 언리얼 Lyra 의 URL 옵션 `?Experience=` 자리다. 전역 변수라 시나리오 `<Launch>` 가 그대로 쓰고 체인 단계마다 되돌려진다(따로 둔 실행 인자는 단계마다 바꿀 길이 없었다).
     *          `gv_firstScene` 처럼 배포에도 있다 — 배포본의 시나리오 · 전용 서버도 미니게임으로 바로 들어간다.
     */
    SW_GLOBAL_VARIABLE( sw::GlobalString, gv_firstExperience, "", "첫 씬 대신 처음 들어갈 Experience 이름 (비우면 사용 안 함)" );

    namespace
    {
        struct ExperienceManagerInternal
        {
            /** @brief `experience.enter <이름>` 의 몸통입니다. 맡기기만 하고 켜졌는지는 로그 · 감시 값으로 본다(맵 로드를 기다린다). */
            static bool runEnterCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                ExperienceManager* const                 pManager = game::findSubsystem<ExperienceManager>();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( pManager == nullptr )
                    replyBuilder.append( "experience.enter: no game is running" );
                else
                    replyBuilder.appendFormat( "experience.enter: %# '%#'", pManager->enter( listArgument[0] ) ? "entering" : "refused", listArgument[0].c_str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief 감시 값 `Experience.Active` 입니다. 켜진 Experience 이름이고, 없으면 `none` 입니다(맵을 기다리는 중도 `none`). */
            [[nodiscard]] static bool readActive( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const ExperienceManager* const pManager = game::findSubsystem<ExperienceManager>();
                if ( pManager == nullptr )
                {
                    outValue.setText( "no game is running" );
                    return false;
                }
                const ExperienceDefinition* const pActive = pManager->isEntering() ? nullptr : pManager->findActiveDefinition();
                outValue.setText( pActive != nullptr ? pActive->_name.c_str() : "none" );
                return true;
            }
        };
    } // namespace

    ExperienceManager::ExperienceManager( SubsystemScope& gameScope, const GamePackConfig& packConfig )
        : _gameScope( gameScope )
        , _packConfig( packConfig )
        , _pActive{ nullptr }
        , _bEntering{ SW_FALSE }
    {
    }

    ExperienceManager::~ExperienceManager() = default;

    void ExperienceManager::shutdownSubsystem()
    {
        leave();
    }

    bool ExperienceManager::enterFirstExperience()
    {
        const string name = gv_firstExperience.get();
        if ( name.empty() )
            return false;
        SW_LOG_INFO_ID( "ExperienceManager.FirstExperience", "First experience %# (gv_firstExperience)", name.c_str() );
        (void)enter( name ); // 실패는 위에서 알렸다 — 다른 씬으로 대신하지 않는다
        return true;
    }

    string ExperienceManager::makeAssetPath( const GamePackConfig& packConfig, string_view name )
    {
        return packConfig.resolve( string( kExperienceFolder ) + string( name ) + kExperienceSuffix );
    }

    bool ExperienceManager::enter( string_view name )
    {
        if ( isEntering() )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.EnterBusy", "Experience '%#' is refused - experience '%#' is still waiting for its map", name, _pActive->getDefinition()._name.c_str() );
            return false;
        }
        const string         assetPath = makeAssetPath( _packConfig, name );
        ExperienceDefinition definition;
        if ( ExperienceDefinition::loadFromAsset( AssetID{ assetPath }, definition ) == false )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.NotLoaded", "Experience '%#' (%#) could not be read (see the messages above)", name, assetPath.c_str() );
            return false;
        }
        if ( definition._name != hashed_string( name ) )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.NameMismatch", "%#: name=\"%#\" differs from the file name '%#'", assetPath.c_str(), definition._name.c_str(), name );
            return false;
        }
        // 등록부는 GameFramework 의 모듈 서브시스템이다 — 없으면 호스트가 모듈을 올리지 않은 것이라 멈춘다.
        if ( definition.validate( game::getSubsystem<ExperienceKitRegistry>(), game::getSubsystem<TypeRegistry>(), assetPath ) == false )
            return false;
        if ( definition._entry == ExperienceEntry::SubLevel )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.SubLevelUnsupported", "%#: entry=\"SubLevel\" needs the hub sub-level loader (hub migration step 2) - use SceneSwitch until then", assetPath.c_str() );
            return false;
        }
        // 앞 것을 끄고 새 것을 켠 뒤 맵을 요청한다 — 맵의 컴포넌트가 씬 적용(`beginPlay`) 때 키트 서비스 · 카탈로그를 찾는다. 앞 맵의 컴포넌트가 아직 산 동안 칸이 비어도
        // 두 Experience 가 섞이지는 않는다(같은 서비스를 둘이 걸면 `ExperienceScope::bindService` 가 거절한다).
        leave();
        if ( activate( definition ) == false )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.NotActivated", "Experience '%#' could not be activated (see the messages above) - nothing of it stays bound", name );
            return false;
        }
        const string mapPath = definition._map.get();
        if ( game::getSubsystem<GameSceneFlow>().requestSceneThen( mapPath, GameSceneFlow::SceneLoadedFunction::create<&ExperienceManager::onMapLoaded>( this ) ) == false )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.MapRequestRefused", "Experience '%#': the map '%#' could not be requested", name, mapPath.c_str() );
            leave();
            return false;
        }
        _bEntering = SW_TRUE;
        return true;
    }

    void ExperienceManager::leave()
    {
        _bEntering = SW_FALSE; // 기다리던 맵의 완료 알림이 와도 할 일이 없다
        if ( _pActive == nullptr )
            return;
        const uint32 leakCount = _pActive->deactivate(); // 범위가 건 역순으로 거두고 남은 것을 알린다
        SW_LOG_INFO_ID( "ExperienceManager.Left", "Experience '%#' is deactivated (%# left behind)", _pActive->getDefinition()._name.c_str(), leakCount );
        _pActive.reset();
    }

    const ExperienceDefinition* ExperienceManager::findActiveDefinition() const
    {
        return _pActive != nullptr ? &_pActive->getDefinition() : nullptr;
    }

    void ExperienceManager::onMapLoaded( Scene* pScene )
    {
        if ( isEntering() == false || _pActive == nullptr )
            return;
        _bEntering = SW_FALSE;
        if ( pScene != nullptr )
            return;
        const ExperienceDefinition& definition = _pActive->getDefinition();
        SW_LOG_ERROR_ID( "ExperienceManager.MapNotOpened", "Experience '%#': the map '%#' did not open (failed or replaced by a later request) - the experience is deactivated", definition._name.c_str(),
                         definition._map.c_str() );
        leave();
    }

    bool ExperienceManager::activate( const ExperienceDefinition& definition )
    {
        InputManager* const          pInput = game::findSubsystem<InputManager>(); // 입력 서비스가 없는 호스트(전용 서버)는 입력 레이어를 걸지 않는다
        unique_ptr<ExperienceScope>  scope  = make_unique<ExperienceScope>( _gameScope, game::getSubsystem<GlobalVariableManager>(), _packConfig, pInput != nullptr ? &pInput->getInputMap() : nullptr,
                                                                            definition );
        const ExperienceKitRegistry& kits   = game::getSubsystem<ExperienceKitRegistry>();
        for ( const hashed_string& kitName : definition._listKit )
        {
            const ExperienceKitRegistration* const pKit = kits.findKit( kitName.view() ); // 검사 뒤에 키트 모듈이 내려갔을 수 있다 — 다시 찾는다
            if ( pKit == nullptr )
            {
                SW_LOG_ERROR_ID( "ExperienceManager.KitGone", "Experience '%#': kit '%#' is no longer registered", definition._name.c_str(), kitName.c_str() );
                return false;
            }
            if ( pKit->_pActivate( *scope ) == false )
            {
                SW_LOG_ERROR_ID( "ExperienceManager.KitNotActivated", "Experience '%#': kit '%#' could not be activated (see the messages above)", definition._name.c_str(), kitName.c_str() );
                return false;
            }
        }
        // 적은 카탈로그를 아무 키트도 읽지 않았으면 데이터와 키트가 어긋났다 — 적지 않은 것을 읽는 쪽은 `ExperienceScope::loadCatalog` 가 막았다.
        bool bCatalogsLoaded = true;
        for ( const hashed_string& catalog : definition._listCatalog )
        {
            if ( VectorUtil::contains( scope->getLoadedCatalogs(), catalog ) )
                continue;
            SW_LOG_ERROR_ID( "ExperienceManager.CatalogNotLoaded", "Experience '%#': catalog '%#' is declared but no kit in kits=\"...\" loaded it", definition._name.c_str(), catalog.c_str() );
            bCatalogsLoaded = false;
        }
        if ( bCatalogsLoaded == false || pushInputLayers( *scope ) == false )
            return false;
        // UI 는 클라이언트 전용 부분이 연다 — 전용 서버(창구가 없다)는 화면이 없다.
        IGameInstanceClientHook* const pClientHook = IGameInstanceClientHook::find();
        if ( pClientHook != nullptr && pClientHook->openExperienceUI( *scope ) == false )
            return false;
        scope->finishActivation();
        _pActive = std::move( scope );
        SW_LOG_INFO_ID( "ExperienceManager.Activated", "Experience '%#' is active (%# kits)", definition._name.c_str(), static_cast<uint32>( definition._listKit.size() ) );
        return true;
    }

    bool ExperienceManager::pushInputLayers( ExperienceScope& scope )
    {
        const ExperienceDefinition& definition = scope.getDefinition();
        if ( definition._listInputContext.empty() )
            return true;
        InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.InputContextNeedsInput", "Experience '%#' lists input contexts but the engine service InputManager is not bound to the game services",
                             definition._name.c_str() );
            return false;
        }
        InputMap& inputMap = pInput->getInputMap();
        for ( const hashed_string& layer : definition._listInputContext )
        {
            if ( inputMap.hasLayer( layer ) == false )
            {
                SW_LOG_ERROR_ID( "ExperienceManager.InputContextMissing", "Experience '%#': input context '%#' is not a layer of the game input map bundle", definition._name.c_str(), layer.c_str() );
                return false;
            }
            inputMap.pushLayer( layer );
            // 같은 입력 맵을 끌 때 찾는다 — 입력 서비스는 엔진 수명이라 Experience 보다 오래 산다.
            scope.addCleanup( Delegate<void()>::create( [pInput, layer]()
            { pInput->getInputMap().popLayer( layer ); } ) );
        }
        return true;
    }

    // 허브의 진입 자리(사물 상호작용 · 트리거)가 서기 전의 개발 입구 — 시나리오가 미니게임으로 바로 들어간다.
    SW_DEV_COMMAND( ExperienceEnter, "experience.enter", "experience.enter <name>", "Enter an experience (<pack>/experiences/<name>.experience.xml): open its map, then activate its kits",
                    &ExperienceManagerInternal::runEnterCommand );
    SW_AUTOMATION_QUERY_WATCH( experienceActive, "Experience.Active", "Name of the active experience; none without one (also while an entered experience waits for its map)",
                               &ExperienceManagerInternal::readActive, false, false );
} // namespace sw
