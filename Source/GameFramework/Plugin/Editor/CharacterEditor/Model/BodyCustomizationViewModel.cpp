#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodyCustomizationViewModel.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/AppearanceEditModel.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/BodyProfile.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterBodyDynamicsComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"

namespace sw::editor
{
    namespace
    {
        struct BodyCustomizationViewModelInternal
        {
            static void gatherChannels( const ResolvedAppearance& resolved, BodyCustomizationViewState& outState )
            {
                const ResolvedPart* pBody = nullptr;
                for ( const ResolvedPart& part : resolved._listPart )
                {
                    if ( part._owner.empty() && part._bodyShapes.empty() == false )
                        pBody = &part;
                }
                const shared_ptr<const BodyShapeSet> shapes = pBody != nullptr ? SharedAssetCache<BodyShapeSet>::acquire( pBody->_bodyShapes.c_str() ) : nullptr;
                for ( const ResolvedBoneProportion& proportion : resolved._listBoneProportion )
                {
                    if ( proportion._owner.empty() == false )
                        continue;
                    BodyCustomizationViewState::ChannelRow row;
                    row._channel = proportion._name;
                    row._value   = proportion._value;
                    if ( shapes != nullptr && shapes->findAxis( proportion._name ) != nullptr )
                        row._mapping = BodyChannelMapping::Mapped;
                    else if ( shapes != nullptr && shapes->isUnsupported( proportion._name ) )
                        row._mapping = BodyChannelMapping::Unsupported;
                    else
                        row._mapping = BodyChannelMapping::Missing;
                    outState._listChannel.push_back( row );
                }
            }

            static void gatherSections( const AppearanceDatabase& database, const ResolvedAppearance& resolved, BodyCustomizationViewState& outState )
            {
                for ( const ResolvedPart& part : resolved._listPart )
                {
                    const ItemVisualDef*     pVisual = database.getVisuals().findVisual( part._visualID );
                    const AppearancePartDef* pPart   = pVisual != nullptr ? pVisual->findPart( part._partName ) : nullptr;
                    if ( pPart == nullptr )
                        continue;
                    for ( const AppearanceSectionDef& section : pPart->_listSection )
                    {
                        BodyCustomizationViewState::SectionRow row;
                        row._owner    = part._owner;
                        row._part     = part._partName;
                        row._section  = section._name;
                        row._alias    = section._alias;
                        row._bDyeable = section._bDyeable ? SW_TRUE : SW_FALSE;
                        for ( const hashed_string& group : section._listGroup )
                        {
                            if ( row._groups.empty() == false )
                                row._groups += ", ";
                            row._groups += group.c_str();
                        }
                        for ( const ResolvedMaterialValue& value : resolved._listMaterialValue )
                        {
                            const bool bThisSection = value._owner == part._owner && value._section == section._name && value._target == ResolvedMaterialTarget::DyeChannel;
                            row._dyeCount += bThisSection ? 1u : 0u;
                        }
                        outState._listSection.push_back( row );
                    }
                }
            }

            static CharacterBodyDynamicsComponent* findDynamics( GameObject* pBody ) { return pBody != nullptr ? pBody->getComponent<CharacterBodyDynamicsComponent>() : nullptr; }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void BodyCustomizationViewModel::gather( const AppearanceEditModel& model, const AppearanceDatabase& database, const GameObject* pBody, BodyCustomizationViewState& outState )
    {
        outState                             = BodyCustomizationViewState{};
        const AppearanceSelection& selection = model.getSelection();
        for ( const AppearanceSlotRequest& request : selection._listSlot )
            outState._listSlot.push_back( BodyCustomizationViewState::SlotRow{ request._slot, request._itemID, request._bHiddenByPlayer } );
        for ( const SkinLayerValue& value : selection._listSkinLayer )
        {
            BodyCustomizationViewState::LayerRow row;
            row._value               = value;
            const SkinLayerDef* pDef = database.getSkinLayers().findLayer( value._layer );
            if ( pDef != nullptr )
            {
                row._category                                       = pDef->_category;
                row._smearResistance                                = pDef->_smearResistance;
                row._bPlaceable                                     = pDef->_bPlaceable ? SW_TRUE : SW_FALSE;
                const CustomizationSchemaCatalog::Palette* pPalette = pDef->_palette.empty() ? nullptr : database.getSchemas().findPalette( pDef->_palette );
                if ( pPalette != nullptr )
                    row._listSwatch = pPalette->_listSwatch;
            }
            outState._listLayer.push_back( row );
        }
        for ( const SkinLayerDef& def : database.getSkinLayers().getLayers() )
            outState._listAvailableLayer.push_back( def._id );

        const CharacterAppearanceComponent* pAppearance = pBody != nullptr ? pBody->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pAppearance == nullptr )
            return;
        outState._bHasBody                 = SW_TRUE;
        const ResolvedAppearance& resolved = pAppearance->getResolvedAppearance();
        outState._bodyProfile              = resolved._bodyProfile;
        BodyCustomizationViewModelInternal::gatherChannels( resolved, outState );
        BodyCustomizationViewModelInternal::gatherSections( database, resolved, outState );
        const CharacterBodyProfileDef* pProfile = database.getBodyProfiles().findProfile( resolved._bodyProfile );
        for ( size_t index = 0; pProfile != nullptr && index < pProfile->_listFluid.size(); ++index )
        {
            if ( VectorUtil::contains( outState._listFluidTrigger, pProfile->_listFluid[index]._trigger ) == false )
                outState._listFluidTrigger.push_back( pProfile->_listFluid[index]._trigger );
        }
        const CharacterBodyDynamicsComponent* pDynamics = pBody->getComponent<CharacterBodyDynamicsComponent>();
        if ( pDynamics == nullptr )
            return;
        outState._bHasDynamics       = SW_TRUE;
        outState._footLift           = pDynamics->getFootLift();
        outState._pupilSize          = pDynamics->getPupilSize();
        outState._eyeLuminance       = pDynamics->getEyeLuminance();
        outState._jiggleDisplacement = pDynamics->computeJiggleDisplacement();
        outState._fluidWetness       = pDynamics->computeFluidWetness();
        outState._smearDilution      = pDynamics->computeMaxSmearDilution();
    }

    bool BodyCustomizationViewModel::setSlotHidden( AppearanceEditModel& model, const hashed_string& slot, bool bHidden )
    {
        return model.setSlotHidden( slot, bHidden );
    }

    bool BodyCustomizationViewModel::addSkinLayer( AppearanceEditModel& model, const AppearanceDatabase& database, const hashed_string& layer )
    {
        const SkinLayerDef* pDef = database.getSkinLayers().findLayer( layer );
        return pDef != nullptr && model.addSkinLayer( layer, pDef->_defaultColor, pDef->_defaultOpacity );
    }

    bool BodyCustomizationViewModel::removeSkinLayer( AppearanceEditModel& model, uint32 index )
    {
        return model.removeSkinLayer( index );
    }

    bool BodyCustomizationViewModel::moveSkinLayer( AppearanceEditModel& model, uint32 index, int32 delta )
    {
        return model.moveSkinLayer( index, delta );
    }

    bool BodyCustomizationViewModel::setSkinLayer( AppearanceEditModel& model, uint32 index, const SkinLayerValue& value, bool bContinuous )
    {
        return model.setSkinLayer( index, value, bContinuous );
    }

    void BodyCustomizationViewModel::previewHit( GameObject* pBody )
    {
        CharacterBodyDynamicsComponent* pDynamics = BodyCustomizationViewModelInternal::findDynamics( pBody );
        if ( pDynamics != nullptr )
            pDynamics->addImpulse( float3{ 0.0f, kPreviewHitSpeed, 0.0f } );
    }

    void BodyCustomizationViewModel::previewFluid( GameObject* pBody, const hashed_string& trigger )
    {
        CharacterBodyDynamicsComponent* pDynamics = BodyCustomizationViewModelInternal::findDynamics( pBody );
        if ( pDynamics != nullptr )
            pDynamics->triggerFluid( trigger, kPreviewFluidSeconds, 1.0f );
    }

    void BodyCustomizationViewModel::clearSmears( GameObject* pBody )
    {
        CharacterBodyDynamicsComponent* pDynamics = BodyCustomizationViewModelInternal::findDynamics( pBody );
        if ( pDynamics != nullptr )
            pDynamics->clearSmears();
    }
} // namespace sw::editor
