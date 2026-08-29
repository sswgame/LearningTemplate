#include "pch.h"

#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
	namespace
	{
		struct SequenceTimelineUtilInternal
		{
			static float32 clipProgress( const SequenceTrackItem& item, int32 frame )
			{
				const int32 span = item._end - item._start;
				if ( span <= 0 )
					return 1.0f;
				const float32 t = static_cast<float32>( frame - item._start ) / static_cast<float32>( span );
				return MathUtil::clamp( t, 0.0f, 1.0f );
			}

			static bool isClipActive( const vector<const SequenceTrackItem*>& listActive, const SequenceTrackItem& item )
			{
				for ( const SequenceTrackItem* pActive : listActive )
				{
					if ( pActive == &item )
						return true;
				}
				return false;
			}

			static GameObject* findTarget( GameObjectManager* pManager, string_view name )
			{
				if ( pManager == nullptr || name.empty() )
					return nullptr;
				return pManager->findGameObjectByName( hashed_string( string{ name }.c_str() ) );
			}

			static void applyClipTransform( GameObject* pTarget, const SequenceTrackItem& item, int32 frame )
			{
				if ( pTarget == nullptr || SequenceTimelineUtil::hasTransform( item ) == false )
					return;
				SceneComponent* pScene = pTarget->getPrimarySceneComponent();
				if ( pScene == nullptr )
					return;

				const float32 t			  = clipProgress( item, frame );
				const float3  translation = float3::lerp( float3{}, item._translation, t );
				const float3  rotation	  = float3::lerp( float3{}, item._rotation, t );
				const float3  scale		  = float3::lerp( float3{ 1.0f, 1.0f, 1.0f }, item._scale, t );
				pScene->setLocalPosition( translation );
				pScene->setLocalRotation( rotation );
				pScene->setLocalScale( scale );
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "SequenceTimeline" );

	bool SequenceTimelineUtil::hasTransform( const SequenceTrackItem& item )
	{
		if ( ( item._translation == float3{} ) == false )
			return true;
		if ( ( item._rotation == float3{} ) == false )
			return true;
		if ( ( item._scale == float3{ 1.0f, 1.0f, 1.0f } ) == false )
			return true;
		return false;
	}

	void SequenceTimelineUtil::applyFrame( GameObjectManager* pManager, const SequenceAsset& asset, int32 frame, int32 previousFrame )
	{
		if ( pManager == nullptr )
			return;

		vector<const SequenceTrackItem*> listActive;
		asset.collectActiveItems( frame, listActive );

		for ( const SequenceTrackItem& item : asset._listItem )
		{
			if ( item._type != 0 || item._targetObject.empty() )
				continue;
			if ( SequenceTimelineUtilInternal::isClipActive( listActive, item ) )
				continue;
			GameObject* pTarget = SequenceTimelineUtilInternal::findTarget( pManager, item._targetObject );
			if ( pTarget != nullptr )
				pTarget->setActive( false );
		}

		for ( const SequenceTrackItem* pItem : listActive )
		{
			if ( pItem == nullptr || pItem->_targetObject.empty() )
				continue;
			GameObject* pTarget = SequenceTimelineUtilInternal::findTarget( pManager, pItem->_targetObject );
			if ( pTarget == nullptr )
				continue;
			if ( pItem->_type == 0 )
			{
				pTarget->setActive( true );
				SequenceTimelineUtilInternal::applyClipTransform( pTarget, *pItem, frame );
			}
		}

		if ( previousFrame < 0 )
			return;
		for ( const SequenceTrackItem& item : asset._listItem )
		{
			if ( item._type != 1 )
				continue;
			if ( previousFrame >= item._start || item._start > frame )
				continue;
			SW_LOG_INFO( "Sequence event %# on %#", item._name.c_str(), item._targetObject.c_str() );
		}
	}
} // namespace sw
