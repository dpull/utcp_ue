#pragma once

#include "CoreMinimal.h"
#include "UTcpSetting.generated.h"

UCLASS(config = OnlineSubsystemUTCP)
class ONLINESUBSYSTEMUTCP_API UOnlineSubsystemUTcpSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(Config)
	bool bEnableServer;

	UPROPERTY(Config)
	bool bEnableClient;

public:
	static UOnlineSubsystemUTcpSettings* Get();
};
