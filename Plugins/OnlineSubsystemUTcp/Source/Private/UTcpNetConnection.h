#pragma once

#include "CoreMinimal.h"
#include "IpConnection.h"
#include "Engine/ControlChannel.h"
#include "Engine/ActorChannel.h"
#include "Engine/NetConnection.h"
#include "UTcpNetConnection.generated.h"

extern bool IsUseUTcp(UNetDriver* Driver);
inline bool IsUseUTcp(UNetConnection* Connection)
{
	return IsUseUTcp(Connection->Driver);
}

inline bool IsUseUTcp(UChannel* Channel)
{
	return IsUseUTcp(Channel->Connection);
}

UCLASS(transient, config = OnlineSubsystemUTcp)
class UUTcpConnection : public UIpConnection
{
public:
	GENERATED_BODY()
	UUTcpConnection(const FObjectInitializer& ObjectInitializer);
	virtual ~UUTcpConnection() override;
	
	virtual void InitSequence(int32 IncomingSequence, int32 OutgoingSequence) override;
	virtual void InitRemoteConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, const class FInternetAddr& InRemoteAddr, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;

	virtual void ReceivedRawPacket(void* Data,int32 Count) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void FlushNet(bool bIgnoreSimulation) override;

public:
	int32 UTcpSendRawBunch(FOutBunch* Bunch, bool Merge);
	void UTcpPostTickDispatch();

private:
	void InternalAck(int32 AckPacketId, FChannelsToClose& OutChannelsToClose);

private:
	class FUTcpFD* UTcpFD = nullptr;
};

class UUTcpChannel : public UChannel
{
public:
	virtual void Tick() override;
	FPacketIdRange UTcpSendBunch(FOutBunch* Bunch, bool Merge);
private:
	int32 UTcpSendRawBunch(FOutBunch* OutBunch, bool Merge);
};
static_assert(sizeof(UUTcpChannel) == sizeof(UChannel));

UCLASS(transient, customConstructor)
class UUTcpControlChannel : public UControlChannel
{
	GENERATED_UCLASS_BODY()

	UUTcpControlChannel(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get())
		: UControlChannel(ObjectInitializer)
	{
	}

	virtual FPacketIdRange SendBunch(FOutBunch* Bunch, bool Merge) override
	{
		if (!IsUseUTcp(this))
			return Super::SendBunch(Bunch, Merge);
		else
			return ((UUTcpChannel*)this)->UTcpSendBunch(Bunch, Merge);
	}
};

UCLASS(transient, customConstructor)
class UUTcpActorChannel : public UActorChannel
{
	GENERATED_UCLASS_BODY()

	UUTcpActorChannel(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get())
		: UActorChannel(ObjectInitializer)
	{
	}

	virtual void Tick() override
	{
		Super::Tick();
		((UUTcpChannel*)this)->Tick();
	}


	virtual FPacketIdRange SendBunch(FOutBunch* Bunch, bool Merge) override
	{
		if (!IsUseUTcp(this))
			return Super::SendBunch(Bunch, Merge);
		else
			return ((UUTcpChannel*)this)->UTcpSendBunch(Bunch, Merge);
	}
};
