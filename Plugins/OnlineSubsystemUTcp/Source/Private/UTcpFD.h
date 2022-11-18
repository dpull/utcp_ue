#pragma once
#include "UTcpNetConnection.h"
#include "IpNetDriver.h"

class FUTcpFD
{
  public:
	static void GlobalConfig();
	static void AddElapsedTime(int64 NS);

	FUTcpFD(UNetConnection* InConn, UNetDriver* InDriver);
	~FUTcpFD();

	bool Accept(FUTcpFD* listener, bool reconnect);
	bool IsCookieEqual(FUTcpFD* listener);
	void InitSequence(int32 IncomingSequence, int32 OutgoingSequence);

	void Tick();
	void PostTickDispatch();
	void FlushNet();

	void ReceivedRawPacket(void* InData, int32 Count);
	int64 SendBunch(const FOutBunch* InBunch);

  protected:
	void OnAccept(bool reconnect);
	void OnRawSend(const void* data, int len);
	void OnRecv(const struct utcp_bunch* bunches[], int count);
	void OnDeliveryStatus(int32_t packet_id, bool ack);

  private:
	void ProcOrderedCache(bool flushing_order_cache);

  protected:
	
	TObjectPtr<UNetConnection> Connection;
	TObjectPtr<UNetDriver> Driver;
	struct utcp_fd* UTcpFD = nullptr;
	struct utcp_packet_view_ordered_queue* OrderedCache = nullptr;
};
