#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UTcpNetConnection.h"
#include "IpNetDriver.h"
#include  "UTcpFD.h"
#include "UTcpNetDriver.generated.h"

UCLASS(transient, config = OnlineSubsystemUTcp)
class UUTcpNetDriver : public UIpNetDriver
{
	GENERATED_BODY()

public:
	UUTcpNetDriver(const FObjectInitializer& ObjectInitializer);
	virtual ~UUTcpNetDriver();

	virtual void InitConnectionlessHandler() override;
	virtual void TickDispatch(float DeltaTime) override;
	virtual void LowLevelSend(TSharedPtr<const FInternetAddr> Address, void* Data, int32 CountBits, FOutPacketTraits& Traits) override;
	virtual void Shutdown() override;
	virtual void PostTickDispatch() override;

	bool IsUseUTcp();
};
