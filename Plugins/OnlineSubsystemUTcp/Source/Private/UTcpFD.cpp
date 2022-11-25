#include "UTcpFD.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include "BunchConvert.h"
#include "Sockets.h"
extern "C" {
#include "utcp/utcp_def_internal.h"
}

static void LogWrapper(int InLevel, const char* InFormat, va_list InArgs)
{
	char TempString[1024];
	FCStringAnsi::GetVarArgs(TempString, UE_ARRAY_COUNT(TempString), InFormat, InArgs);
	UE_LOG(LogUTcp, Log, TEXT("[UTCP]%s"), UTF8_TO_TCHAR(TempString));
}

void FUTcpFD::GlobalConfig()
{
	config(LogWrapper);
	enbale_dump_data(true);
}

void FUTcpFD::AddElapsedTime(int64 NS)
{
	add_elapsed_time(NS);
}

FUTcpFD::FUTcpFD(UNetConnection* InConn, UNetDriver* InDriver) : Connection(InConn), Driver(InDriver)
{
}

extern "C" void utcp_sequence_init(struct utcp_connection* fd, int32_t IncomingSequence, int32_t OutgoingSequence);
void FUTcpFD::InitSequence(int32 IncomingSequence, int32 OutgoingSequence)
{
	utcp_sequence_init(get_fd(), IncomingSequence, OutgoingSequence);
}

void FUTcpFD::Tick()
{
	UE_LOG(LogUTcp, Log, TEXT("utcp Out[%d/%d], In:%d"), get_fd()->OutPacketId, get_fd()->OutAckPacketId, get_fd()->InPacketId);
	update();
}

void FUTcpFD::PostTickDispatch()
{
	flush_incoming_cache();
}

void FUTcpFD::FlushNet()
{
	send_flush();
	UE_LOG(LogUTcp, Display, TEXT("utcp_flush"));
}

void FUTcpFD::ReceivedRawPacket(void* InData, int32 Count)
{
	incoming((uint8_t*)InData, Count);
}

int64 FUTcpFD::SendBunch(const FOutBunch* InBunch)
{
	utcp_bunch Bunch;
	FConvert::To(InBunch, &Bunch);
	return utcp_send_bunch(get_fd(), &Bunch);
}

void FUTcpFD::on_connect(bool reconnect)
{
}

void FUTcpFD::on_disconnect(int close_reason)
{
}

void FUTcpFD::on_outgoing(const void* data, int len)
{
#if DO_ENABLE_NET_TEST
	if (Connection->PacketSimulationSettings.PktLoss > 0 && FMath::FRand() * 100.f < Connection->PacketSimulationSettings.PktLoss)
	{
		UE_LOG(LogUTcp, Display, TEXT("OnRawSend Drop"));
		return;
	}
#endif

	auto Conn = Cast<UIpConnection>(Connection);
	int32 BytesSent;
	Conn->Socket->SendTo((uint8*)data, len, BytesSent, *Conn->RemoteAddr);
}

void FUTcpFD::on_recv_bunch(utcp_bunch* const bunches[], int count)
{
	auto Bunch = bunches[0];
	UChannel* Channel = Connection->Channels[Bunch->ChIndex];
	if (!Channel)
	{
		ensure(Bunch->bOpen);
		auto ChName = EName(Bunch->NameIndex); 
		Channel = Connection->CreateChannelByName(ChName, EChannelCreateFlags::None, Bunch->ChIndex);
	}

	for (int i = 0; i < count; ++i)
	{
		bool bOutSkipAck;
		FInBunch InBunch(Connection);
		FConvert::To(bunches[i], &InBunch);

		if (InBunch.bReliable)
			InBunch.ChSequence = Connection->InReliable[InBunch.ChIndex] + 1;
		Channel->ReceivedRawBunch(InBunch, bOutSkipAck);
	}
}

void FUTcpFD::on_delivery_status(int32_t packet_id, bool ack)
{
	UE_LOG(LogUTcp, Display, TEXT("OnDeliveryStatus:(%d, %s)"), packet_id, ack ? TEXT("ACK") : TEXT("NAK"));
}


