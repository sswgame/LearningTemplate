#include "pch.h"

#include "Games/StarSkirmish/SkirmishUnitComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

#include "Games/StarSkirmish/SkirmishGameModeComponent.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 감시 값 `Skirmish.ViewLagMax` 입니다. 모든 유닛 뷰 가운데 메시가 디렉터의 유닛 자리에서 가장 멀리 벗어난 거리입니다.
         *        측정한 뷰가 하나도 없으면(뷰가 없거나 모두 비교 불가) false 를 돌려 시나리오가 빈 측정으로 통과하지 못하게 합니다.
         */
        [[nodiscard]] bool readViewLagMax( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            float32 lagMax        = 0.0f;
            uint32  measuredCount = 0;
            pManager->forEachComponentOfType<SkirmishUnitComponent>( [&lagMax, &measuredCount]( const SkirmishUnitComponent* pView )
            {
                const float32 lag = pView->measureViewLag();
                if ( lag < 0.0f )
                    return;
                lagMax = MathUtil::max( lagMax, lag );
                ++measuredCount;
            } );
            if ( measuredCount == 0 )
                return false;
            outValue = static_cast<float64>( lagMax );
            return true;
        }
    } // namespace
} // namespace sw

namespace sw
{
    SkirmishUnitComponent::SkirmishUnitComponent()
        : _airHeight{ 2.5f }
        , _footprintFill{ 0.9f }
        , _unitFill{ 1.6f }
        , _lastPosition{ 0.0f, 0.0f, 0.0f }
        , _unitID{}
        , _pShownLook{ nullptr }
        , _modelWidth{ 1.0f }
        , _yaw{ MathUtil::kPi }
        , _bPlaced{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void SkirmishUnitComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 판을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void SkirmishUnitComponent::assignUnit( GameObjectHandle gameMode, SlotHandle unitID, float32 modelWidth )
    {
        bindGameMode( gameMode );
        _unitID     = unitID;
        _modelWidth = modelWidth > 0.0f ? modelWidth : 1.0f;
        _pShownLook = nullptr;
        _yaw        = MathUtil::kPi; // 처음엔 카메라(남쪽) 쪽을 본다
        _bPlaced    = SW_FALSE;
    }

    string SkirmishUnitComponent::makeModelPath( const utf8* pName )
    {
        return string( "game/starskirmish/models/" ) + pName + string( MeshAssetFormat::kExtension );
    }

    vector<string> SkirmishUnitModelCatalog::makeAllModelPaths() const
    {
        vector<string> listPath;
        for ( const SkirmishUnitModel& entry : getAll() )
        {
            string path = SkirmishUnitComponent::makeModelPath( entry._model.c_str() );
            if ( VectorUtil::contains( listPath, path ) == false )
                listPath.push_back( std::move( path ) );
        }
        return listPath;
    }

    uint32 SkirmishUnitModelCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<SkirmishUnitModelCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&SkirmishUnitModelCatalog::loadModel>( "Model" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool SkirmishUnitModelCatalog::loadModel( const XMLNode& node, string_view sourceName )
    {
        // 세 칸은 리플렉션 필수 칸이다 — 빠진 칸 · 빈 이름(XMLFields.RequiredFieldMissing) · 범위 밖 폭(XMLFields.ValueOutOfRange)을 이름과 함께 알린다.
        SkirmishUnitModelDef def;
        if ( XMLFields::readFields( node, def, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "SkirmishUnit.ModelInvalidFieldsSkipped", "%#: model '%#' has invalid fields - skipped", sourceName, def._id.viewOrEmpty() );
            return false;
        }
        SkirmishUnitModel entry;
        entry._id    = def._id;
        entry._model = string( def._model.c_str() );
        entry._width = def._width;
        add( entry );
        return true;
    }

    float32 SkirmishUnitComponent::measureViewLag() const
    {
        const GameObject*        pOwner         = getOwner();
        const GameObjectManager* pManager       = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const MeshComponent*     pMesh          = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        const bool               bNotMeasurable = pManager == nullptr || pMesh == nullptr || _bPlaced == SW_FALSE;
        if ( bNotMeasurable )
            return -1.0f;
        const SkirmishGameModeComponent* pGameMode = findGameMode<SkirmishGameModeComponent>();
        const RTSUnit*                   pUnit     = pGameMode != nullptr ? pGameMode->getWorld().findUnit( _unitID ) : nullptr;
        if ( pUnit == nullptr )
            return -1.0f;
        const float3  shown = pMesh->getLocalPosition();
        const float32 dx    = shown._x - pUnit->_position._x;
        const float32 dz    = shown._z - pUnit->_position._z;
        return MathUtil::sqrt( dx * dx + dz * dz );
    }

    void SkirmishUnitComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const SkirmishGameModeComponent* pGameMode = findGameMode<SkirmishGameModeComponent>();
        const RTSUnit*                   pUnit     = pGameMode != nullptr ? pGameMode->getWorld().findUnit( _unitID ) : nullptr;
        if ( pUnit == nullptr )
            return;

        // 크기 · 자리 — 모델 바닥이 원점이다. 건물은 지은 만큼 솟고, 광물은 남은 만큼 낮아진다. 공중 유닛은 떠 있다.
        const RTSUnitDef& def = *pUnit->_pDef;
        float3            position{};
        float3            scale{};
        if ( pUnit->isResource() || pUnit->isBuilding() )
        {
            const float32 size   = static_cast<float32>( def._footprint ) * _footprintFill / _modelWidth;
            float32       height = 1.0f;
            if ( pUnit->isResource() && def._resourceType != RTSResourceType::Gas )
                height = 0.4f + 0.6f * ( def._resourceAmount > 0 ? static_cast<float32>( pUnit->_resourceLeft ) / static_cast<float32>( def._resourceAmount ) : 1.0f );
            else if ( pUnit->isBuilding() )
                height = 0.2f + 0.8f * MathUtil::clamp( pUnit->_buildProgress, 0.0f, 1.0f );
            scale    = float3{ size, size * height, size };
            position = float3{ pUnit->_position._x, 0.0f, pUnit->_position._z };
        }
        else
        {
            const float32 size = def._radius * 2.0f * _unitFill / _modelWidth;
            scale              = float3{ size, size, size };
            position           = float3{ pUnit->_position._x, def._bAir != SW_FALSE ? _airHeight : 0.0f, pUnit->_position._z };
        }
        // 움직이는 유닛은 움직인 쪽을 본다(키트 모델의 앞이 +Z). 멈추면 마지막 방향을 지킨다.
        const float32 moveX       = position._x - _lastPosition._x;
        const float32 moveZ       = position._z - _lastPosition._z;
        const bool    bMovingUnit = _bPlaced == SW_TRUE && pUnit->isBuilding() == false && pUnit->isResource() == false;
        if ( bMovingUnit && moveX * moveX + moveZ * moveZ > 1.0e-6f )
            _yaw = MathUtil::atan2( moveX, moveZ );
        _lastPosition = position;
        pMesh->setLocalPosition( position );
        pMesh->setLocalScale( scale );
        pMesh->setLocalRotation( float3{ 0.0f, _yaw, 0.0f } );
        const shared_ptr<MaterialInstance>& look = pGameMode->findUnitLook( *pUnit, pGameMode->isSelected( _unitID ) );
        if ( look != nullptr && look.get() != _pShownLook )
        {
            pMesh->setMaterialInstance( look );
            _pShownLook = look.get();
        }
        if ( _bPlaced == SW_FALSE )
        {
            pMesh->setVisible( true );
            _bPlaced = SW_TRUE;
        }
    }
} // namespace sw

namespace sw
{
    SW_AUTOMATION_WATCH( skirmishViewLagMax, "Skirmish.ViewLagMax", "Largest distance between a unit view mesh and the game mode's unit position", &readViewLagMax );
} // namespace sw

namespace sw
{
    /** @brief StarSkirmish 유닛 모델 표(`SkirmishUnitModelCatalog`)의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 알아본다). */
    SW_MODULE_SHARED_ASSET_CACHE( SkirmishUnitModels, SkirmishUnitModelCatalog, , SkirmishUnitModelCatalog::kAssetType.withLoader( AssetTypeLoader::of<SkirmishUnitModelCatalog>( MemoryTag::Game ) ) );
} // namespace sw
