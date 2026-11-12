#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodyCustomizationViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Name/TagID.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/AppearanceEditModel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodyDataEditModel.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/BodyProfile.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterBodyDynamicsComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ItemVisual.h"
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
                const shared_ptr<const BodyShapeSet> shapes = pBody != nullptr ? SharedAssetCache<BodyShapeSet>::acquire( pBody->_bodyShapes.view() ) : nullptr;
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
                        row._visual   = part._visualID;
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

            /** @brief 구간을 고르는 드라이브(include · exclude 를 적었거나 염색)를 모읍니다 — 강조 콤보의 항목이다. */
            static void gatherDrives( const AppearanceDatabase& database, BodyCustomizationViewState& outState )
            {
                for ( const CustomizationSchemaDef& schema : database.getSchemas().getSchemas() )
                {
                    for ( const CustomizationParamDef& param : schema._listParameter )
                    {
                        for ( uint32 drive = 0; drive < static_cast<uint32>( param._listDrive.size() ); ++drive )
                        {
                            const CustomizationDriveDef& def = param._listDrive[drive];
                            if ( def.isSectionTargeted() == false && def._kind != CustomizationDriveKind::DyeChannel )
                                continue;
                            BodyCustomizationViewState::DriveRow row;
                            row._parameter = param._name;
                            row._drive     = drive;
                            row._label     = string( param._name.c_str() ) + " / " + def._target.c_str();
                            outState._listDrive.push_back( std::move( row ) );
                        }
                    }
                }
            }

            /** @brief 강조 드라이브의 정의입니다. 없으면 nullptr 입니다. */
            static const CustomizationDriveDef* findDrive( const AppearanceDatabase& database, const BodyCustomizationViewState::DriveRow& row )
            {
                for ( const CustomizationSchemaDef& schema : database.getSchemas().getSchemas() )
                {
                    for ( const CustomizationParamDef& param : schema._listParameter )
                    {
                        if ( param._name == row._parameter && row._drive < param._listDrive.size() )
                            return &param._listDrive[row._drive];
                    }
                }
                return nullptr;
            }

            /** @brief 드라이브가 고르는 구간 줄에 강조를 켭니다. 주인이 빈 드라이브는 몸(주인 없는) 부품의 구간, 아니면 그 주인의 구간이다. */
            static void markHighlighted( const AppearanceDatabase& database, const CustomizationDriveDef& drive, BodyCustomizationViewState& inoutState )
            {
                for ( BodyCustomizationViewState::SectionRow& row : inoutState._listSection )
                {
                    const ItemVisualDef*        pVisual  = database.getVisuals().findVisual( row._visual );
                    const AppearancePartDef*    pPart    = pVisual != nullptr ? pVisual->findPart( row._part ) : nullptr;
                    const AppearanceSectionDef* pSection = pPart != nullptr ? pPart->findSection( row._section ) : nullptr;
                    const bool                  bOwner   = row._owner == drive._owner;
                    row._bHighlighted                    = pSection != nullptr && bOwner && drive.isDrivenSection( *pSection ) ? SW_TRUE : SW_FALSE;
                }
            }

            static string formatFloat( float32 value )
            {
                StringBuilder<constant::buffer::kMax64> textBuilder;
                textBuilder.appendFormat( "%#", value );
                return string( textBuilder.view() );
            }

            static vector<BodyDataElementKey> makeProfilePath( const hashed_string& profile )
            {
                return vector<BodyDataElementKey>{
                    BodyDataElementKey{ "BodyProfile", "id", string( profile.c_str() ) }
                };
            }

            static vector<BodyDataElementKey> makeSectionPath( const BodyCustomizationViewState::SectionRow& row )
            {
                return vector<BodyDataElementKey>{
                    BodyDataElementKey{"ItemVisual",   "id",  string( row._visual.c_str() )},
                    BodyDataElementKey{      "Part", "name",    string( row._part.c_str() )},
                    BodyDataElementKey{   "Section", "name", string( row._section.c_str() )}
                };
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void BodyCustomizationViewModel::gather( const AppearanceEditModel& model, const AppearanceDatabase& database, const GameObject* pBody, uint32 highlightDrive,
                                             BodyCustomizationViewState& outState )
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
        BodyCustomizationViewModelInternal::gatherDrives( database, outState );
        const CustomizationDriveDef* pHighlight = highlightDrive < outState._listDrive.size() ? BodyCustomizationViewModelInternal::findDrive( database, outState._listDrive[highlightDrive] ) : nullptr;
        if ( pHighlight != nullptr )
            BodyCustomizationViewModelInternal::markHighlighted( database, *pHighlight, outState );
        for ( const ResolvedPart& part : resolved._listPart )
        {
            if ( part._owner.empty() && part._bodyShapes.empty() == false )
                outState._bodyShapes = part._bodyShapes;
        }
        const CharacterBodyProfileDef* pProfile = database.getBodyProfiles().findProfile( resolved._bodyProfile );
        if ( pProfile != nullptr )
        {
            outState._wrinkleIntensity = pProfile->_wrinkleIntensity;
            for ( const RigJiggleGroupParams& jiggle : pProfile->_listJiggle )
                outState._listJiggle.push_back( BodyCustomizationViewState::JiggleRow{ jiggle } );
            for ( const WrinkleRegionDef& wrinkle : pProfile->_listWrinkle )
                outState._listWrinkle.push_back( BodyCustomizationViewState::WrinkleRow{ wrinkle } );
            for ( const BodyFluidDef& fluid : pProfile->_listFluid )
                outState._listFluid.push_back( BodyCustomizationViewState::FluidRow{ fluid._name, fluid._listPathPoint, fluid._ratePerSecond, fluid._speed, fluid._radius } );
            for ( const BodyActionScaleDef& action : pProfile->_listActionScale )
            {
                const bool bActive = pBody->hasTag( TagID::request( action._tag.c_str() ), true );
                outState._listAction.push_back( BodyCustomizationViewState::ActionRow{ action._tag, action._scale, static_cast<uint8>( bActive ? SW_TRUE : SW_FALSE ) } );
            }
        }
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

    bool BodyCustomizationViewModel::setAxisValue( BodyDataEditModel& shapes, const hashed_string& channel, const utf8* pAttribute, float32 value, bool bContinuous )
    {
        const BodyDataElementKey arrPath[] = {
            BodyDataElementKey{ "Axis", "name", string( channel.c_str() ) }
        };
        return shapes.setAttribute( arrPath, pAttribute, BodyCustomizationViewModelInternal::formatFloat( value ), bContinuous );
    }

    bool BodyCustomizationViewModel::addAxis( BodyDataEditModel& shapes, const hashed_string& channel )
    {
        using Internal                     = BodyCustomizationViewModelInternal;
        const BodyDataElementKey arrPath[] = {
            BodyDataElementKey{ "Axis", "name", string( channel.c_str() ) }
        };
        const BodyDataAttribute arrValues[] = {
            BodyDataAttribute{    "min", Internal::formatFloat( kNewAxisMin )},
            BodyDataAttribute{    "max", Internal::formatFloat( kNewAxisMax )},
            BodyDataAttribute{"default",        Internal::formatFloat( 0.0f )}
        };
        return shapes.setAttributes( arrPath, arrValues );
    }

    bool BodyCustomizationViewModel::declareUnsupported( BodyDataEditModel& shapes, const hashed_string& channel )
    {
        const BodyDataElementKey arrPath[] = {
            BodyDataElementKey{ "Unsupported", "name", string( channel.c_str() ) }
        };
        return shapes.ensureElement( arrPath );
    }

    bool BodyCustomizationViewModel::setJiggleValue( BodyDataEditModel& profiles, const hashed_string& profile, const hashed_string& group, const utf8* pAttribute, float32 value,
                                                     bool bContinuous )
    {
        vector<BodyDataElementKey> path = BodyCustomizationViewModelInternal::makeProfilePath( profile );
        path.push_back( BodyDataElementKey{ "Jiggle", "group", string( group.c_str() ) } );
        return profiles.setAttribute( path, pAttribute, BodyCustomizationViewModelInternal::formatFloat( value ), bContinuous );
    }

    bool BodyCustomizationViewModel::setWrinkleIntensity( BodyDataEditModel& profiles, const hashed_string& profile, float32 value, bool bContinuous )
    {
        return profiles.setAttribute( BodyCustomizationViewModelInternal::makeProfilePath( profile ), "wrinkleIntensity", BodyCustomizationViewModelInternal::formatFloat( value ), bContinuous );
    }

    bool BodyCustomizationViewModel::setWrinkleMask( BodyDataEditModel& profiles, const hashed_string& profile, const hashed_string& region, uint32 mask )
    {
        vector<BodyDataElementKey> path = BodyCustomizationViewModelInternal::makeProfilePath( profile );
        path.push_back( BodyDataElementKey{ "Wrinkle", "region", string( region.c_str() ) } );
        return profiles.setAttribute( path, "mask", to_string( static_cast<int32>( mask ) ), false );
    }

    bool BodyCustomizationViewModel::setWrinkleWeight( BodyDataEditModel& profiles, const hashed_string& profile, const hashed_string& region, const hashed_string& expression, float32 weight,
                                                       bool bContinuous )
    {
        vector<BodyDataElementKey> path = BodyCustomizationViewModelInternal::makeProfilePath( profile );
        path.push_back( BodyDataElementKey{ "Wrinkle", "region", string( region.c_str() ) } );
        path.push_back( BodyDataElementKey{ "Source", "expression", string( expression.c_str() ) } );
        return profiles.setAttribute( path, "weight", BodyCustomizationViewModelInternal::formatFloat( weight ), bContinuous );
    }

    bool BodyCustomizationViewModel::setFluidPath( BodyDataEditModel& profiles, const hashed_string& profile, const hashed_string& fluid, vector_reference<const float2> listPoint, bool bContinuous )
    {
        if ( listPoint.size() < 2 )
            return false; // 경로는 점 둘 이상이다(읽기가 거절한다)
        string text;
        for ( const float2& point : listPoint )
        {
            if ( text.empty() == false )
                text += "; ";
            text += BodyCustomizationViewModelInternal::formatFloat( point._x ) + " " + BodyCustomizationViewModelInternal::formatFloat( point._y );
        }
        vector<BodyDataElementKey> path = BodyCustomizationViewModelInternal::makeProfilePath( profile );
        path.push_back( BodyDataElementKey{ "Fluid", "name", string( fluid.c_str() ) } );
        return profiles.setAttribute( path, "path", text, bContinuous );
    }

    bool BodyCustomizationViewModel::setFluidValue( BodyDataEditModel& profiles, const hashed_string& profile, const hashed_string& fluid, const utf8* pAttribute, float32 value, bool bContinuous )
    {
        vector<BodyDataElementKey> path = BodyCustomizationViewModelInternal::makeProfilePath( profile );
        path.push_back( BodyDataElementKey{ "Fluid", "name", string( fluid.c_str() ) } );
        return profiles.setAttribute( path, pAttribute, BodyCustomizationViewModelInternal::formatFloat( value ), bContinuous );
    }

    bool BodyCustomizationViewModel::setSectionAlias( BodyDataEditModel& visuals, const BodyCustomizationViewState::SectionRow& row, string_view alias )
    {
        return visuals.setAttribute( BodyCustomizationViewModelInternal::makeSectionPath( row ), "alias", alias, false );
    }

    bool BodyCustomizationViewModel::setSectionGroups( BodyDataEditModel& visuals, const BodyCustomizationViewState::SectionRow& row, string_view groups )
    {
        return visuals.setAttribute( BodyCustomizationViewModelInternal::makeSectionPath( row ), "groups", groups, false );
    }

    bool BodyCustomizationViewModel::setSectionDyeable( BodyDataEditModel& visuals, const BodyCustomizationViewState::SectionRow& row, bool bDyeable )
    {
        return visuals.setAttribute( BodyCustomizationViewModelInternal::makeSectionPath( row ), "dyeable", bDyeable ? "true" : "false", false );
    }

    optional<uint32> BodyCustomizationViewModel::findSectionRow( const BodyCustomizationViewState& state, const hashed_string& section )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( state._listSection.size() ); ++index )
        {
            if ( state._listSection[index]._section == section )
                return index;
        }
        return nullopt;
    }

    bool BodyCustomizationViewModel::placeSkinLayer( AppearanceEditModel& model, uint32 index, const float2& uv, bool bContinuous )
    {
        const vector<SkinLayerValue>& listLayer = model.getSelection()._listSkinLayer;
        if ( index >= listLayer.size() )
            return false;
        SkinLayerValue value = listLayer[index];
        value._offset        = float2{ MathUtil::saturate( uv._x ), MathUtil::saturate( uv._y ) };
        return model.setSkinLayer( index, value, bContinuous );
    }

    void BodyCustomizationViewModel::previewActionTag( GameObject* pBody, const hashed_string& tag, bool bOn )
    {
        if ( pBody == nullptr || tag.empty() )
            return;
        if ( bOn )
            pBody->addTag( TagID::request( tag.c_str() ) );
        else
            pBody->removeTag( TagID::request( tag.c_str() ) );
    }

    void BodyCustomizationViewModel::previewStep( GameObject* pBody )
    {
        CharacterBodyDynamicsComponent* pDynamics = BodyCustomizationViewModelInternal::findDynamics( pBody );
        if ( pDynamics != nullptr )
            pDynamics->addImpulse( float3{ 0.0f, kPreviewStepSpeed, 0.0f } );
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
