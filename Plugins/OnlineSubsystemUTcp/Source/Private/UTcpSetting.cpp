#include "UTcpSetting.h"
#include "Modules/ModuleManager.h"
#include "OnlineSubsystemUtils.h"

class FOnlineSubsystemUTcpModule : public IModuleInterface
{
};

IMPLEMENT_MODULE(FOnlineSubsystemUTcpModule, OnlineSubsystemUTcp);
DEFINE_LOG_CATEGORY(LogUTcp);

UOnlineSubsystemUTcpSettings::UOnlineSubsystemUTcpSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UOnlineSubsystemUTcpSettings* UOnlineSubsystemUTcpSettings::Get()
{
	auto Setting = const_cast<UOnlineSubsystemUTcpSettings*>(GetDefault<UOnlineSubsystemUTcpSettings>());
	return Setting;
}
