#include "pch.h"

#include "Engine/Character/FitOperator.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/CharacterGeometry.h"
#include "Engine/Character/FitPartData.h"
#include "Engine/Character/FitSolver.h"
#include "Engine/Character/FitTables.h"

namespace sw
{
    namespace
    {
        struct FitOperatorInternal
        {
            static constexpr float32 kDefaultMargin         = 0.002f;
            static constexpr float32 kDefaultSearchDistance = 0.05f;
            static constexpr float32 kDefaultTolerance      = 0.001f;
            static constexpr float32 kRayStartOffset        = 1.0e-4f;

            template <size_t Count>
            static bool containsName( const hashed_string& name, const utf8* const ( &arrName )[Count] )
            {
                for ( const utf8* pName : arrName )
                {
                    if ( name == hashed_string( pName ) )
                        return true;
                }
                return false;
            }

            static float32 findArgument( const FitPairContext& pair, const utf8* pName, float32 fallback )
            {
                return pair._pInteraction == nullptr ? fallback : pair._pInteraction->findArgument( hashed_string( pName ), fallback );
            }

            /**
             * @brief 제약된 정점(양 > 0)의 양을 이웃으로 감쇠시켜 퍼뜨린다 — 가장 가까운 제약 정점의 양 × 감쇠(본 축을 따른 거리 / 폭).
             * @details 축이 없는 정점(스킨 없음)은 그냥 거리로 잰다. 부드럽게 오므라들거나 부풀게 하는 것이 목적이다.
             */
            static void spreadWithFalloff( const FitPartState& part, float32 falloffWidth, vector<float32>& inoutListAmount )
            {
                if ( falloffWidth <= 0.0f )
                    return;
                vector<uint32> listConstrained;
                for ( uint32 vertex = 0; vertex < inoutListAmount.size(); ++vertex )
                {
                    if ( inoutListAmount[vertex] > 0.0f )
                        listConstrained.push_back( vertex );
                }
                if ( listConstrained.empty() )
                    return;
                const vector<float32> listSource = inoutListAmount;
                for ( uint32 vertex = 0; vertex < inoutListAmount.size(); ++vertex )
                {
                    if ( listSource[vertex] > 0.0f )
                        continue;
                    const float3& position        = part._listBindPosition[vertex];
                    uint32        nearest         = listConstrained.front();
                    float32       nearestDistance = MathUtil::MaxFloat;
                    for ( const uint32 constrained : listConstrained )
                    {
                        const float32 distanceSquared = float3::getDistanceSquared( position, part._listBindPosition[constrained] );
                        if ( distanceSquared < nearestDistance )
                        {
                            nearestDistance = distanceSquared;
                            nearest         = constrained;
                        }
                    }
                    const float3& axis      = part._listFalloffAxis[vertex];
                    const float3  offset    = position - part._listBindPosition[nearest];
                    const float32 axialSpan = axis == float3::Zero ? offset.getLength() : MathUtil::abs( offset.dot( axis ) );
                    const float32 weight    = CharacterGeometryUtil::computeFalloff( axialSpan / falloffWidth );
                    inoutListAmount[vertex] = listSource[nearest] * weight;
                }
            }
        };

        /** @brief 조임 — 단단한 바깥 겹의 안면(또는 손으로 적은 고리) 밖에 있는 부드러운 안쪽 정점을 안으로 당긴다. 본 축을 따라 감쇠. */
        class FitShrinkOperator final : public IFitOperator
        {
        public:
            const hashed_string& getName() const override
            {
                static const hashed_string s_name( "Shrink" );
                return s_name;
            }
            FitPhase getPhase() const override { return FitPhase::Deform; }
            bool     acceptsArgument( const hashed_string& name ) const override
            {
                static constexpr const utf8* kArrArgument[] = { "strength", "margin", "searchDistance", "falloffWidth" };
                return FitOperatorInternal::containsName( name, kArrArgument );
            }

            void execute( FitSolveState& state, const FitPairContext& pair ) const override
            {
                const FitPartState& inner    = state._listPart[pair._innerPart];
                const FitPartState& outer    = state._listPart[pair._outerPart];
                const float32       softness = 1.0f - inner._pProfile->_rigidity;
                if ( softness <= 0.0f )
                    return;
                const float32             strength     = FitOperatorInternal::findArgument( pair, "strength", inner._pProfile->_shrinkStrength ) * softness;
                const float32             margin       = FitOperatorInternal::findArgument( pair, "margin", FitOperatorInternal::kDefaultMargin );
                const float32             search       = FitOperatorInternal::findArgument( pair, "searchDistance", FitOperatorInternal::kDefaultSearchDistance );
                const float32             falloffWidth = FitOperatorInternal::findArgument( pair, "falloffWidth", inner._pProfile->_falloffWidth );
                const vector<FitRingDef>& listRing     = outer._pInput->_pFitData->getRings();
                if ( listRing.empty() == false )
                {
                    for ( const FitRingDef& ring : listRing )
                    {
                        applyRing( state, pair._innerPart, ring, strength, margin, search, falloffWidth );
                    }
                    return;
                }
                applySurface( state, pair, strength, margin, search, falloffWidth );
            }

        private:
            static void applyRing( FitSolveState& state, uint32 innerIndex, const FitRingDef& ring, float32 strength, float32 margin, float32 search, float32 falloffWidth )
            {
                const FitPartState& inner      = state._listPart[innerIndex];
                float4x4            boneMatrix = float4x4::Identity;
                if ( ring._bone.empty() == false && state._pBindBones != nullptr )
                {
                    const int32 boneIndex = state._pBindBones->findBone( ring._bone );
                    if ( boneIndex >= 0 )
                        boneMatrix = state._pBindBones->_listModel[static_cast<size_t>( boneIndex )];
                }
                const float3  center       = float3::transform( ring._offset, boneMatrix );
                const float3  axis         = CharacterGeometryUtil::makeUnitOr( float3::transformVector( ring._axis, boneMatrix ), float3::UnitY );
                const float32 targetRadius = MathUtil::max( 0.0f, ring._radius - margin );
                const float32 halfWidth    = ring._width * 0.5f;
                for ( uint32 vertex = 0; vertex < inner._listBindPosition.size(); ++vertex )
                {
                    // 바인드 자리에서 목표를 정하고 지금 자리와의 차를 낸다 — 야코비 반복이 몇 번이든 같은 곳에 멈춘다.
                    const float3& bindPosition = inner._listBindPosition[vertex];
                    const float3  relative     = bindPosition - center;
                    const float32 axial        = relative.dot( axis );
                    const float3  radial       = relative - axis * axial;
                    const float32 radius       = radial.getLength();
                    if ( radius <= targetRadius || radius > ring._radius + search )
                        continue;
                    const float32 axialOutside = MathUtil::max( 0.0f, MathUtil::abs( axial ) - halfWidth );
                    const float32 weight       = falloffWidth > 0.0f ? CharacterGeometryUtil::computeFalloff( axialOutside / falloffWidth ) : ( axialOutside > 0.0f ? 0.0f : 1.0f );
                    if ( weight <= 0.0f )
                        continue;
                    const float32 newRadius  = MathUtil::lerp( radius, targetRadius, MathUtil::saturate( weight * strength ) );
                    const float3  desired    = bindPosition + radial * ( ( newRadius - radius ) / radius );
                    const float3  correction = desired - inner._listPosition[vertex];
                    if ( correction.getLengthSquared() > 1.0e-14f )
                        state.addDisplacement( innerIndex, vertex, correction );
                }
            }

            static void applySurface( FitSolveState& state, const FitPairContext& pair, float32 strength, float32 margin, float32 search, float32 falloffWidth )
            {
                const FitPartState&    inner   = state._listPart[pair._innerPart];
                const SurfaceBvh&      surface = state.getLayerSurface( pair._outerPart );
                vector<float32>        listAmount( inner._listPosition.size(), 0.0f );
                vector<GeometryRayHit> listHit;
                for ( uint32 vertex = 0; vertex < inner._listPosition.size(); ++vertex )
                {
                    // 안쪽(법선 반대)으로 쏴서 바깥 부품을 지나는 가장 깊은 면이 바깥 겹의 안면이다 — 그보다 안으로 당긴다.
                    const float3 inward = -inner._listNormal[vertex];
                    surface.collectHits( inner._listPosition[vertex], inward, search, listHit, static_cast<uint16>( pair._outerPart ) );
                    if ( listHit.empty() )
                        continue;
                    listAmount[vertex] = listHit.back()._distance + margin;
                }
                FitOperatorInternal::spreadWithFalloff( inner, falloffWidth, listAmount );
                for ( uint32 vertex = 0; vertex < listAmount.size(); ++vertex )
                {
                    if ( listAmount[vertex] > 0.0f )
                        state.addDisplacement( pair._innerPart, vertex, -inner._listNormal[vertex] * ( listAmount[vertex] * strength ) );
                }
            }
        };

        /** @brief 밀어내기 — 단단한 안쪽 겹 위의 부드러운 바깥 겹(갑옷 위 망토)을 바깥으로 민다. */
        class FitPushOperator final : public IFitOperator
        {
        public:
            const hashed_string& getName() const override
            {
                static const hashed_string s_name( "Push" );
                return s_name;
            }
            FitPhase getPhase() const override { return FitPhase::Deform; }
            bool     acceptsArgument( const hashed_string& name ) const override
            {
                static constexpr const utf8* kArrArgument[] = { "strength", "distance", "searchDistance", "falloffWidth" };
                return FitOperatorInternal::containsName( name, kArrArgument );
            }

            void execute( FitSolveState& state, const FitPairContext& pair ) const override
            {
                const FitPartState& outer    = state._listPart[pair._outerPart];
                const float32       softness = 1.0f - outer._pProfile->_rigidity;
                if ( softness <= 0.0f )
                    return;
                const float32     strength     = FitOperatorInternal::findArgument( pair, "strength", 1.0f ) * softness;
                const float32     distance     = FitOperatorInternal::findArgument( pair, "distance", outer._pProfile->_pushDistance );
                const float32     search       = FitOperatorInternal::findArgument( pair, "searchDistance", FitOperatorInternal::kDefaultSearchDistance );
                const float32     falloffWidth = FitOperatorInternal::findArgument( pair, "falloffWidth", outer._pProfile->_falloffWidth );
                const SurfaceBvh& surface      = state.getLayerSurface( pair._innerPart );
                const uint16      innerFilter  = static_cast<uint16>( pair._innerPart );
                vector<float32>   listAmount( outer._listPosition.size(), 0.0f );
                for ( uint32 vertex = 0; vertex < outer._listPosition.size(); ++vertex )
                {
                    const float3&  position = outer._listPosition[vertex];
                    const float3&  normal   = outer._listNormal[vertex];
                    GeometryRayHit hit;
                    // 바깥으로 쏴서 안쪽 부품의 뒷면에 맞으면 그 안에 묻힌 것이다.
                    if ( surface.findNearestHit( position, normal, search, hit, innerFilter ) && hit._bFrontFace == SW_FALSE )
                    {
                        listAmount[vertex] = hit._distance + distance;
                        continue;
                    }
                    // 밖에 있어도 거리보다 가까우면 그만큼 띄운다.
                    if ( surface.findNearestHit( position, -normal, distance, hit, innerFilter ) && hit._bFrontFace == SW_TRUE )
                        listAmount[vertex] = distance - hit._distance;
                }
                FitOperatorInternal::spreadWithFalloff( outer, falloffWidth, listAmount );
                for ( uint32 vertex = 0; vertex < listAmount.size(); ++vertex )
                {
                    if ( listAmount[vertex] > 0.0f )
                        state.addDisplacement( pair._outerPart, vertex, outer._listNormal[vertex] * ( listAmount[vertex] * strength ) );
                }
            }
        };

        /** @brief 잘라 내기 — 안쪽 정점에서 법선 방향으로 쏜 광선이 판정 거리 안에서 바깥 부품에 맞으면 덮임, 세 정점이 덮이면 삼각형을 표시한다. */
        class FitCutOperator final : public IFitOperator
        {
        public:
            const hashed_string& getName() const override
            {
                static const hashed_string s_name( "Cut" );
                return s_name;
            }
            FitPhase getPhase() const override { return FitPhase::Coverage; }
            bool     acceptsArgument( const hashed_string& name ) const override
            {
                static constexpr const utf8* kArrArgument[] = { "distance" };
                return FitOperatorInternal::containsName( name, kArrArgument );
            }

            void execute( FitSolveState& state, const FitPairContext& pair ) const override
            {
                FitPartState&             inner    = state._listPart[pair._innerPart];
                const float32             distance = FitOperatorInternal::findArgument( pair, "distance", inner._pProfile->_coverageDistance );
                const SurfaceBvh&         surface  = state.getLayerSurface( pair._outerPart );
                const AppearanceGeometry& geometry = *inner._pInput->_pGeometry;
                vector<uint8>             listVertexCovered( inner._listPosition.size(), SW_FALSE );
                for ( uint32 vertex = 0; vertex < inner._listPosition.size(); ++vertex )
                {
                    const float3&  normal = inner._listNormal[vertex];
                    const float3   origin = inner._listPosition[vertex] + normal * FitOperatorInternal::kRayStartOffset;
                    GeometryRayHit hit;
                    if ( surface.findNearestHit( origin, normal, distance, hit, static_cast<uint16>( pair._outerPart ) ) )
                        listVertexCovered[vertex] = SW_TRUE;
                }
                for ( uint32 triangle = 0; triangle < geometry.getTriangleCount(); ++triangle )
                {
                    const bool bCovered = listVertexCovered[geometry._listIndex[triangle * 3]] == SW_TRUE && listVertexCovered[geometry._listIndex[triangle * 3 + 1]] == SW_TRUE &&
                                          listVertexCovered[geometry._listIndex[triangle * 3 + 2]] == SW_TRUE;
                    if ( bCovered )
                        inner._listCovered[triangle] = SW_TRUE;
                }
            }
        };

        /** @brief 보고 — 두 부품이 서로 파고든 정점을 센다(가장 가까운 면의 뒤에 있으면 안). 고치지 않는다. */
        class FitReportOperator final : public IFitOperator
        {
        public:
            const hashed_string& getName() const override
            {
                static const hashed_string s_name( "Report" );
                return s_name;
            }
            FitPhase getPhase() const override { return FitPhase::Validate; }
            bool     acceptsArgument( const hashed_string& name ) const override
            {
                static constexpr const utf8* kArrArgument[] = { "tolerance", "searchDistance" };
                return FitOperatorInternal::containsName( name, kArrArgument );
            }

            void execute( FitSolveState& state, const FitPairContext& pair ) const override
            {
                const float32  tolerance = FitOperatorInternal::findArgument( pair, "tolerance", FitOperatorInternal::kDefaultTolerance );
                const float32  search    = FitOperatorInternal::findArgument( pair, "searchDistance", FitOperatorInternal::kDefaultSearchDistance );
                FitPenetration penetration;
                penetration._partA = pair._innerPart;
                penetration._partB = pair._outerPart;
                countInside( state, pair._outerPart, pair._innerPart, tolerance, search, penetration );
                countInside( state, pair._innerPart, pair._outerPart, tolerance, search, penetration );
                if ( penetration._vertexCount == 0 || state._pReport == nullptr )
                    return;
                state._pReport->_listPenetration.push_back( penetration );
                string message( "parts '" );
                message += state._listPart[pair._innerPart]._pInput->_name.view();
                message += "' and '";
                message += state._listPart[pair._outerPart]._pInput->_name.view();
                message += "' penetrate (rigid against rigid is not resolved automatically)";
                state._pReport->_listMessage.push_back( std::move( message ) );
            }

        private:
            static void countInside( const FitSolveState& state, uint32 testedPart, uint32 surfacePart, float32 tolerance, float32 search, FitPenetration& inoutPenetration )
            {
                const FitPartState& tested  = state._listPart[testedPart];
                const SurfaceBvh&   surface = state.getLayerSurface( surfacePart );
                for ( const float3& position : tested._listPosition )
                {
                    GeometryClosestPoint closest;
                    if ( surface.findClosestPoint( position, search, closest, static_cast<uint16>( surfacePart ) ) == false )
                        continue;
                    const float32 signedDistance = ( position - closest._point ).dot( closest._normal );
                    if ( signedDistance >= -tolerance )
                        continue;
                    ++inoutPenetration._vertexCount;
                    inoutPenetration._maxDepth = MathUtil::max( inoutPenetration._maxDepth, -signedDistance );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FitOperatorRegistry::FitOperatorRegistry()
        : _listOperator{}
    {
    }

    FitOperatorRegistry::~FitOperatorRegistry() = default;

    bool FitOperatorRegistry::registerOperator( unique_ptr<IFitOperator> fitOperator )
    {
        if ( fitOperator == nullptr || findOperator( fitOperator->getName() ) != nullptr )
            return false;
        _listOperator.push_back( std::move( fitOperator ) );
        return true;
    }

    const IFitOperator* FitOperatorRegistry::findOperator( const hashed_string& name ) const
    {
        for ( const unique_ptr<IFitOperator>& fitOperator : _listOperator )
        {
            if ( fitOperator->getName() == name )
                return fitOperator.get();
        }
        return nullptr;
    }

    void FitOperatorRegistry::registerDefaultOperators()
    {
        (void)registerOperator( make_unique<FitShrinkOperator>() );
        (void)registerOperator( make_unique<FitPushOperator>() );
        (void)registerOperator( make_unique<FitCutOperator>() );
        (void)registerOperator( make_unique<FitReportOperator>() );
    }
} // namespace sw
