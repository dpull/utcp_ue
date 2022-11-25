#include "UTcpNetConnection.h"
#include "Containers/Array.h"
#include "Net/DataChannel.h"
#include "Net/Core/Misc/PacketAudit.h"
#include "UTcpNetDriver.h"
#include "Engine/PackageMapClient.h"

UUTcpConnection::UUTcpConnection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FUTcpFD::GlobalConfig();
}

UUTcpConnection::~UUTcpConnection()
{
	if (UTcpFD)
	{
		delete UTcpFD;
	}
}

void UUTcpConnection::InitSequence(int32 IncomingSequence, int32 OutgoingSequence)
{
	// Driver maybe nullptr
	if (IsUseUTcp(Driver))
	{
		UTcpFD = new FUTcpFD(this, Driver);
		UTcpFD->InitSequence(IncomingSequence, OutgoingSequence);
	}
	Super::InitSequence(IncomingSequence, OutgoingSequence);
}

void UUTcpConnection::InitRemoteConnection(UNetDriver* InDriver, FSocket* InSocket, const FURL& InURL,
	const FInternetAddr& InRemoteAddr, EConnectionState InState, int32 InMaxPacket, int32 InPacketOverhead)
{
	if (IsUseUTcp(InDriver))
	{
		UTcpFD = new FUTcpFD(this, InDriver);
		UTcpFD->InitSequence(InPacketId+1, OutPacketId);
	}
	Super::InitRemoteConnection(InDriver, InSocket, InURL, InRemoteAddr, InState, InMaxPacket, InPacketOverhead);
}

void UUTcpConnection::ReceivedRawPacket(void* Data, int32 Count)
{
	if (!UTcpFD)
	{
		Super::ReceivedRawPacket(Data, Count);
		return;
	}

	LastReceiveTime = Driver->GetElapsedTime();
	UTcpFD->ReceivedRawPacket(Data, Count);
}

void UUTcpConnection::Tick(float DeltaSeconds)
{
	if (!UTcpFD)
	{
		Super::Tick(DeltaSeconds);
		if (OutPacketId > 0)
			UE_LOG(LogUTcp, Log, TEXT("Raw Out[%d/%d], In:%d"), OutPacketId, OutAckPacketId, InPacketId);
		return;
	}

	UTcpFD->Tick();

	FlushNet(false);

	FChannelsToClose ChannelsToClose;
	InternalAck(OutPacketId, ChannelsToClose);
}

void UUTcpConnection::FlushNet(bool bIgnoreSimulation)
{
	if (!UTcpFD)
	{
		Super::FlushNet(bIgnoreSimulation);
		return;
	}

	UTcpFD->FlushNet();
}

int32 UUTcpConnection::UTcpSendRawBunch(FOutBunch* Bunch, bool Merge)
{
	ensure(UTcpFD);
	return UTcpFD->SendBunch(Bunch);
}

void UUTcpConnection::UTcpPostTickDispatch()
{
	if (UTcpFD)
		UTcpFD->PostTickDispatch();
}

void UUTcpConnection::InternalAck(int32 AckPacketId, FChannelsToClose& OutChannelsToClose)
{
	UE_LOG(LogNetTraffic, Verbose, TEXT("   Received ack %i"), AckPacketId);
	
	// Advance OutAckPacketId
	OutAckPacketId = AckPacketId;

	if (PackageMap != NULL)
	{
		PackageMap->ReceivedAck( AckPacketId );
	}

	auto AckChannelFunc = [this, &OutChannelsToClose](int32 AckedPacketId, uint32 ChannelIndex)
	{
		UChannel* const Channel = Channels[ChannelIndex];

		if (Channel)
		{
			if (Channel->OpenPacketId.Last == AckedPacketId) // Necessary for unreliable "bNetTemporary" channels.
				{
				Channel->OpenAcked = 1;
				}
				
			for (FOutBunch* OutBunch = Channel->OutRec; OutBunch; OutBunch = OutBunch->Next)
			{
				ensure(false);
			}
			Channel->ReceivedAck(AckedPacketId);
			EChannelCloseReason CloseReason;
			if (Channel->ReceivedAcks(CloseReason))
			{
				const FChannelCloseInfo Info = {ChannelIndex, CloseReason};
				OutChannelsToClose.Emplace(Info);
			}	
		}
	};

	// TODO 可以优化数量
	for (auto It = ActorChannelConstIterator(); It; ++It)
	{
		AckChannelFunc(AckPacketId, It->Value->ChIndex);
	}
}

void UUTcpChannel::Tick()
{
	if (!IsUseUTcp(this))
	{
		UE_LOG(LogUTcp, Log, TEXT("RAW Channel %d InRec:%d, OutRec:%d"), ChIndex, NumInRec, NumOutRec);
	}
}

FPacketIdRange UUTcpChannel::UTcpSendBunch(FOutBunch* Bunch, bool Merge)
{
	using namespace UE::Net;

	if (!ensure(ChIndex != -1))
	{
		// Client "closing" but still processing bunches. Client->Server RPCs should avoid calling this, but perhaps more code needs to check this condition.
		return FPacketIdRange(INDEX_NONE);
	}

	check(!Closing);
	checkf(Connection->Channels[ChIndex] == this, TEXT("This: %s, Connection->Channels[ChIndex]: %s"), *Describe(), Connection->Channels[ChIndex] ? *Connection->Channels[ChIndex]->Describe() : TEXT("Null"));
	check(!Bunch->IsError());
	check(!Bunch->bHasPackageMapExports);

	// Set bunch flags.

	const bool bDormancyClose = Bunch->bClose && (Bunch->CloseReason == EChannelCloseReason::Dormancy);

	if (OpenedLocally && ((OpenPacketId.First == INDEX_NONE) || ((Connection->ResendAllDataState != EResendAllDataState::None) && !bDormancyClose)))
	{
		bool bOpenBunch = true;

		if (Connection->ResendAllDataState == EResendAllDataState::SinceCheckpoint)
		{
			bOpenBunch = !bOpenedForCheckpoint;
			bOpenedForCheckpoint = true;
		}

		if (bOpenBunch)
		{
			Bunch->bOpen = 1;
			OpenTemporary = !Bunch->bReliable;
		}
	}

	// If channel was opened temporarily, we are never allowed to send reliable packets on it.
	check(!OpenTemporary || !Bunch->bReliable);

	TArray<FOutBunch*>& OutgoingBunches = Connection->GetOutgoingBunches();
	OutgoingBunches.Reset();

	// Add any export bunches
	// Replay connections will manage export bunches separately.
	if (!Connection->IsInternalAck())
	{
		AppendExportBunches(OutgoingBunches);
	}

	if (OutgoingBunches.Num())
	{
		// Don't merge if we are exporting guid's
		// We can't be for sure if the last bunch has exported guids as well, so this just simplifies things
		Merge = false;
	}

	if (Connection->Driver->IsServer())
	{
		// Append any "must be mapped" guids to front of bunch from the packagemap
		AppendMustBeMappedGuids(Bunch);

		if (Bunch->bHasMustBeMappedGUIDs)
		{
			// We can't merge with this, since we need all the unique static guids in the front
			Merge = false;
		}
	}
	OutgoingBunches.Add(Bunch);

	//-----------------------------------------------------
	// Send all the bunches we need to
	//	Note: this is done all at once. We could queue this up somewhere else before sending to Out.
	//-----------------------------------------------------
	FPacketIdRange PacketIdRange;

	for (int32 PartialNum = 0; PartialNum < OutgoingBunches.Num(); ++PartialNum)
	{
		FOutBunch* NextBunch = OutgoingBunches[PartialNum];

		NextBunch->bReliable = Bunch->bReliable;
		NextBunch->bOpen = Bunch->bOpen;
		NextBunch->bClose = Bunch->bClose;
		NextBunch->CloseReason = Bunch->CloseReason;
		NextBunch->bIsReplicationPaused = Bunch->bIsReplicationPaused;
		NextBunch->ChIndex = Bunch->ChIndex;
		NextBunch->ChName = Bunch->ChName;

		if (!NextBunch->bHasPackageMapExports)
		{
			NextBunch->bHasMustBeMappedGUIDs |= Bunch->bHasMustBeMappedGUIDs;
		}

		if (OutgoingBunches.Num() > 1)
		{
			NextBunch->bPartial = 1;
			NextBunch->bPartialInitial = (PartialNum == 0 ? 1 : 0);
			NextBunch->bPartialFinal = (PartialNum == OutgoingBunches.Num() - 1 ? 1 : 0);
			NextBunch->bOpen &= (PartialNum == 0);											  // Only the first bunch should have the bOpen bit set
			NextBunch->bClose = (Bunch->bClose && (OutgoingBunches.Num() - 1 == PartialNum)); // Only last bunch should have bClose bit set
		}

		// FOutBunch *ThisOutBunch = PrepBunch(NextBunch, OutBunch, Merge); // This handles queuing reliable bunches into the ack list
		FOutBunch *ThisOutBunch = NextBunch;

		// Update Packet Range
		int32 PacketId = UTcpSendRawBunch(ThisOutBunch, Merge);
		if (PartialNum == 0)
		{
			PacketIdRange = FPacketIdRange(PacketId);
		}
		else
		{
			PacketIdRange.Last = PacketId;
		}

		// Update channel sequence count.
		Connection->LastOut = *ThisOutBunch;
		Connection->LastEnd	= FBitWriterMark( Connection->SendBuffer );
	}

	// Update open range if necessary
	if (Bunch->bOpen && (Connection->ResendAllDataState == EResendAllDataState::None))
	{
		OpenPacketId = PacketIdRange;
	}

	// Destroy outgoing bunches now that they are sent, except the one that was passed into ::SendBunch
	//	This is because the one passed in ::SendBunch is the responsibility of the caller, the other bunches in OutgoingBunches
	//	were either allocated in this function for partial bunches, or taken from the package map, which expects us to destroy them.
	for (auto It = OutgoingBunches.CreateIterator(); It; ++It)
	{
		FOutBunch* DeleteBunch = *It;
		if (DeleteBunch != Bunch)
			delete DeleteBunch;
	}

	return PacketIdRange;
}

int32 UUTcpChannel::UTcpSendRawBunch(FOutBunch* OutBunch, bool Merge)
{
	// Sending for checkpoints may need to send an open bunch if the actor went dormant, so allow the OpenPacketId to be set

	// Send the raw bunch.
	OutBunch->ReceivedAck = 0;
	int32 PacketId = Cast<UUTcpConnection>(Connection)->UTcpSendRawBunch(OutBunch, Merge);
	if( OpenPacketId.First==INDEX_NONE && OpenedLocally )
	{
		OpenPacketId = FPacketIdRange(PacketId);
	}

	if( OutBunch->bClose )
	{
		SetClosingFlag();
	}

	// return PacketId;
	return Connection->OutPacketId;
}
