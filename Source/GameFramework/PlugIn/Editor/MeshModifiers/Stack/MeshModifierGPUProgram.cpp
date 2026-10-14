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
                                                              "displacelimits" };

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
        if ( stack.isRecomputingNormals() )
            return false; // 면 법선 재계산은 이웃 정점이 필요하다 - 정점 하나씩 도는 컴퓨트로는 못 한다
        for ( int32 index = 0; index < stack.getModifierCount(); ++index )
        {
            const MeshModifier* pModifier = stack.findModifier( index );
            if ( pModifier == nullptr || pModifier->isEnabled() == false )
                continue;
            const bool bSupported = pModifier->supportsGPU() && findKind( pModifier->getTypeID() ) >= 0;
            if ( bSupported == false )
            {
                if ( pOutBlockingIndex != nullptr )
                    *pOutBlockingIndex = index;
                return false;
            }
        }
        return true;
    }

    bool MeshModifierGPUProgram::make( const MeshModifierStack& stack, const MeshModifierEvaluation& evaluation, const vector<MeshModifierBounds>& listInputBounds,
                                       bool bHasWeight, vector<float4>& outProgram )
    {
        outProgram.clear();
        if ( bHasWeight || canMake( stack ) == false )
            return false;

        outProgram.push_back( float4{ 0.0f, evaluation._time, 0.0f, 0.0f } );
        uint32 modifierCount = 0;
        for ( int32 index = 0; index < stack.getModifierCount(); ++index )
        {
            const MeshModifier* pModifier = stack.findModifier( index );
            if ( pModifier == nullptr || pModifier->isEnabled() == false )
                continue;
            if ( modifierCount >= listInputBounds.size() )
            {
                outProgram.clear();
                return false;
            }
            // MeshModifier::apply 와 같은 행렬 - 공간은 거기 한 곳의 규칙을 그대로 옮긴다
            const MeshModifierBounds& bounds                = listInputBounds[modifierCount];
            const float4x4            localToModifier       = pModifier->computeLocalToModifier( evaluation );
            const float4x4            modifierToLocal       = localToModifier.invert();
            const float4x4            localToModifierNormal = modifierToLocal.transpose();
            const float4x4            modifierToLocalNormal = localToModifier.transpose();

            const size_t recordStart = outProgram.size();
            outProgram.push_back( float4{ static_cast<float32>( findKind( pModifier->getTypeID() ) ), 0.0f, static_cast<float32>( MathUtil::min( pModifier->getGizmo()._axisIndex, 2u ) ),
                                          MeshModifierUtil::computeJacobianStep( bounds ) } );
            outProgram.push_back( float4{ bounds._min._x, bounds._min._y, bounds._min._z, bounds.isValid() ? 1.0f : 0.0f } );
            outProgram.push_back( float4{ bounds._max._x, bounds._max._y, bounds._max._z, 0.0f } );
            MeshModifierGPUProgramInternal::pushRows( localToModifier, 4, outProgram );
            MeshModifierGPUProgramInternal::pushRows( modifierToLocal, 4, outProgram );
            MeshModifierGPUProgramInternal::pushRows( localToModifierNormal, 3, outProgram );
            MeshModifierGPUProgramInternal::pushRows( modifierToLocalNormal, 3, outProgram );
            SW_ASSERT( outProgram.size() - recordStart == kRecordFloat4Count );

            const size_t paramStart = outProgram.size();
            (void)pModifier->packGPUParams( outProgram );
            outProgram[recordStart]._y = static_cast<float32>( outProgram.size() - paramStart );
            ++modifierCount;
        }
        if ( modifierCount != listInputBounds.size() )
        {
            outProgram.clear();
            return false;
        }
        outProgram[0]._x = static_cast<float32>( modifierCount );
        return true;
    }
} // namespace sw::editor
