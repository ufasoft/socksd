#pragma once

#include <el/inet/proxy.h>

namespace Ext {
	namespace Inet {

class CProxyRelay : public Object {
public:
	Stream *m_pStm;
	MemoryStream m_qs;

	virtual ~CProxyRelay() {}
	virtual CProxyQuery GetQuery(char beg) { return CProxyQuery(); }
	virtual void SendReply(const IPEndPoint& hp, const error_code& ec = error_code()) {}

	void AfterConnect(Stream& ostm) {
		Blob blob = m_qs.Blob;
		if (blob.Size != 0)
			ostm.WriteBuffer(blob.constData(), blob.Size);
	}

	static CProxyRelay *CreateSocks4Relay();
	static CProxyRelay *CreateSocks5Relay();
	static CProxyRelay *CreateTorSocks5Relay();
	static CProxyRelay *CreateHttpRelay();
};

}} // Ext::Inet::

