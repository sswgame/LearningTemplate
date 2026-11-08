#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/ItemVisual.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    namespace
    {
        struct ItemVisualInternal
        {
            static constexpr const utf8* kArrRegionAttribute[]       = { "name" };
            static constexpr const utf8* kArrStateAttribute[]        = { "name" };
            static constexpr const utf8* kArrPlaceAttribute[]        = { "part", "socket", "offset", "rotation" };
            static constexpr const utf8* kArrHidePartAttribute[]     = { "part" };
            static constexpr const utf8* kArrStageVariantAttribute[] = { "part", "name" };

            /** @brief 부품 종류가 쓰는 에셋 속성 이름입니다. 몸 수정에는 없습니다. */
            static const utf8* getAssetAttribute( AppearancePartKind kind )
            {
                switch ( kind )
                {
                    case AppearancePartKind::Skinned:
                        return "mesh";
                    case AppearancePartKind::SocketPrefab:
                        return "prefab";
                    case AppearancePartKind::Sprite:
                        return "sprite";
                    case AppearancePartKind::BodyModification:
                        return nullptr;
                }
                return nullptr;
            }

            /** @brief 종류 @p kind 가 쓰는 에셋 칸의 값입니다(`getAssetAttribute` 와 같은 갈래). 몸 수정은 에셋이 없다. */
            template <typename TDef>
            static hashed_string pickAsset( AppearancePartKind kind, const TDef& def )
            {
                switch ( kind )
                {
                    case AppearancePartKind::Skinned:
                        return def._meshAsset;
                    case AppearancePartKind::SocketPrefab:
                        return def._prefabAsset;
                    case AppearancePartKind::Sprite:
                        return def._spriteAsset;
                    case AppearancePartKind::BodyModification:
                        return {};
                }
                return {};
            }

            /** @brief 종류에 맞지 않는 에셋 속성이 있으면 오류로 처리합니다. */
            static void reportMisplacedAssetAttributes( const XMLNode& node, AppearancePartKind kind, AppearanceLoadReport& report, string_view sourceName, const hashed_string& visualID )
            {
                constexpr const utf8* kArrAssetAttribute[] = { "mesh", "prefab", "sprite" };
                const utf8*           pExpected            = getAssetAttribute( kind );
                for ( const utf8* pAttribute : kArrAssetAttribute )
                {
                    if ( node.findAttribute( pAttribute ) != nullptr && ( pExpected == nullptr || StringUtil::equals( pAttribute, pExpected, true ) == false ) )
                        report.addErrorWithID( "Appearance.PartAttributeNotAllowed", "%#: visual '%#' <%#> of kind %# cannot have '%#'", sourceName, visualID.c_str(), node.getName(), getEnumName( kind ), pAttribute );
                }
            }

            static bool hasPart( const ItemVisualDef& visual, const hashed_string& part, AppearanceLoadReport& report, string_view sourceName, const utf8* pWhere )
            {
                if ( visual.findPart( part ) != nullptr )
                    return true;
                report.addErrorWithID( "Appearance.PartUnknown", "%#: visual '%#' %# names unknown part '%#'", sourceName, visual._id.c_str(), pWhere, part.c_str() );
                return false;
            }

            /** @brief 머티리얼 값 원소(`<Material name value part section>`) 하나를 읽습니다. 값은 스칼라 · 벡터 네 모양이다(`readRequiredShaderValue`). */
            static AppearanceMaterialValueDef readMaterialValue( const XMLNode& node, AppearanceLoadReport& report, string_view sourceName )
            {
                AppearanceMaterialValueDef value;
                (void)AppearanceXMLUtil::readFields( node, value, report, sourceName, { "value" } );
                AppearanceXMLUtil::readRequiredShaderValue( node, "value", value._value, report, sourceName );
                return value;
            }

            /** @brief 구간 원소(`<Section name material>`)를 @p inoutListSection 에 더합니다. 같은 이름이 이미 있으면 오류입니다. */
            static void readSection( const XMLNode& node, vector<AppearanceSectionDef>& inoutListSection, AppearanceLoadReport& report, string_view sourceName, const hashed_string& visualID,
                                     const hashed_string& partName )
            {
                AppearanceSectionDef section;
                if ( AppearanceXMLUtil::readFields( node, section, report, sourceName, { "groups" } ) == false )
                    return; // 빠진 이름 · 머티리얼은 위 읽기가 보고서에 남겼다
                AppearanceXMLUtil::readNameList( node, "groups", section._listGroup );
                if ( VectorUtil::containsBy<&AppearanceSectionDef::_name>( inoutListSection, section._name ) )
                    report.addErrorWithID( "Appearance.SectionInvalid", "%#: visual '%#' part '%#' has a duplicate section '%#'", sourceName, visualID.c_str(), partName.c_str(), section._name.c_str() );
                else
                    inoutListSection.push_back( section );
            }

            /** @brief 변형의 자식(`<Section>` 구간 머티리얼, `<Material>` 머티리얼 값)을 읽습니다. 구간 이름이 부품에 있는지는 부품을 다 읽은 뒤 `reportUnknownSections` 가 본다. */
            static void readVariantChildren( const XMLNode& node, const AppearancePartDef& part, AppearancePartVariantDef& inoutVariant, AppearanceLoadReport& report, string_view sourceName,
                                             const hashed_string& visualID )
            {
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Section" ) && part._kind == AppearancePartKind::Skinned )
                    {
                        readSection( child, inoutVariant._listSection, report, sourceName, visualID, part._name );
                    }
                    else if ( StringUtil::equals( child.getName(), "Material" ) )
                    {
                        const AppearanceMaterialValueDef value = readMaterialValue( child, report, sourceName );
                        if ( value._part.empty() == false )
                            report.addErrorWithID( "Appearance.VariantMaterialPart", "%#: visual '%#' part '%#' variant '%#' <Material> cannot name a part - it applies to its own part", sourceName,
                                                   visualID.c_str(), part._name.c_str(), inoutVariant._name.c_str() );
                        else
                            inoutVariant._listMaterialValue.push_back( value );
                    }
                    else
                    {
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                    }
                }
            }

            /** @brief 변형 @p variant 가 적은 구간 이름(`<Section>` · `<Material section>`)이 부품 @p part 의 구간인지 봅니다. 없는 구간이면 오류입니다(조용히 아무것도 바꾸지 않게). */
            static void reportUnknownVariantSections( const AppearancePartDef& part, const AppearancePartVariantDef& variant, AppearanceLoadReport& report, string_view sourceName, const hashed_string& visualID )
            {
                for ( const AppearanceSectionDef& section : variant._listSection )
                {
                    if ( part.findSection( section._name ) == nullptr )
                        report.addErrorWithID( "Appearance.SectionUnknown", "%#: visual '%#' part '%#' variant '%#' names section '%#' the part does not list", sourceName, visualID.c_str(), part._name.c_str(),
                                               variant._name.c_str(), section._name.c_str() );
                }
                for ( const AppearanceMaterialValueDef& value : variant._listMaterialValue )
                {
                    if ( value._section.empty() == false && part.findSection( value._section ) == nullptr )
                        report.addErrorWithID( "Appearance.SectionUnknown", "%#: visual '%#' part '%#' variant '%#' <Material> names section '%#' the part does not list", sourceName, visualID.c_str(),
                                               part._name.c_str(), variant._name.c_str(), value._section.c_str() );
                }
            }

            static void readPartChild( const XMLNode& child, const XMLNode& partNode, AppearancePartDef& inoutPart, AppearanceLoadReport& report, string_view sourceName, const hashed_string& visualID )
            {
                const utf8* pName         = child.getName();
                const bool  bModification = inoutPart._kind == AppearancePartKind::BodyModification;
                if ( StringUtil::equals( pName, "Variant" ) && bModification == false )
                {
                    reportMisplacedAssetAttributes( child, inoutPart._kind, report, sourceName, visualID );
                    AppearancePartVariantDef variant;
                    (void)AppearanceXMLUtil::readFields( child, variant, report, sourceName ); // 에셋 칸 중 무엇을 쓰는지는 부품 종류가 정한다(`pickAsset`)
                    readVariantChildren( child, inoutPart, variant, report, sourceName, visualID );
                    variant._asset = pickAsset( inoutPart._kind, variant );
                    if ( variant._skeleton.empty() == false && inoutPart._kind != AppearancePartKind::Skinned )
                        report.addErrorWithID( "Appearance.VariantSkeletonNotSkinned", "%#: visual '%#' part '%#' variant '%#' names a skeleton but the part is not Skinned", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    if ( variant._name.empty() || inoutPart.findVariant( variant._name ) != nullptr )
                        report.addErrorWithID( "Appearance.VariantNameInvalid", "%#: visual '%#' part '%#' has a variant without a name or a duplicate '%#'", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    else
                        inoutPart._listVariant.push_back( variant );
                }
                else if ( StringUtil::equals( pName, "MaterialVariant" ) && bModification == false )
                {
                    AppearancePartVariantDef variant;
                    (void)AppearanceXMLUtil::readFields( child, variant, report, sourceName, {} );
                    readVariantChildren( child, inoutPart, variant, report, sourceName, visualID );
                    // 에셋 칸은 메시 변형(`<Variant>`)의 것이다 — 머티리얼 변형이 적으면 읽히고 쓰이지 않으므로 오류다.
                    if ( variant._meshAsset.empty() == false || variant._prefabAsset.empty() == false || variant._spriteAsset.empty() == false )
                        report.addErrorWithID( "Appearance.MaterialVariantAsset", "%#: visual '%#' part '%#' material variant '%#' cannot name an asset (mesh · prefab · sprite)", sourceName, visualID.c_str(),
                                               inoutPart._name.c_str(), variant._name.c_str() );
                    // 머티리얼 변형은 머티리얼 · 구간 머티리얼 · 머티리얼 값 가운데 하나는 바꿔야 한다(아무것도 바꾸지 않는 변형은 철자 실수다).
                    const bool bChangesNothing = variant._material.empty() && variant._listSection.empty() && variant._listMaterialValue.empty();
                    const bool bInvalidVariant = variant._name.empty() || bChangesNothing || variant._skeleton.empty() == false || inoutPart.findMaterialVariant( variant._name ) != nullptr;
                    if ( bInvalidVariant )
                        report.addErrorWithID( "Appearance.VariantNameInvalid", "%#: visual '%#' part '%#' has a bad or duplicate material variant '%#'", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    else
                        inoutPart._listMaterialVariant.push_back( variant );
                }
                else if ( StringUtil::equals( pName, "Section" ) && inoutPart._kind == AppearancePartKind::Skinned )
                {
                    readSection( child, inoutPart._listSection, report, sourceName, visualID, inoutPart._name );
                }
                else if ( StringUtil::equals( pName, "Morph" ) && bModification )
                {
                    AppearanceMorphDef morph;
                    (void)AppearanceXMLUtil::readFields( child, morph, report, sourceName, {} );
                    inoutPart._listMorph.push_back( morph );
                }
                else if ( StringUtil::equals( pName, "Material" ) && bModification )
                {
                    inoutPart._listMaterialValue.push_back( readMaterialValue( child, report, sourceName ) );
                }
                else if ( StringUtil::equals( pName, "ChannelMaterial" ) && bModification == false )
                {
                    AppearanceChannelMaterialDef channelMaterial;
                    if ( AppearanceXMLUtil::readFields( child, channelMaterial, report, sourceName, {} ) == false )
                        return; // 빠진 칸은 위 읽기가 보고서에 남겼다
                    if ( channelMaterial._channelMax <= channelMaterial._channelMin )
                        report.addErrorWithID( "Appearance.ChannelMaterialRange", "%#: visual '%#' part '%#' <ChannelMaterial channel='%#'> needs channelMax > channelMin", sourceName, visualID.c_str(),
                                               inoutPart._name.c_str(), channelMaterial._channel.c_str() );
                    else
                        inoutPart._listChannelMaterial.push_back( channelMaterial );
                }
                else if ( StringUtil::equals( pName, "HideRegion" ) && bModification )
                {
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrRegionAttribute, report, sourceName );
                    const hashed_string region = AppearanceXMLUtil::readRequiredName( child, "name", report, sourceName );
                    if ( region.empty() == false )
                        inoutPart._listHiddenRegion.push_back( region ); // 이름 없는 구역은 보고서에 오류로 남았다
                }
                else
                {
                    AppearanceXMLUtil::reportUnknownChild( partNode, child, report, sourceName );
                }
            }

            static void readPart( const XMLNode& node, ItemVisualDef& inoutVisual, AppearanceLoadReport& report, string_view sourceName )
            {
                AppearancePartDef part;
                // 에셋은 종류가 속성을 정하고, 배치 · 충격 · 깃발은 적었는지가 기본값을 가른다.
                if ( AppearanceXMLUtil::readFields( node, part, report, sourceName, { "socket", "offset", "rotation", "deforms", "breakable", "impulse" } ) == false )
                    return;
                if ( part._name.empty() || inoutVisual.findPart( part._name ) != nullptr )
                {
                    report.addErrorWithID( "Appearance.PartNameInvalid", "%#: visual '%#' has a part without a name or a duplicate '%#'", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                    return;
                }
                reportMisplacedAssetAttributes( node, part._kind, report, sourceName, inoutVisual._id );
                part._asset                 = pickAsset( part._kind, part );
                const utf8* pAssetAttribute = getAssetAttribute( part._kind );
                // 자기 리그는 자기 뼈를 움직인다 — 스킨드 부품이고 몸과 다른 자기 스켈레톤을 적어야 한다(몸 스켈레톤을 빌리면 몸 리더 포즈와 다툰다).
                const bool bRigWithoutOwnSkeleton = part._rig.empty() == false && ( part._kind != AppearancePartKind::Skinned || part._skeleton.empty() );
                if ( bRigWithoutOwnSkeleton )
                    report.addErrorWithID( "Appearance.PartRigNeedsSkeleton", "%#: visual '%#' part '%#' has a 'rig' but is not a Skinned part with its own 'skeleton'", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                const bool bFitOnRigidPart = ( part._fit.empty() == false || part._fitTables.empty() == false ) && part._kind != AppearancePartKind::Skinned;
                if ( bFitOnRigidPart )
                    report.addErrorWithID( "Appearance.PartFitNotAllowed", "%#: visual '%#' part '%#' of kind %# cannot have 'fit' or 'fitTables' (only Skinned parts are fitted)", sourceName, inoutVisual._id.c_str(),
                                           part._name.c_str(), getEnumName( part._kind ) );
                if ( part._bodyShapes.empty() == false && part._kind != AppearancePartKind::Skinned )
                    report.addErrorWithID( "Appearance.PartFitNotAllowed", "%#: visual '%#' part '%#' of kind %# cannot have 'bodyShapes' (only the Skinned body has body shape axes)", sourceName, inoutVisual._id.c_str(),
                                           part._name.c_str(), getEnumName( part._kind ) );
                // 적지 않으면 스킨 파츠만 변형하고, 부서지는 단계를 적은 파츠만 떨어진다(이 파츠의 데이터에서 온 값).
                part._bDeforms   = part._kind == AppearancePartKind::Skinned ? SW_TRUE : SW_FALSE;
                part._bBreakable = part._breakStage.empty() == false ? SW_TRUE : SW_FALSE;
                if ( XMLReadUtil::readOptionalFlag( node, "deforms", part._bDeforms ) == false || XMLReadUtil::readOptionalFlag( node, "breakable", part._bBreakable ) == false )
                    report.addErrorWithID( "Appearance.PartDeformsInvalid", "%#: visual '%#' part '%#' has an invalid deforms / breakable", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                // 휘는 것은 스킨드 메시뿐이다(2D 스프라이트 스키닝 · 프리팹 변형은 없다). 휘지 않는 스킨드는 소켓(본 이름 포함) 하나를 따르는 단단한 조각이라
                // 몸 스켈레톤으로 피팅 · 리그하지 않는다 — 언리얼 소켓에 붙인 Static Mesh, 유니티 본 아래 둔 MeshRenderer 의 자리.
                if ( part._bDeforms == SW_TRUE && part._kind != AppearancePartKind::Skinned )
                    report.addErrorWithID( "Appearance.PartDeformsInvalid", "%#: visual '%#' part '%#' of kind %# cannot deform - only Skinned parts skin", sourceName, inoutVisual._id.c_str(), part._name.c_str(),
                                           getEnumName( part._kind ) );
                const bool bRigidSkinned = part._kind == AppearancePartKind::Skinned && part._bDeforms == SW_FALSE;
                const bool bRigidFitted  = part._rig.empty() == false || part._fit.empty() == false || part._fitTables.empty() == false || part._bodyShapes.empty() == false;
                if ( bRigidSkinned && bRigidFitted )
                    report.addErrorWithID( "Appearance.PartDeformsInvalid", "%#: visual '%#' part '%#' does not deform - it cannot have 'rig', 'fit', 'fitTables' or 'bodyShapes'", sourceName, inoutVisual._id.c_str(),
                                           part._name.c_str() );
                // 그리기 순서는 스프라이트만 가진다(3D 메시는 깊이 · 정렬 레이어로 정한다).
                if ( node.findAttribute( "layer" ) != nullptr && part._kind != AppearancePartKind::Sprite )
                    report.addErrorWithID( "Appearance.PartAttributeNotAllowed", "%#: visual '%#' part '%#' of kind %# cannot have 'layer' (only Sprite parts have a draw order)", sourceName,
                                           inoutVisual._id.c_str(), part._name.c_str(), getEnumName( part._kind ) );
                AppearanceXMLUtil::readOptionalFloat3( node, "impulse", part._breakImpulse, report, sourceName );
                AppearanceXMLUtil::readPlacement( node, "socket", part._placement, report, sourceName );
                if ( pAssetAttribute != nullptr && part._asset.empty() )
                    report.addErrorWithID( "Appearance.PartAssetMissing", "%#: visual '%#' part '%#' needs '%#'", sourceName, inoutVisual._id.c_str(), part._name.c_str(), pAssetAttribute );
                // 소켓에 붙는 부품(프리팹 · 스프라이트 · 휘지 않는 스킨드 조각)은 붙을 소켓을 적는다.
                const bool bSocketBound = part._kind == AppearancePartKind::SocketPrefab || part._kind == AppearancePartKind::Sprite || bRigidSkinned;
                if ( bSocketBound && part._placement.isEmpty() )
                    report.addErrorWithID( "Appearance.PartSocketMissing", "%#: visual '%#' part '%#' needs a socket", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                if ( part._kind == AppearancePartKind::BodyModification && ( part._placement.isEmpty() == false || part._bBreakable == SW_TRUE ) )
                    report.addErrorWithID( "Appearance.PartBodyModificationInvalid", "%#: visual '%#' body modification '%#' cannot have a socket or break off", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    readPartChild( child, node, part, report, sourceName, inoutVisual._id );
                }
                const bool bLODRuleOnUnmerged = ( part._lodDetail != 1.0f || part._lodDropLevel != 0 ) && part._kind != AppearancePartKind::Skinned;
                if ( bLODRuleOnUnmerged )
                    report.addErrorWithID( "Appearance.LODRuleNotAllowed", "%#: visual '%#' part '%#' of kind %# cannot have lodDetail / lodDropLevel (only merged Skinned parts)", sourceName, inoutVisual._id.c_str(),
                                           part._name.c_str(), getEnumName( part._kind ) );
                if ( part._listSection.empty() == false && part._material.empty() == false )
                    report.addErrorWithID( "Appearance.SectionWithPartMaterial", "%#: visual '%#' part '%#' has both 'material' and <Section> - give each section its material", sourceName,
                                           inoutVisual._id.c_str(), part._name.c_str() );
                // 구간은 변형보다 뒤에 적을 수 있다 — 다 읽은 뒤 변형이 짚는 구간을 본다.
                for ( const AppearancePartVariantDef& variant : part._listVariant )
                {
                    reportUnknownVariantSections( part, variant, report, sourceName, inoutVisual._id );
                }
                for ( const AppearancePartVariantDef& variant : part._listMaterialVariant )
                {
                    reportUnknownVariantSections( part, variant, report, sourceName, inoutVisual._id );
                }
                inoutVisual._listPart.push_back( part );
            }

            static void readState( const XMLNode& node, ItemVisualDef& inoutVisual, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrStateAttribute, report, sourceName );
                AppearanceStateDef state;
                state._name = AppearanceXMLUtil::readRequiredName( node, "name", report, sourceName );
                if ( state._name.empty() || inoutVisual.findState( state._name ) != nullptr )
                {
                    report.addErrorWithID( "Appearance.StateNameInvalid", "%#: visual '%#' has a state without a name or a duplicate '%#'", sourceName, inoutVisual._id.c_str(), state._name.c_str() );
                    return;
                }
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Place" ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrPlaceAttribute, report, sourceName );
                        AppearanceStateDef::Placement placement;
                        placement._part = AppearanceXMLUtil::readRequiredName( child, "part", report, sourceName );
                        AppearanceXMLUtil::readPlacement( child, "socket", placement._placement, report, sourceName );
                        if ( hasPart( inoutVisual, placement._part, report, sourceName, "<Place>" ) )
                            state._listPlacement.push_back( placement );
                    }
                    else if ( StringUtil::equals( child.getName(), "HidePart" ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrHidePartAttribute, report, sourceName );
                        const hashed_string part = AppearanceXMLUtil::readRequiredName( child, "part", report, sourceName );
                        if ( hasPart( inoutVisual, part, report, sourceName, "<HidePart>" ) )
                            state._listHiddenPart.push_back( part );
                    }
                    else
                    {
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                    }
                }
                inoutVisual._listState.push_back( state );
            }

            static void readDamageStage( const XMLNode& node, ItemVisualDef& inoutVisual, AppearanceLoadReport& report, string_view sourceName )
            {
                AppearanceDamageStageDef stage;
                if ( AppearanceXMLUtil::readFields( node, stage, report, sourceName, {} ) == false )
                    return;
                const float32 previous            = inoutVisual._listDamageStage.empty() ? 0.0f : inoutVisual._listDamageStage.back()._threshold;
                const bool    bOrdered            = previous < stage._threshold && stage._threshold <= 1.0f;
                const bool    bInvalidDamageStage = stage._name.empty() || inoutVisual.findDamageStage( stage._name ) != nullptr || bOrdered == false;
                if ( bInvalidDamageStage )
                {
                    report.addErrorWithID( "Appearance.DamageStageThreshold", "%#: visual '%#' damage stage '%#' needs a unique name and a threshold in (previous, 1]", sourceName, inoutVisual._id.c_str(), stage._name.c_str() );
                    return;
                }
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Variant" ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrStageVariantAttribute, report, sourceName );
                        AppearanceDamageStageDef::PartVariant entry;
                        entry._part                    = AppearanceXMLUtil::readRequiredName( child, "part", report, sourceName );
                        entry._variant                 = AppearanceXMLUtil::readRequiredName( child, "name", report, sourceName );
                        const AppearancePartDef* pPart = inoutVisual.findPart( entry._part );
                        if ( pPart == nullptr || pPart->findVariant( entry._variant ) == nullptr )
                            report.addErrorWithID( "Appearance.PartUnknown", "%#: visual '%#' damage stage '%#' names unknown part/variant '%#'/'%#'", sourceName, inoutVisual._id.c_str(), stage._name.c_str(), entry._part.c_str(),
                                                   entry._variant.c_str() );
                        else
                            stage._listVariant.push_back( entry );
                    }
                    else if ( StringUtil::equals( child.getName(), "Material" ) )
                    {
                        const AppearanceMaterialValueDef value = readMaterialValue( child, report, sourceName );
                        if ( value._part.empty() == false && hasPart( inoutVisual, value._part, report, sourceName, "damage <Material>" ) == false )
                            continue;
                        // 구간은 부품 안의 이름이라 부품을 함께 적는다(어느 부품의 구간인지 모르면 조용히 빗나간다).
                        const AppearancePartDef* pPart = value._part.empty() ? nullptr : inoutVisual.findPart( value._part );
                        if ( value._section.empty() == false && ( pPart == nullptr || pPart->findSection( value._section ) == nullptr ) )
                        {
                            report.addErrorWithID( "Appearance.SectionUnknown", "%#: visual '%#' damage stage '%#' <Material> names section '%#' without a part that lists it", sourceName, inoutVisual._id.c_str(),
                                                   stage._name.c_str(), value._section.c_str() );
                            continue;
                        }
                        stage._listMaterialValue.push_back( value );
                    }
                    else
                    {
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                    }
                }
                inoutVisual._listDamageStage.push_back( stage );
            }

            /** @brief 몸 채널 층(`<BodyChannel name value priority blend/>`)을 읽습니다. 같은 채널을 두 번 적으면 오류입니다(어느 층이 이기는지 모호하다). */
            static void readBodyChannel( const XMLNode& node, ItemVisualDef& inoutVisual, AppearanceLoadReport& report, string_view sourceName )
            {
                BodyChannelLayerDef layer;
                if ( AppearanceXMLUtil::readFields( node, layer, report, sourceName ) == false )
                    return;
                if ( VectorUtil::containsBy<&BodyChannelLayerDef::_name>( inoutVisual._listBodyChannel, layer._name ) )
                    report.addErrorWithID( "Appearance.BodyChannelRepeated", "%#: visual '%#' repeats body channel '%#'", sourceName, inoutVisual._id.c_str(), layer._name.c_str() );
                else
                    inoutVisual._listBodyChannel.push_back( layer );
            }

            static void readVisual( const XMLNode& node, ItemVisualDef& outVisual, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::readFields( node, outVisual, report, sourceName, { "id", "tags" } );
                AppearanceXMLUtil::readTags( node, "tags", outVisual._tags );
                // 부품을 먼저 모두 읽는다. 상태와 피해 단계가 파일 안의 순서와 상관없이 부품을 가리킬 수 있게 하기 위해서다.
                for ( XMLNode child = node.findChild( "Part" ); child; child = child.findNextSibling( "Part" ) )
                {
                    readPart( child, outVisual, report, sourceName );
                }
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pName = child.getName();
                    if ( StringUtil::equals( pName, "Part" ) )
                        continue;
                    if ( StringUtil::equals( pName, "State" ) )
                        readState( child, outVisual, report, sourceName );
                    else if ( StringUtil::equals( pName, "BodyChannel" ) )
                        readBodyChannel( child, outVisual, report, sourceName );
                    else if ( StringUtil::equals( pName, "DamageStage" ) )
                        readDamageStage( child, outVisual, report, sourceName );
                    else
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                }
                if ( outVisual._defaultState.empty() == false && outVisual.findState( outVisual._defaultState ) == nullptr )
                    report.addErrorWithID( "Appearance.DefaultStateUnknown", "%#: visual '%#' default state '%#' is not one of its states", sourceName, outVisual._id.c_str(), outVisual._defaultState.c_str() );
                if ( outVisual._defaultState.empty() && outVisual._listState.empty() == false )
                    outVisual._defaultState = outVisual._listState.front()._name;
                for ( const AppearancePartDef& part : outVisual._listPart )
                {
                    if ( part._breakStage.empty() == false && outVisual.findDamageStage( part._breakStage ) == nullptr )
                        report.addErrorWithID( "Appearance.DamageStageUnknown", "%#: visual '%#' part '%#' breaks at unknown damage stage '%#'", sourceName, outVisual._id.c_str(), part._name.c_str(), part._breakStage.c_str() );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const AppearancePartVariantDef* AppearancePartDef::findVariant( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AppearancePartVariantDef::_name>( _listVariant, name );
    }

    const AppearancePartVariantDef* AppearancePartDef::findMaterialVariant( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AppearancePartVariantDef::_name>( _listMaterialVariant, name );
    }

    const AppearanceSectionDef* AppearancePartDef::findSection( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AppearanceSectionDef::_name>( _listSection, name );
    }

    const AppearancePartDef* ItemVisualDef::findPart( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AppearancePartDef::_name>( _listPart, name );
    }

    bool AppearanceSectionDef::matchesTag( const hashed_string& tag ) const
    {
        return _name == tag || ( _alias.empty() == false && _alias == tag ) || VectorUtil::contains( _listGroup, tag );
    }

    float32 AppearanceChannelMaterialDef::computeValue( float32 channelValue ) const
    {
        const float32 t = MathUtil::saturate( ( channelValue - _channelMin ) / ( _channelMax - _channelMin ) ); // 읽기가 channelMax > channelMin 을 지켰다
        return MathUtil::lerp( _valueMin, _valueMax, t );
    }

    bool ItemVisualDef::hasSectionTag( const hashed_string& tag ) const
    {
        for ( const AppearancePartDef& part : _listPart )
        {
            for ( const AppearanceSectionDef& section : part._listSection )
            {
                if ( section.matchesTag( tag ) )
                    return true;
            }
        }
        return false;
    }

    const AppearanceStateDef* ItemVisualDef::findState( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AppearanceStateDef::_name>( _listState, name );
    }

    const AppearanceDamageStageDef* ItemVisualDef::findDamageStage( const hashed_string& name ) const
    {
        const optional<int32> index = findDamageStageIndex( name );
        return index.has_value() ? &_listDamageStage[static_cast<size_t>( *index )] : nullptr;
    }

    optional<int32> ItemVisualDef::findDamageStageIndex( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listDamageStage.size(); ++index )
        {
            if ( _listDamageStage[index]._name == name )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    int32 ItemVisualDef::computeDamageStageIndex( float32 damage ) const
    {
        int32 stageIndex = constant::invalid_index::kInt32;
        for ( size_t index = 0; index < _listDamageStage.size(); ++index )
        {
            if ( _listDamageStage[index]._threshold <= damage )
                stageIndex = static_cast<int32>( index );
        }
        return stageIndex;
    }

    bool ItemVisualCatalog::loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "ItemVisual" ) == false )
            {
                AppearanceXMLUtil::reportUnknownChild( root, node, report, sourceName );
                continue;
            }
            ItemVisualDef visual;
            visual._id = AppearanceXMLUtil::readRequiredName( node, "id", report, sourceName );
            if ( visual._id.empty() )
                continue; // 빠진 id 는 위 읽기가 보고서에 남겼다
            if ( findVisual( visual._id ) != nullptr )
            {
                report.addErrorWithID( "Appearance.IDDuplicate", "%#: <ItemVisual> has a duplicate id '%#'", sourceName, visual._id.c_str() );
                continue;
            }
            ItemVisualInternal::readVisual( node, visual, report, sourceName );
            _listVisual.push_back( visual );
        }
        return report.getErrors().size() == errorCountBefore;
    }

    const ItemVisualDef* ItemVisualCatalog::findVisual( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&ItemVisualDef::_id>( _listVisual, id );
    }
} // namespace sw
