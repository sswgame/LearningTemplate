#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Element/ElementStatusComponent.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Serialization/Format/FormatDef.h"

#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Element/ElementRuleCatalog.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

namespace sw
{
    namespace
    {
        struct ElementStatusComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "ELST" };
        };

        const StatefulComponentRegistrar<ElementStatusComponent> s_stateRegistration{ "ElementStatusComponent" };
    } // namespace

    ElementStatusComponent::ElementStatusComponent()
        : _tablePath{}
        , _elementMaterial{}
        , _signalStatus{}
        , _startStimulus{}
        , _grid{}
        , _restore{}
        , _pTable{}
        , _tableReload{}
    {
    }

    void ElementStatusComponent::rebuild()
    {
        _tableReload.markSeen();
        if ( _tablePath.empty() )
        {
            SW_LOG_ERROR_ID( "ElementStatusComponent.ElementStatusNoRule", "Element status '%#' has no rule table path - set Rule Table", _elementMaterial.c_str() );
            return;
        }
        // 새 테이블을 받기 전에 격자를 옛 테이블과 함께 쥔 채 둔다 — 받지 못하면 옛 상태 그대로다.
        shared_ptr<const ElementRuleCatalog> pTable = SharedAssetCache<ElementRuleCatalog>::acquire( _tablePath ).snapshot();
        if ( pTable == nullptr )
        {
            SW_LOG_ERROR_ID( "ElementStatusComponent.ElementRuleTableCould", "Element rule table %# could not be read", _tablePath.c_str() );
            return;
        }
        if ( _grid.initialize( 1, 1, pTable.get() ) == false )
            return; // 이유는 ElementGrid 가 남겼다
        _pTable                        = std::move( pTable );
        const optional<int32> material = _pTable->findMaterial( _elementMaterial );
        if ( material.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ElementStatusComponent.ElementMaterialNotRule", "Element material '%#' is not in the rule table", _elementMaterial.c_str() );
            return; // 0 번 재질로 대신 서지 않는다
        }
        _grid.setMaterial( int2{ 0, 0 }, *material );
    }

    void ElementStatusComponent::onPostLoad()
    {
        Component::onPostLoad();
        rebuild();
    }

    void ElementStatusComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _grid.getTable() == nullptr )
            rebuild();
        if ( _startStimulus.empty() == false )
            (void)applyStimulus( _startStimulus ); // 바뀐 것이 없거나 알 수 없는 자극이면 false 이고, 이때는 시작 상태 그대로 둔다
        (void)_restore.open( *this );              // 들고 있던 상태가 있으면 시작 상태를 덮는다
        publishSignal();
    }

    void ElementStatusComponent::serializeState( StateIO& io )
    {
        const bool bNothingToWrite = io.isWriting() && ( _restore.writeHeldBytes( io ) || _restore.isOpen() == false );
        if ( bNothingToWrite )
            return; // 들고 있던 바이트를 썼거나, 아직 테이블을 읽지 않았다
        io.header( ElementStatusComponentInternal::kStateFormat );
        io.valueAtomic( _grid );
        io.end();
    }

    void ElementStatusComponent::restoreState( vector<uint8>&& bytes )
    {
        _restore.receive( *this, std::move( bytes ) );
        publishSignal();
    }

    void ElementStatusComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _tableReload.isStale() )
            rebuild(); // 규칙 테이블 파일이 바뀌었으므로 새 테이블로 다시 만든다. 상태는 처음부터 시작한다
        if ( _grid.update( context.getDeltaTime() ) > 0 )
            publishSignal();
    }

    bool ElementStatusComponent::applyStimulus( const hashed_string& stimulus )
    {
        const ElementRuleCatalog* pTable = _grid.getTable();
        if ( pTable == nullptr )
            return false;
        const optional<int32> found    = pTable->findStimulus( stimulus );
        const bool            bChanged = found.has_value() && _grid.applyStimulus( int2{ 0, 0 }, *found ) > 0;
        publishSignal();
        return bChanged;
    }

    bool ElementStatusComponent::hasStatus( const hashed_string& status ) const
    {
        const ElementRuleCatalog* pTable = _grid.getTable();
        if ( pTable == nullptr )
            return false;
        const optional<int32> found = pTable->findStatus( status );
        return found.has_value() && _grid.hasStatus( int2{ 0, 0 }, *found );
    }

    void ElementStatusComponent::setMaterial( const hashed_string& material )
    {
        _elementMaterial = material;
        rebuild();
        publishSignal();
    }

    void ElementStatusComponent::setTablePath( string_view path )
    {
        _tablePath = path;
        rebuild();
        publishSignal();
    }

    void ElementStatusComponent::publishSignal()
    {
        GameObject*             pOwner  = getOwner();
        GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pSensor != nullptr )
            pSensor->setSignal( hasStatus( _signalStatus ) ? 1.0f : 0.0f );
    }
} // namespace sw
