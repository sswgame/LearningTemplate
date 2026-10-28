/**
 * @file MeshModifierGPUProgram.cpp
 * @brief 모디파이어 스택을 GPU 변형 프로그램으로 바꿉니다 — 종류 표, 기록(종류 · 상자 · 공간 행렬 · 매개변수) 쓰기.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierGPUProgram.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierStack.h"

namespace sw::editor
{
    namespace
    {
        struct MeshModifierGPUProgramInternal
        {
            /** @brief 셰이더 `SW_MODIFIER_*` 번호 순서입니다(번호 = 배열 위치). 바꾸면 meshmodifier.hlsli 도 같이 바꾼다. */
            static constexpr const utf8* kArrKindTypeID[] = { "bend", "twist", "taper", "stretch", "wave", "ripple", "noise", "melt",
                                                              "push", "poke", "displace", "bubble", "bulge", "cylindrify", "globe", "hump",
                                                              "radialskew", "skew", "spherify", "squeeze", "scale", "sinuscurve", "rolled", "simplemod",
                                                              "crumple", "dynamicripple", "pageflip", "treebend", "vertnoise", "zstretchnoise", "waving", "pivotadjust",
                                                              "displacelimits", "bezierattractor", "rubber", "curvedeform", "curvesculpt",
                                                              "curvesculptlayered", "ropedeform", "pathdeform", "worldpathdeform", "displacerendertarget", "displacewebcam",
                                                              "lattice", "ffd2", "ffd3", "ffd4", "ffd2d", "collisiondeform", "hitdeform",
                                                              "conform", "conformmulti", "pointcache", "vertexanim", "paint" };

            /** @brief 행렬의 행 0..@p rowCount-1 을 xyz 로 붙입니다(행 벡터 규약 v·M, 행 3 은 평행 이동). */
            static void pushRows( const float4x4& matrix, uint32 rowCount, vector<float4>& outListProgram )
            {
                const float32 arrRow[4][3] = {
                    {matrix._11, matrix._12, matrix._13},
                    {matrix._21, matrix._22, matrix._23},
                    {matrix._31, matrix._32, matrix._33},
                    {matrix._41, matrix._42, matrix._43}
                };
                for ( uint32 row = 0; row < rowCount; ++row )
                {
                    outListProgram.push_back( float4{ arrRow[row][0], arrRow[row][1], arrRow[row][2], 0.0f } );
                }
            }

            /** @brief 프로그램을 쓰는 동안의 상태입니다. 입력 상자는 CPU 적용이 깊이 우선으로 모은 순서(묶음 자신 → 하위)로 하나씩 소비한다. */
            struct Emitter
            {
                const vector<MeshModifierBounds>* _pBounds{ nullptr };
                vector<float4>*                   _pProgram{ nullptr };
                size_t                            _cursor{ 0 };
                uint32                            _vertexCount{ 0 };
                uint32                            _recordCount{ 0 };
                bool                              _bNeedsUV{ false };
            };

            /** @brief 스택(과 그 안의 묶음)이 GPU 로 옮겨지는지 봅니다. 막는 첫 최상위 번호를 받습니다(하위가 막으면 그 묶음 번호). */
            static bool canMakeStack( const MeshModifierStack& stack, int32* pOutBlockingIndex )
            {
                if ( stack.isRecomputingNormals() )
                    return false; // 면 법선 재계산은 이웃 정점이 필요하다 - 정점 하나씩 도는 컴퓨트로는 못 한다
                for ( int32 index = 0; index < stack.getModifierCount(); ++index )
                {
                    const MeshModifier* pModifier = stack.findModifier( index );
                    if ( pModifier == nullptr || pModifier->isEnabled() == false )
                        continue;
                    const MeshModifierStack* pChild     = pModifier->getGPUChildStack();
                    bool                     bSupported = pModifier->supportsGPU();
                    if ( pChild != nullptr )
                        bSupported = bSupported && canMakeStack( *pChild, nullptr ); // 묶음은 기록을 싣지 않는다 - 하위가 모두 옮겨지면 된다
                    else
                        bSupported = bSupported && MeshModifierGPUProgram::findKind( pModifier->getTypeID() ) >= 0;
                    if ( bSupported == false )
                    {
                        if ( pOutBlockingIndex != nullptr )
                            *pOutBlockingIndex = index;
                        return false;
                    }
                }
                return true;
            }

            /**
             * @brief 스택의 켜진 모디파이어를 기록으로 이어 붙입니다. 묶음은 기록이 없고 하위 스택을 같은 자리에서 잇는다.
             * @param localToLevel 메시 로컬 → 이 단계의 "메시 로컬"(바깥 묶음들의 로컬 → 묶음 행렬의 곱). 최상위는 단위 행렬이다.
             * @details 묶음 G 안의 모디파이어 하나는 CPU 에서 p → p·G → (하위 S) → … → ·G⁻¹ 로 걸리는데, 이웃 두 기록 사이에서 G⁻¹ · G 가 약분되므로
             *          기록의 로컬 → 모디파이어 행렬을 `localToLevel · S` 로 쓰기만 하면 같은 결과다(법선도 같은 행렬의 역전치).
             */
            static bool appendStack( const MeshModifierStack& stack, const MeshModifierEvaluation& evaluation, const float4x4& localToLevel, Emitter& emitter )
            {
                vector<float4>& program = *emitter._pProgram;
                for ( int32 index = 0; index < stack.getModifierCount(); ++index )
                {
                    const MeshModifier* pModifier = stack.findModifier( index );
                    if ( pModifier == nullptr || pModifier->isEnabled() == false )
                        continue;
                    if ( emitter._cursor >= emitter._pBounds->size() )
                        return false;
                    // MeshModifier::apply 와 같은 행렬 - 공간은 거기 한 곳의 규칙을 그대로 옮긴다
                    const MeshModifierBounds& bounds          = ( *emitter._pBounds )[emitter._cursor++];
                    const float4x4            levelToModifier = pModifier->computeLocalToModifier( evaluation );
                    const float4x4            modifierToLevel = levelToModifier.invert();
                    const MeshModifierStack*  pChild          = pModifier->getGPUChildStack();
                    if ( pChild != nullptr )
                    {
                        MeshModifierEvaluation childEvaluation = evaluation;
                        childEvaluation._meshToWorld           = modifierToLevel * evaluation._meshToWorld; // 하위 스택에게는 묶음 공간이 메시 로컬이다
                        if ( appendStack( *pChild, childEvaluation, localToLevel * levelToModifier, emitter ) == false )
                            return false;
                        continue;
                    }
                    const float4x4 localToModifier       = localToLevel * levelToModifier;
                    const float4x4 modifierToLocal       = localToModifier.invert();
                    const float4x4 localToModifierNormal = modifierToLocal.transpose(); // 법선은 역전치로 옮긴다
                    const float4x4 modifierToLocalNormal = localToModifier.transpose();

                    const size_t recordStart = program.size();
                    program.push_back( float4{ static_cast<float32>( MeshModifierGPUProgram::findKind( pModifier->getTypeID() ) ), 0.0f,
                                               static_cast<float32>( MathUtil::min( pModifier->getGizmo()._axisIndex, 2u ) ), MeshModifierUtil::computeJacobianStep( bounds ) } );
                    program.push_back( float4{ bounds._min._x, bounds._min._y, bounds._min._z, bounds.isValid() ? 1.0f : 0.0f } );
                    program.push_back( float4{ bounds._max._x, bounds._max._y, bounds._max._z, 0.0f } );
                    pushRows( localToModifier, 4, program );
                    pushRows( modifierToLocal, 4, program );
                    pushRows( localToModifierNormal, 3, program );
                    pushRows( modifierToLocalNormal, 3, program );
                    SW_ASSERT( program.size() - recordStart == MeshModifierGPUProgram::kRecordFloat4Count );

                    const size_t paramStart = program.size();
                    (void)pModifier->packGPUParams( program );
                    // 외부 입력은 매개변수 뒤에 인라인으로 싣는다 - CPU `apply` 가 만드는 것과 같은 문맥(상자 · 주축 · 월드 ↔ 모디파이어 행렬)에서 얻는다
                    MeshModifierContext context{};
                    context._pEvaluation     = &evaluation;
                    context._bounds          = bounds;
                    context._axisIndex       = MathUtil::min( pModifier->getGizmo()._axisIndex, 2u );
                    context._vertexCount     = emitter._vertexCount;
                    context._modifierToWorld = modifierToLevel * evaluation._meshToWorld;
                    context._worldToModifier = context._modifierToWorld.invert();
                    if ( pModifier->packGPUInputs( context, program ) == false )
                        return false;
                    emitter._bNeedsUV       = emitter._bNeedsUV || pModifier->needsGPUVertexUV();
                    program[recordStart]._y = static_cast<float32>( program.size() - paramStart );
                    ++emitter._recordCount;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    int32 MeshModifierGPUProgram::findKind( string_view typeID )
    {
        for ( uint32 kind = 0; kind < getKindCount(); ++kind )
        {
            if ( typeID == string_view( MeshModifierGPUProgramInternal::kArrKindTypeID[kind] ) )
                return static_cast<int32>( kind );
        }
        return -1;
    }

    uint32 MeshModifierGPUProgram::getKindCount()
    {
        return static_cast<uint32>( sizeof( MeshModifierGPUProgramInternal::kArrKindTypeID ) / sizeof( MeshModifierGPUProgramInternal::kArrKindTypeID[0] ) );
    }

    string_view MeshModifierGPUProgram::getKindTypeID( uint32 kind )
    {
        return kind < getKindCount() ? string_view( MeshModifierGPUProgramInternal::kArrKindTypeID[kind] ) : string_view{};
    }

    bool MeshModifierGPUProgram::canMake( const MeshModifierStack& stack, int32* pOutBlockingIndex )
    {
        if ( pOutBlockingIndex != nullptr )
            *pOutBlockingIndex = -1;
        return MeshModifierGPUProgramInternal::canMakeStack( stack, pOutBlockingIndex );
    }

    bool MeshModifierGPUProgram::make( const MeshModifierStack& stack, const MeshModifierEvaluation& evaluation, const vector<MeshModifierBounds>& listInputBounds,
                                       bool bHasWeight, uint32 vertexCount, const vector<float2>* pListUV, vector<float4>& outProgram )
    {
        outProgram.clear();
        if ( bHasWeight || canMake( stack ) == false )
            return false;

        outProgram.push_back( float4{ 0.0f, evaluation._time, 0.0f, 0.0f } );
        MeshModifierGPUProgramInternal::Emitter emitter{};
        emitter._pBounds     = &listInputBounds;
        emitter._pProgram    = &outProgram;
        emitter._vertexCount = vertexCount;
        const float4x4 identity{};
        // 입력 상자는 켜진 모디파이어(묶음 포함)마다 하나씩 깊이 우선으로 모였다 - 다 소비해야 스택과 상자가 같은 것이다
        if ( MeshModifierGPUProgramInternal::appendStack( stack, evaluation, identity, emitter ) == false || emitter._cursor != listInputBounds.size() )
        {
            outProgram.clear();
            return false;
        }
        outProgram[0]._x = static_cast<float32>( emitter._recordCount );
        if ( emitter._bNeedsUV )
        {
            // 정점 UV 표 - 프로그램 맨 끝, float4 하나에 UV 둘. 머리 z 가 표의 시작(프로그램 시작 기준 float4 번호)이고 w 가 정점 수다.
            // 셰이더는 풀 안의 메시 정점 번호(칸 표 y 의 구간 시작을 뺀 값)로 읽는다.
            if ( pListUV == nullptr || pListUV->empty() )
            {
                outProgram.clear();
                return false;
            }
            outProgram[0]._z = static_cast<float32>( outProgram.size() );
            outProgram[0]._w = static_cast<float32>( pListUV->size() );
            for ( size_t vertex = 0; vertex < pListUV->size(); vertex += 2 )
            {
                const float2& first  = ( *pListUV )[vertex];
                const float2& second = vertex + 1 < pListUV->size() ? ( *pListUV )[vertex + 1] : first;
                outProgram.push_back( float4{ first._x, first._y, second._x, second._y } );
            }
        }
        return true;
    }
} // namespace sw::editor
