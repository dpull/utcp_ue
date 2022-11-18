#include "UTcpFD.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <queue>

#include "BunchConvert.h"
#include "Sockets.h"
#include "utcp/utcp.h"

struct utcp_packet_view
{
	uint32_t packet_id;

	uint16_t data_len;
	uint8_t data[UTCP_MAX_PACKET];

	bool operator()(const utcp_packet_view* l, const utcp_packet_view* r) const
	{
		return l->packet_id > r->packet_id;
	}
};

struct utcp_packet_view_ordered_queue
{
	void push(utcp_packet_view* view)
	{
		queue.push(view);
	}

	utcp_packet_view* pop(int packet_id = -1)
	{
		if (queue.empty())
			return nullptr;
		auto view = queue.top();
		if (packet_id != -1 && view->packet_id != packet_id)
			return nullptr;
		queue.pop();
		return view;
	}

	std::priority_queue<utcp_packet_view*, std::vector<utcp_packet_view*>, utcp_packet_view> queue;
};

static void LogWrapper(int InLevel, const char* InFormat, va_list InArgs)
{
	char TempString[1024];
	FCStringAnsi::GetVarArgs(TempString, UE_ARRAY_COUNT(TempString), InFormat, InArgs);
	UE_LOG(LogUTcp, Display, TEXT("%s"), UTF8_TO_TCHAR(TempString));
}

void FUTcpFD::GlobalConfig()
{
	auto config = utcp_get_config();
	config->on_accept = [](struct utcp_fd* fd, void* userdata, bool reconnect) {
		auto conn = static_cast<FUTcpFD*>(userdata);
		assert(conn->UTcpFD == fd);
		conn->OnAccept(reconnect);
	};
	config->on_raw_send = [](struct utcp_fd* fd, void* userdata, const void* data, int len) {
		auto conn = static_cast<FUTcpFD*>(userdata);
		assert(conn->UTcpFD == fd);
		conn->OnRawSend(data, len);
	};
	config->on_recv = [](struct utcp_fd* fd, void* userdata, const struct utcp_bunch* bunches[], int count) {
		auto conn = static_cast<FUTcpFD*>(userdata);
		assert(conn->UTcpFD == fd);
		conn->OnRecv(bunches, count);
	};
	config->on_delivery_status = [](struct utcp_fd* fd, void* userdata, int32_t packet_id, bool ack) {
		auto conn = static_cast<FUTcpFD*>(userdata);
		assert(conn->UTcpFD == fd);
		conn->OnDeliveryStatus(packet_id, ack);
	};
	config->on_log = LogWrapper;

	config->enable_debug_cookie = true;
	for (int i = 0; i < sizeof(config->debug_cookie); ++i)
	{
		config->debug_cookie[i] = i + 1;
	}
}

void FUTcpFD::AddElapsedTime(int64 NS)
{
	utcp_add_time(NS);
}

FUTcpFD::FUTcpFD(UNetConnection* InConn, UNetDriver* InDriver) : Connection(InConn), Driver(InDriver)
{
	UTcpFD = new utcp_fd;
	utcp_init(UTcpFD, this, !!Driver->ServerConnection);
}

FUTcpFD::~FUTcpFD()
{
	if (OrderedCache)
	{
		delete OrderedCache;
	}
}

bool FUTcpFD::Accept(FUTcpFD* listener, bool reconnect)
{
	memcpy(UTcpFD->LastChallengeSuccessAddress, listener->UTcpFD->LastChallengeSuccessAddress, sizeof(UTcpFD->LastChallengeSuccessAddress));

	assert(!OrderedCache);
	OrderedCache = new utcp_packet_view_ordered_queue;

	if (!reconnect)
	{
		memcpy(UTcpFD->AuthorisedCookie, listener->UTcpFD->AuthorisedCookie, sizeof(UTcpFD->AuthorisedCookie));
		utcp_sequence_init(UTcpFD, listener->UTcpFD->LastClientSequence, listener->UTcpFD->LastServerSequence);
		UE_LOG(LogUTcp, Display, TEXT("Accept:(%d, %d)"), listener->UTcpFD->LastClientSequence, listener->UTcpFD->LastServerSequence);
	}
	return true;
}

bool FUTcpFD::IsCookieEqual(FUTcpFD* listener)
{
	return memcmp(UTcpFD->AuthorisedCookie, listener->UTcpFD->AuthorisedCookie, sizeof(UTcpFD->AuthorisedCookie)) == 0;
}

void FUTcpFD::InitSequence(int32 IncomingSequence, int32 OutgoingSequence)
{
	utcp_sequence_init(UTcpFD, IncomingSequence, OutgoingSequence);

	assert(!OrderedCache);
	OrderedCache = new utcp_packet_view_ordered_queue;
}

void FUTcpFD::Tick()
{
	utcp_update(UTcpFD);
	ProcOrderedCache(false);
}

void FUTcpFD::PostTickDispatch()
{
	ProcOrderedCache(true);
}

void FUTcpFD::FlushNet()
{
	utcp_flush(UTcpFD);
	UE_LOG(LogUTcp, Display, TEXT("utcp_flush:(%d, %d)"));
}

void FUTcpFD::ReceivedRawPacket(void* InData, int32 Count)
{
	auto PacketId = utcp_peep_packet_id(UTcpFD, (uint8*)InData, Count);
	if (PacketId <= 0)
	{
		utcp_ordered_incoming(UTcpFD, (uint8*)InData, Count);
		return;
	}

	auto View = new utcp_packet_view;
	View->packet_id = PacketId;
	View->data_len = Count;
	memcpy(View->data, InData, Count);
	OrderedCache->push(View);
}

int64 FUTcpFD::SendBunch(const FOutBunch* InBunch)
{
	utcp_bunch Bunch;
	FConvert::To(InBunch, &Bunch);
	return utcp_send_bunch(UTcpFD, &Bunch);
}

void FUTcpFD::OnAccept(bool reconnect)
{
}

void FUTcpFD::ProcOrderedCache(bool flushing_order_cache)
{
	while (true)
	{
		int handle = -1;
		if (!flushing_order_cache)
		{
			handle = utcp_expect_packet_id(UTcpFD);
		}

		auto view = OrderedCache->pop(handle);
		if (!view)
			break;

		utcp_ordered_incoming(UTcpFD, view->data, view->data_len);
		delete view;
	}
}

void FUTcpFD::OnRawSend(const void* data, int len)
{
	auto Conn = Cast<UIpConnection>(Connection);
	int32 BytesSent;
	Conn->Socket->SendTo((uint8*)data, len, BytesSent, *Conn->RemoteAddr);
}

void FUTcpFD::OnRecv(const utcp_bunch* bunches[], int count)
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

void FUTcpFD::OnDeliveryStatus(int32_t packet_id, bool ack)
{
}

