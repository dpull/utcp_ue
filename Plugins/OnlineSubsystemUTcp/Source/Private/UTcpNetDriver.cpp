#include "UTcpNetDriver.h"
#include "EngineUtils.h"
#include "UTcpSetting.h"
#include "Net/RepLayout.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "PacketHandlers/StatelessConnectHandlerComponent.h"
#include "utcp/utcp.h"

bool IsUseUTcp(UNetDriver* Driver)
{
	auto UTcpNetDriver = Cast<UUTcpNetDriver>(Driver);
	return UTcpNetDriver ? UTcpNetDriver->IsUseUTcp() : false;
}

UUTcpNetDriver::UUTcpNetDriver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FUTcpFD::GlobalConfig();
}

UUTcpNetDriver::~UUTcpNetDriver()
{
}

void UUTcpNetDriver::InitConnectionlessHandler()
{
	check(!ConnectionlessHandler.IsValid());

#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("NoPacketHandler")))
#endif
	{
		ConnectionlessHandler = MakeUnique<PacketHandler>(&DDoS);

		if (ConnectionlessHandler.IsValid())
		{
			ConnectionlessHandler->NotifyAnalyticsProvider(AnalyticsProvider, AnalyticsAggregator);
			ConnectionlessHandler->Initialize(Handler::Mode::Server, MAX_PACKET_SIZE, true, nullptr, nullptr, NetDriverName);

			// Add handling for the stateless connect handshake, for connectionless packets, as the outermost layer
			TSharedPtr<HandlerComponent> NewComponent =
				ConnectionlessHandler->AddHandler(TEXT("Engine.EngineHandlerComponentFactory(StatelessConnectHandlerComponent)"), true);

			StatelessConnectComponent = StaticCastSharedPtr<StatelessConnectHandlerComponent>(NewComponent);

			if (StatelessConnectComponent.IsValid())
			{
				StatelessConnectComponent.Pin()->SetDriver(this);
			}

			ConnectionlessHandler->InitializeComponents();
		}
	}
}

void UUTcpNetDriver::TickDispatch(float DeltaTime)
{
	if (IsUseUTcp())
	{
		FUTcpFD::AddElapsedTime(DeltaTime * 1000 * 1000 * 1000);
	}
	Super::TickDispatch(DeltaTime);
}

void UUTcpNetDriver::LowLevelSend(TSharedPtr<const FInternetAddr> Address, void* Data, int32 CountBits, FOutPacketTraits& Traits)
{
	Super::LowLevelSend(Address, Data, CountBits, Traits);
}

void UUTcpNetDriver::Shutdown()
{
	Super::Shutdown();
}

void UUTcpNetDriver::PostTickDispatch()
{
	Super::PostTickDispatch();
	if (ServerConnection != nullptr)
	{
		if (IsValid(ServerConnection))
		{
			Cast<UUTcpConnection>(ServerConnection)->UTcpPostTickDispatch();
		}
	}

	TArray<UNetConnection*> ClientConnCopy = ClientConnections;
	for (UNetConnection* CurConn : ClientConnCopy)
	{
		if (IsValid(CurConn))
		{
			Cast<UUTcpConnection>(CurConn)->UTcpPostTickDispatch();
		}
	}
}

bool UUTcpNetDriver::IsUseUTcp()
{
	auto Setting = UOnlineSubsystemUTcpSettings::Get();
	auto bIsClient = !!ServerConnection;
	return (bIsClient && Setting->bEnableClient) || (!bIsClient && Setting->bEnableServer);
}
