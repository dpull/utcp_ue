#pragma once
#include "UTcpNetConnection.h"
#include "IpNetDriver.h"
#include "abstract/utcp.hpp"

class FUTcpFD : public utcp::conn
{
  public:
	static void GlobalConfig();
	static void AddElapsedTime(int64 NS);

	FUTcpFD(UNetConnection* InConn, UNetDriver* InDriver);
	void InitSequence(int32 IncomingSequence, int32 OutgoingSequence);

	void Tick();
	void PostTickDispatch();
	void FlushNet();

	void ReceivedRawPacket(void* InData, int32 Count);
	int64 SendBunch(const FOutBunch* InBunch);

  protected:
	virtual void on_connect(bool reconnect) override;
	virtual void on_disconnect(int close_reason) override;
	virtual void on_outgoing(const void* data, int len) override;
	virtual void on_recv_bunch(struct utcp_bunch* const bunches[], int count) override;
	virtual void on_delivery_status(int32_t packet_id, bool ack) override;
	
	TObjectPtr<UNetConnection> Connection;
	TObjectPtr<UNetDriver> Driver;
};
