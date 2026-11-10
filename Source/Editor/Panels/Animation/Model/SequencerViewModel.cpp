#include "pch.h"

#include "Editor/Panels/Animation/Model/SequencerViewModel.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

namespace sw::editor
{
    namespace
    {
        struct SequencerViewModelInternal
        {
            /** @brief 문맥 플레이어가 없을 때 미리보기가 쓰는 초당 프레임입니다(플레이어 기본값과 같다). */
            static constexpr float32 kDefaultFramesPerSecond = 30.0f;

            /** @brief 에디터에서 고른 오브젝트입니다. 고른 것이 없으면 nullptr 입니다(메인 씬의 선택 — 언리얼 시퀀서도 레벨 선택의 액터를 바인딩에 더한다). */
            static GameObject* findSelectedObject() { return editor::getMainSceneSelection().getPrimaryObject(); }
        };
    } // namespace

    SequencerViewModel::SequencerViewModel()
        : _listPreviewSpan{}
        , _listPreviewSpawn{}
        , _listNoSlot{}
        , _contextPlayer{}
    {
    }

    SequencerViewModel::~SequencerViewModel() = default;

    SequencePlayerComponent* SequencerViewModel::findBindingContext( string_view documentPath ) const
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr || documentPath.empty() )
            return nullptr;
        SequencePlayerComponent* pFirst = nullptr;
        for ( GameObject* pObject : pManager->getAllGameObjects() )
        {
            SequencePlayerComponent* pPlayer = pObject != nullptr ? pObject->getComponent<SequencePlayerComponent>() : nullptr;
            // 리소스 id 는 대소문자를 가리지 않는다.
            if ( pPlayer == nullptr || StringUtil::equals( pPlayer->getSequencePath(), documentPath, true ) == false )
                continue;
            if ( pObject->getHandle() == _contextPlayer )
                return pPlayer;
            if ( pFirst == nullptr )
                pFirst = pPlayer;
        }
        return pFirst;
    }

    SequenceBindingView SequencerViewModel::makeBindingView( const SequenceAsset& asset, string_view documentPath ) const
    {
        const SequencePlayerComponent* pContext = findBindingContext( documentPath );
        SequenceBindingView            view;
        view._pAsset       = &asset;
        view._pListSlot    = pContext != nullptr ? &pContext->getBindings() : &_listNoSlot;
        view._pListSpawned = &_listPreviewSpawn;
        return view;
    }

    void SequencerViewModel::gather( const SequenceAsset& asset, string_view documentPath, SequencerSceneViewState& outState ) const
    {
        outState._listBoundName.clear();
        outState._contextName.clear();
        outState._selectionName.clear();
        const SequencePlayerComponent* pContext      = findBindingContext( documentPath );
        const GameObject*              pContextOwner = pContext != nullptr ? pContext->getOwner() : nullptr;
        const GameObject*              pSelected     = SequencerViewModelInternal::findSelectedObject();
        outState._bHasContext                        = pContext != nullptr ? SW_TRUE : SW_FALSE;
        outState._bHasSelection                      = pSelected != nullptr ? SW_TRUE : SW_FALSE;
        if ( pContextOwner != nullptr )
            outState._contextName = pContextOwner->getName().c_str();
        if ( pSelected != nullptr )
            outState._selectionName = pSelected->getName().c_str();

        const SequenceBindingView view     = makeBindingView( asset, documentPath );
        GameObjectManager*        pManager = editor::getActiveObjectManager();
        for ( const SequenceBinding& binding : asset._listBinding )
        {
            const GameObject* pBound = SequenceTimelineUtil::findTarget( pManager, view, binding._id );
            outState._listBoundName.push_back( pBound != nullptr ? string{ pBound->getName().c_str() } : string{} );
        }
    }

    void SequencerViewModel::resetContext( string_view documentPath )
    {
        _contextPlayer                          = GameObjectHandle{};
        const SequencePlayerComponent* pContext = findBindingContext( documentPath );
        if ( pContext != nullptr && pContext->getOwner() != nullptr )
            _contextPlayer = pContext->getOwner()->getHandle();
    }

    bool SequencerViewModel::bindSelection( string_view documentPath, const uint32 bindingID ) const
    {
        EditorTransaction        transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        SequencePlayerComponent* pContext = findBindingContext( documentPath );
        GameObject*              pTarget  = SequencerViewModelInternal::findSelectedObject();
        GameObject*              pOwner   = pContext != nullptr ? pContext->getOwner() : nullptr;
        if ( pOwner == nullptr || pTarget == nullptr )
            return false;
        // 묶음은 씬의 데이터(플레이어의 바인딩 표)다 — 오브젝트 수정으로 되돌리기에 남긴다.
        const ObjectSnapshot before = EditorTransaction::captureSnapshot( pOwner );
        pContext->setBinding( bindingID, pTarget->getHandle() );
        transaction.recordModify( pOwner, before, EditorTransaction::captureSnapshot( pOwner ), "Bind Sequence Object" );
        return true;
    }

    uint32 SequencerViewModel::countUnboundBindings( const SequenceAsset& asset, string_view documentPath ) const
    {
        const SequenceBindingView view     = makeBindingView( asset, documentPath );
        GameObjectManager*        pManager = editor::getActiveObjectManager();
        uint32                    unbound  = 0;
        for ( const SequenceBinding& binding : asset._listBinding )
        {
            const bool bSpawned = asset.findSpawnItem( binding._id ) != nullptr;
            if ( bSpawned == false && SequenceTimelineUtil::findTarget( pManager, view, binding._id ) == nullptr )
                ++unbound;
        }
        return unbound;
    }

    bool SequencerViewModel::readTrackValues( const SequenceAsset& asset, string_view documentPath, const SequenceKeyTrack& track, float32* pOutArrValue, const uint32 valueCapacity ) const
    {
        GameObject* pTarget = SequenceTimelineUtil::findTarget( editor::getActiveObjectManager(), makeBindingView( asset, documentPath ), track._binding );
        return SequenceTimelineUtil::readTrackValues( pTarget, track, pOutArrValue, valueCapacity );
    }

    void SequencerViewModel::applyPreviewFrame( const SequenceAsset& asset, string_view documentPath, const int32 frame )
    {
        const SequencePlayerComponent* pContext = findBindingContext( documentPath );
        const float32                  fps      = pContext != nullptr ? pContext->getFramesPerSecond() : SequencerViewModelInternal::kDefaultFramesPerSecond;
        EditorViewportPreview::applySequenceFrame( asset, makeBindingView( asset, documentPath ), frame, fps, _listPreviewSpan );
    }

    void SequencerViewModel::releasePreview( const SequenceAsset& asset, string_view documentPath )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        SequenceTimelineUtil::releaseSpans( pManager, asset, makeBindingView( asset, documentPath ), SequencerViewModelInternal::kDefaultFramesPerSecond, _listPreviewSpan );
        SequenceTimelineUtil::releaseCameraCut( pManager, asset );
    }

    EditorAssetType SequencerViewModel::getDocumentType()
    {
        return EditorAssetType{ SequenceAsset::kAssetType };
    }

    ToolAssetLoadResult SequencerViewModel::loadSequence( SequenceAsset& outAsset, string_view path )
    {
        return EditorToolAssetCommands::loadSequence( outAsset, path );
    }

    bool SequencerViewModel::saveSequence( const SequenceAsset& asset, string_view path )
    {
        return EditorToolAssetCommands::saveSequence( asset, path );
    }
} // namespace sw::editor
