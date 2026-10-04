#include "pch.h"

#include "Engine/Character/FitSolver.h"

#include "Engine/Character/CharacterGeometry.h"
#include "Engine/Character/FitPartData.h"
#include "Engine/Character/FitTables.h"

namespace sw
{
    namespace
    {
        struct FitSolverInternal
        {
            static constexpr uint32 kDefaultIterationCount = 3;

            /** @brief 짝 (안쪽 @p inner, 바깥 @p outer)이 성립하는가 — 순서가 작은 쪽이 안쪽, 같으면 입력 앞이 안쪽. */
            static bool isInnerOuterPair( const FitSolveState& state, uint32 inner, uint32 outer )
            {
                if ( inner == outer )
                    return false;
                const int32 innerOrder = state._listPart[inner]._layerOrder;
                const int32 outerOrder = state._listPart[outer]._layerOrder;
                return innerOrder < outerOrder || ( innerOrder == outerOrder && inner < outer );
            }

            /** @brief 정점마다 감쇠 축 — 주 본에서 첫 자식(없으면 부모에서 그 본)으로. 스킨이 없으면 0. */
            static void computeFalloffAxes( const AppearanceGeometry& geometry, const CharacterBoneArray* pBones, vector<float3>& outListAxis )
            {
                outListAxis.assign( geometry.getVertexCount(), float3::Zero );
                if ( pBones == nullptr || geometry.isSkinned() == false )
                    return;
                const uint32   boneCount = pBones->getBoneCount();
                vector<float3> listBoneAxis( boneCount, float3::Zero );
                for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
                {
                    const float3 bonePosition = pBones->_listModel[boneIndex].getTranslation();
                    for ( uint32 childIndex = boneIndex + 1; childIndex < boneCount; ++childIndex )
                    {
                        if ( pBones->_listParent[childIndex] != static_cast<int32>( boneIndex ) )
                            continue;
                        listBoneAxis[boneIndex] = CharacterGeometryUtil::makeUnitOr( pBones->_listModel[childIndex].getTranslation() - bonePosition, float3::Zero );
                        break;
                    }
                    const int32 parentIndex = pBones->_listParent[boneIndex];
                    if ( listBoneAxis[boneIndex] == float3::Zero && parentIndex >= 0 )
                        listBoneAxis[boneIndex] = CharacterGeometryUtil::makeUnitOr( bonePosition - pBones->_listModel[static_cast<size_t>( parentIndex )].getTranslation(), float3::Zero );
                }
                for ( uint32 vertex = 0; vertex < geometry.getVertexCount(); ++vertex )
                {
                    const int32 joint = geometry._listSkin[vertex].findDominantJoint();
                    if ( 0 <= joint && joint < static_cast<int32>( boneCount ) )
                        outListAxis[vertex] = listBoneAxis[static_cast<size_t>( joint )];
                }
            }

            static float32 findMorphWeight( const FitInput& input, const hashed_string& morph )
            {
                if ( morph.empty() )
                    return 1.0f;
                for ( const BodyMorphWeight& morphWeight : input._listMorphWeight )
                {
                    if ( morphWeight._morph == morph )
                        return morphWeight._weight;
                }
                return 0.0f;
            }

            static void rebuildMovedSurfaces( FitSolveState& state )
            {
                vector<uint8> listLayerDirty( state._listLayerSurface.size(), SW_FALSE );
                for ( FitPartState& part : state._listPart )
                {
                    if ( part._bMoved == SW_FALSE )
                        continue;
                    state._listLayerSurface[part._layerIndex].setSurfacePositions( part._surfaceIndex, part._listPosition );
                    listLayerDirty[part._layerIndex] = SW_TRUE;
                    part._bMoved                     = SW_FALSE;
                }
                for ( size_t layerIndex = 0; layerIndex < listLayerDirty.size(); ++layerIndex )
                {
                    if ( listLayerDirty[layerIndex] == SW_TRUE )
                        state._listLayerSurface[layerIndex].build();
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void FitPartResult::initialize( uint32 triangleCount, uint32 vertexCount )
    {
        _triangleCount    = triangleCount;
        _cutTriangleCount = 0;
        _listVisibleBit.assign( ( triangleCount + 31 ) / 32, 0xFFFFFFFFu );
        const uint32 tailBits = triangleCount % 32;
        if ( tailBits != 0 )
            _listVisibleBit.back() &= ( 1u << tailBits ) - 1u;
        _listVertexDelta.assign( vertexCount, float3::Zero );
    }

    bool FitPartResult::isTriangleVisible( uint32 triangle ) const
    {
        const uint32 word = triangle / 32;
        if ( word >= _listVisibleBit.size() )
            return false;
        return ( _listVisibleBit[word] & ( 1u << ( triangle % 32 ) ) ) != 0;
    }

    void FitPartResult::setTriangleVisible( uint32 triangle, bool bVisible )
    {
        const uint32 word = triangle / 32;
        if ( word >= _listVisibleBit.size() )
            return;
        const uint32 bit = 1u << ( triangle % 32 );
        if ( bVisible )
            _listVisibleBit[word] |= bit;
        else
            _listVisibleBit[word] &= ~bit;
    }

    uint32 FitPartResult::hideTriangles( vector_reference<const uint8> listTriangleHidden )
    {
        uint32 hiddenCount = 0;
        for ( uint32 triangle = 0; triangle < _triangleCount && triangle < listTriangleHidden.size(); ++triangle )
        {
            if ( listTriangleHidden[triangle] == 0 || isTriangleVisible( triangle ) == false )
                continue;
            setTriangleVisible( triangle, false );
            ++hiddenCount;
        }
        _cutTriangleCount += hiddenCount;
        return hiddenCount;
    }
} // namespace sw

namespace sw
{
    void FitSolveState::addDisplacement( uint32 part, uint32 vertex, const float3& displacement )
    {
        FitPartState& partState = _listPart[part];
        partState._listDisplacement[vertex] += displacement;
        ++partState._listDisplacementCount[vertex];
    }
} // namespace sw

namespace sw
{
    FitSolver::FitSolver()
        : _operatorRegistry{}
        , _iterationCount{ FitSolverInternal::kDefaultIterationCount }
    {
        _operatorRegistry.registerDefaultOperators();
    }

    bool FitSolver::prepareState( const FitTables& tables, const FitInput& input, FitSolveState& outState, FitReport& outReport ) const
    {
        bool bValid = true;
        outState._listPart.resize( input._listPart.size() );
        vector<hashed_string> listLayerName;
        for ( uint32 partIndex = 0; partIndex < input._listPart.size(); ++partIndex )
        {
            const FitPartInput& partInput = input._listPart[partIndex];
            FitPartState&       part      = outState._listPart[partIndex];
            part._pInput                  = &partInput;
            if ( partInput._pGeometry == nullptr || partInput._pFitData == nullptr || partInput._pGeometry->isValid() == false )
            {
                outReport._listMessage.push_back( string( "part '" ) + partInput._name.c_str() + "' has no valid geometry or fit data" );
                bValid = false;
                continue;
            }
            const FitLayerDef* pLayer = tables.findLayer( partInput._pFitData->getLayer() );
            part._pProfile            = tables.findProfile( partInput._pFitData->getProfile() );
            if ( pLayer == nullptr || part._pProfile == nullptr )
            {
                outReport._listMessage.push_back( string( "part '" ) + partInput._name.c_str() + "' names a layer or profile the fit tables do not have" );
                bValid = false;
                continue;
            }
            part._layerOrder = pLayer->_order;
            size_t layerSlot = 0;
            while ( layerSlot < listLayerName.size() && listLayerName[layerSlot] != pLayer->_name )
            {
                ++layerSlot;
            }
            if ( layerSlot == listLayerName.size() )
                listLayerName.push_back( pLayer->_name );
            part._layerIndex = static_cast<uint32>( layerSlot );

            const AppearanceGeometry& geometry = *partInput._pGeometry;
            part._listBindPosition             = geometry._listPosition;
            part._listPosition                 = geometry._listPosition;
            if ( geometry._listNormal.size() == geometry._listPosition.size() )
                part._listNormal = geometry._listNormal;
            else
                CharacterGeometryUtil::computeVertexNormals( geometry._listPosition, geometry._listIndex, part._listNormal );
            part._listDisplacement.assign( geometry.getVertexCount(), float3::Zero );
            part._listDisplacementCount.assign( geometry.getVertexCount(), 0 );
            part._listCovered.assign( geometry.getTriangleCount(), SW_FALSE );
            part._listHidden.assign( geometry.getTriangleCount(), SW_FALSE );
            if ( input._pBindBones != nullptr )
                tables.assignRegions( geometry, *input._pBindBones, part._listVertexRegion );
            else
                part._listVertexRegion.assign( geometry.getVertexCount(), CharacterGeometryConstant::kNoGroup );
            FitSolverInternal::computeFalloffAxes( geometry, input._pBindBones, part._listFalloffAxis );
        }
        if ( bValid == false )
            return false;

        outState._listLayerSurface.clear();
        outState._listLayerSurface.resize( listLayerName.size() );
        for ( uint32 partIndex = 0; partIndex < outState._listPart.size(); ++partIndex )
        {
            FitPartState& part = outState._listPart[partIndex];
            part._surfaceIndex = outState._listLayerSurface[part._layerIndex].addSurface( static_cast<uint16>( partIndex ), part._listPosition,
                                                                                          part._pInput->_pGeometry->_listIndex );
        }
        for ( SurfaceBvh& surface : outState._listLayerSurface )
        {
            surface.build();
        }
        return true;
    }

    void FitSolver::runPhase( FitPhase phase, const FitTables& tables, FitSolveState& state ) const
    {
        vector<const FitInteractionDef*> listInteraction;
        const uint32                     partCount = static_cast<uint32>( state._listPart.size() );
        for ( uint32 inner = 0; inner < partCount; ++inner )
        {
            for ( uint32 outer = 0; outer < partCount; ++outer )
            {
                if ( FitSolverInternal::isInnerOuterPair( state, inner, outer ) == false )
                    continue;
                tables.collectInteractions( state._listPart[inner]._pProfile->_name, state._listPart[outer]._pProfile->_name, listInteraction );
                for ( const FitInteractionDef* pInteraction : listInteraction )
                {
                    const IFitOperator* pOperator = _operatorRegistry.findOperator( pInteraction->_operator );
                    if ( pOperator == nullptr || pOperator->getPhase() != phase )
                        continue;
                    FitPairContext pair;
                    pair._pInteraction = pInteraction;
                    pair._innerPart    = inner;
                    pair._outerPart    = outer;
                    pOperator->execute( state, pair );
                }
            }
        }
    }

    bool FitSolver::solve( const FitTables& tables, const FitInput& input, FitResult& outResult ) const
    {
        outResult._listPart.clear();
        outResult._report = FitReport{};
        for ( const FitInteractionDef& interaction : tables.getInteractions() )
        {
            if ( _operatorRegistry.findOperator( interaction._operator ) == nullptr )
            {
                outResult._report._listMessage.push_back( string( "fit tables name unknown operator '" ) + interaction._operator.c_str() + "'" );
                return false;
            }
        }
        FitSolveState state;
        state._pReport    = &outResult._report;
        state._pTables    = &tables;
        state._pBindBones = input._pBindBones;
        if ( prepareState( tables, input, state, outResult._report ) == false )
            return false;

        // 1) 변형 — 야코비: 짝마다 변위를 쌓고 정점마다 평균을 한 번에 적용한다.
        for ( uint32 iteration = 0; iteration < _iterationCount; ++iteration )
        {
            for ( FitPartState& part : state._listPart )
            {
                std::fill( part._listDisplacement.begin(), part._listDisplacement.end(), float3::Zero );
                std::fill( part._listDisplacementCount.begin(), part._listDisplacementCount.end(), 0u );
            }
            runPhase( FitPhase::Deform, tables, state );
            bool bAnyMoved = false;
            for ( FitPartState& part : state._listPart )
            {
                for ( size_t vertex = 0; vertex < part._listPosition.size(); ++vertex )
                {
                    const uint32 count = part._listDisplacementCount[vertex];
                    if ( count == 0 )
                        continue;
                    const float3 displacement = part._listDisplacement[vertex] * ( 1.0f / static_cast<float32>( count ) );
                    if ( displacement.getLengthSquared() <= 1.0e-14f )
                        continue;
                    part._listPosition[vertex] += displacement;
                    part._bMoved = SW_TRUE;
                    bAnyMoved    = true;
                }
            }
            if ( bAnyMoved == false )
                break;
            FitSolverInternal::rebuildMovedSurfaces( state );
        }

        // 2) 손 보정 조각 — 체형 모프 가중치만큼.
        for ( FitPartState& part : state._listPart )
        {
            for ( const FitCorrectiveDef& corrective : part._pInput->_pFitData->getCorrectives() )
            {
                const float32 weight = FitSolverInternal::findMorphWeight( input, corrective._morph );
                if ( weight == 0.0f )
                    continue;
                for ( const FitCorrectiveDelta& delta : corrective._listDelta )
                {
                    if ( delta._vertex >= part._listPosition.size() )
                        continue;
                    part._listPosition[delta._vertex] += delta._offset * weight;
                    part._bMoved = SW_TRUE;
                }
            }
        }
        FitSolverInternal::rebuildMovedSurfaces( state );

        // 3) 덮임 — 변형 뒤의 자리로. 숨김 영역은 바깥 부품이 안쪽 부품에 적는다.
        runPhase( FitPhase::Coverage, tables, state );
        const uint32 partCount = static_cast<uint32>( state._listPart.size() );
        for ( uint32 outer = 0; outer < partCount; ++outer )
        {
            const vector<hashed_string>& listHiddenRegion = state._listPart[outer]._pInput->_pFitData->getHiddenRegions();
            if ( listHiddenRegion.empty() )
                continue;
            vector<uint16> listHiddenIndex;
            for ( const hashed_string& region : listHiddenRegion )
            {
                const int32 regionIndex = tables.findRegionIndex( region );
                if ( regionIndex >= 0 )
                    listHiddenIndex.push_back( static_cast<uint16>( regionIndex ) );
            }
            for ( uint32 inner = 0; inner < partCount; ++inner )
            {
                if ( FitSolverInternal::isInnerOuterPair( state, inner, outer ) == false )
                    continue;
                FitPartState&             innerPart = state._listPart[inner];
                const AppearanceGeometry& geometry  = *innerPart._pInput->_pGeometry;
                for ( uint32 triangle = 0; triangle < geometry.getTriangleCount(); ++triangle )
                {
                    bool bAllHidden = true;
                    for ( uint32 corner = 0; corner < 3 && bAllHidden; ++corner )
                    {
                        const uint16 region = innerPart._listVertexRegion[geometry._listIndex[triangle * 3 + corner]];
                        bAllHidden          = std::find( listHiddenIndex.begin(), listHiddenIndex.end(), region ) != listHiddenIndex.end();
                    }
                    if ( bAllHidden )
                        innerPart._listHidden[triangle] = SW_TRUE;
                }
            }
        }

        // 4) 검증 — 고치지 않고 보고.
        runPhase( FitPhase::Validate, tables, state );

        // 5) 결과 — 덮였어도 덮이지 않은 삼각형과 정점을 나누면 남긴다(경계 한 겹).
        outResult._listPart.resize( partCount );
        for ( uint32 partIndex = 0; partIndex < partCount; ++partIndex )
        {
            const FitPartState&       part     = state._listPart[partIndex];
            const AppearanceGeometry& geometry = *part._pInput->_pGeometry;
            FitPartResult&            result   = outResult._listPart[partIndex];
            result._name                       = part._pInput->_name;
            result.initialize( geometry.getTriangleCount(), static_cast<uint32>( part._listPosition.size() ) );
            for ( size_t vertex = 0; vertex < part._listPosition.size(); ++vertex )
            {
                result._listVertexDelta[vertex] = part._listPosition[vertex] - part._listBindPosition[vertex];
            }
            vector<uint8> listTouchesUncovered( geometry.getVertexCount(), SW_FALSE );
            for ( uint32 triangle = 0; triangle < result._triangleCount; ++triangle )
            {
                if ( part._listCovered[triangle] == SW_TRUE )
                    continue;
                for ( uint32 corner = 0; corner < 3; ++corner )
                {
                    listTouchesUncovered[geometry._listIndex[triangle * 3 + corner]] = SW_TRUE;
                }
            }
            for ( uint32 triangle = 0; triangle < result._triangleCount; ++triangle )
            {
                bool bCut = part._listHidden[triangle] == SW_TRUE;
                if ( bCut == false && part._listCovered[triangle] == SW_TRUE )
                {
                    bCut = true;
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        if ( listTouchesUncovered[geometry._listIndex[triangle * 3 + corner]] == SW_TRUE )
                            bCut = false;
                    }
                }
                if ( bCut )
                {
                    result.setTriangleVisible( triangle, false );
                    ++result._cutTriangleCount;
                }
            }
        }
        return true;
    }
} // namespace sw
